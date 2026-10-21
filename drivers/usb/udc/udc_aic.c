/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * USB device controller (UDC) driver for the ArtInChip USB 2.0 device core.
 *
 * The controller is a Synopsys DWC2-derived device-only core that uses the
 * legacy "device subset" register layout (see udc_aic.h). The device-mode bit
 * semantics are standard DWC2; only the register offsets differ. This driver
 * implements the Completer/PIO (slave) transfer mode (no DMA) which is the
 * mode used by the luban-lite reference port.
 *
 * Bring-up (PHY PLL, REXT calibration, DRD->device, clock, reset, PLL lock)
 * and the AIC-specific core init handshake are reproduced from the reference
 * port. Validated on D125CCV demo88-nor: enumeration plus USB MSC file
 * create/read/write over a bulk OUT/IN pair.
 */

#define DT_DRV_COMPAT artinchip_d12p_usbd

#include "udc_common.h"
#include "udc_aic.h"

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/drivers/usb/udc.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/irq.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(udc_aic, CONFIG_UDC_DRIVER_LOG_LEVEL);

#define AIC_UDC_POLL_TMO_US	100000U

struct udc_aic_config {
	uintptr_t base;
	uintptr_t syscfg_base;
	const struct device *clk_dev;
	clock_control_subsys_t phy_clk;
	clock_control_subsys_t dev_clk;
	struct reset_dt_spec phy_reset;
	struct reset_dt_spec dev_reset;
	size_t num_in_eps;
	size_t num_out_eps;
	struct udc_ep_config *ep_cfg_in;
	struct udc_ep_config *ep_cfg_out;
	void (*irq_enable_func)(const struct device *dev);
	void (*irq_disable_func)(const struct device *dev);
	k_thread_stack_t *thread_stk;
	size_t thread_stk_sz;
	int speed_idx;
};

struct udc_aic_data {
	struct k_thread thread_data;
	struct k_sem setup_sem;
	struct k_spinlock lock;
	uint8_t setup[8];
	/*
	 * Expected transfer length per endpoint, tracked separately for each
	 * direction. EP0 IN and EP0 OUT share ep_idx 0, so a single array would
	 * let an OUT (status/setup) transfer clobber the length a pending IN
	 * completion still needs, corrupting net_buf_pull().
	 */
	uint32_t tx_len[AIC_UDC_MAX_EPS];
	uint32_t rx_len[AIC_UDC_MAX_EPS];
	/* Last OUT packet on this endpoint was a zero-length packet. */
	bool rx_zlp[AIC_UDC_MAX_EPS];
	/*
	 * Set once the host has driven a bus reset. A SUSPEND interrupt fired
	 * before that point is just the idle attached-but-unenumerated bus (for
	 * example when powered from a charger or before the terminal is opened).
	 * Reporting it would put the stack into the suspended state and make it
	 * reject IN transfers ("usbd_ep: device is suspended"), so suspend and
	 * resume are only forwarded after a real host session has started.
	 */
	bool bus_reset_seen;
};

/* ------------------------------------------------------------------ */
/* Register helpers                                                    */
/* ------------------------------------------------------------------ */

static inline uint32_t aic_rd(const struct device *dev, uint32_t off)
{
	const struct udc_aic_config *cfg = dev->config;

	return sys_read32(cfg->base + off);
}

static inline void aic_wr(const struct device *dev, uint32_t off, uint32_t val)
{
	const struct udc_aic_config *cfg = dev->config;

	sys_write32(val, cfg->base + off);
}

static inline void aic_set_bits(const struct device *dev, uint32_t off, uint32_t bits)
{
	aic_wr(dev, off, aic_rd(dev, off) | bits);
}

static inline void aic_clear_bits(const struct device *dev, uint32_t off, uint32_t bits)
{
	aic_wr(dev, off, aic_rd(dev, off) & ~bits);
}

static int aic_poll_clear(const struct device *dev, uint32_t off, uint32_t mask)
{
	for (uint32_t i = 0; i < AIC_UDC_POLL_TMO_US; i++) {
		if (!(aic_rd(dev, off) & mask)) {
			return 0;
		}
		k_busy_wait(1);
	}

	return -ETIMEDOUT;
}

static int aic_poll_set(const struct device *dev, uint32_t off, uint32_t mask)
{
	for (uint32_t i = 0; i < AIC_UDC_POLL_TMO_US; i++) {
		if (aic_rd(dev, off) & mask) {
			return 0;
		}
		k_busy_wait(1);
	}

	return -ETIMEDOUT;
}

/* ------------------------------------------------------------------ */
/* PHY / clock / reset bring-up                                        */
/* ------------------------------------------------------------------ */

static void aic_phy_power(const struct device *dev)
{
	const struct udc_aic_config *cfg = dev->config;
	uint32_t val;

	/* Enable the USB PLL so the UTMI+ PHY clock is generated. */
	val = sys_read32(cfg->syscfg_base + AIC_SYSCFG_USB_PLL_COM);
	if ((val & AIC_SYSCFG_USB_PLL_GEN_EN) == 0U) {
		val |= AIC_SYSCFG_USB_PLL_GEN_EN;
		sys_write32(val, cfg->syscfg_base + AIC_SYSCFG_USB_PLL_COM);
	}

	/* Program USB PHY resistor (impedance) calibration. */
	val = sys_read32(cfg->syscfg_base + AIC_SYSCFG_USB0_REXT);
	val &= ~AIC_SYSCFG_USB0_REXT_CAL_MSK;
	val |= AIC_SYSCFG_USB0_REXT_CAL_VAL;
	val |= AIC_SYSCFG_USB0_REXT_CAL_EN;
	sys_write32(val, cfg->syscfg_base + AIC_SYSCFG_USB0_REXT);

	/* Select USB device (DRD) role before releasing the controller. */
	val = sys_read32(cfg->syscfg_base + AIC_SYSCFG_USB0_CFG);
	val |= AIC_SYSCFG_USB0_CFG_DRD_MODE;
	sys_write32(val, cfg->syscfg_base + AIC_SYSCFG_USB0_CFG);

	k_msleep(2);
}

static int aic_phy_clock_reset(const struct device *dev)
{
	const struct udc_aic_config *cfg = dev->config;
	int ret;

	if (!device_is_ready(cfg->clk_dev)) {
		return -ENODEV;
	}

	ret = clock_control_on(cfg->clk_dev, cfg->phy_clk);
	if (ret != 0) {
		return ret;
	}

	ret = clock_control_on(cfg->clk_dev, cfg->dev_clk);
	if (ret != 0) {
		return ret;
	}

	k_busy_wait(AIC_USB_RESET_DELAY_US);

	if (!device_is_ready(cfg->phy_reset.dev) || !device_is_ready(cfg->dev_reset.dev)) {
		return -ENODEV;
	}

	(void)reset_line_assert_dt(&cfg->phy_reset);
	(void)reset_line_assert_dt(&cfg->dev_reset);
	k_busy_wait(AIC_USB_RESET_DELAY_US);
	(void)reset_line_deassert_dt(&cfg->phy_reset);
	(void)reset_line_deassert_dt(&cfg->dev_reset);
	k_busy_wait(AIC_USB_RESET_DELAY_US);

	/* Wait for the USB PHY PLL to lock before touching the core. */
	for (uint32_t i = 0; i < AIC_USB_PHY_PLL_LOCK_TMO_US; i++) {
		if (aic_rd(dev, AIC_USB_PHY_PLL_STS) & AIC_USB_PHY_PLL_LOCKED) {
			return 0;
		}
		k_busy_wait(1);
	}

	LOG_ERR("USB PHY PLL lock timeout, PLL_STS 0x%08x",
		aic_rd(dev, AIC_USB_PHY_PLL_STS));

	return -ETIMEDOUT;
}

/* ------------------------------------------------------------------ */
/* Core init                                                           */
/* ------------------------------------------------------------------ */

static void aic_fifo_setup(const struct device *dev)
{
	const struct udc_aic_config *cfg = dev->config;
	uint32_t addr;
	uint32_t depth;

	/*
	 * FIFO budget in 32-bit words. Conservative layout that fits the
	 * controller RAM and is large enough for one HS bulk packet per IN
	 * endpoint. RX FIFO must hold SETUP plus the largest OUT packet.
	 */
	aic_wr(dev, AIC_UDC_GRXFSIZ, 0x100U);		/* 256 words = 1 KiB */

	addr = 0x100U;
	depth = 0x80U;					/* 128 words = 512 B (EP0 IN) */
	aic_wr(dev, AIC_UDC_GNPTXFSIZ, (depth << 16) | addr);

	addr += depth;
	for (size_t n = 1; n < cfg->num_in_eps; n++) {
		depth = 0x80U;				/* 512 B per bulk/intr IN EP */
		aic_wr(dev, AIC_UDC_DIEPTXF_BASE + 4U * (n - 1U),
		       (depth << 16) | addr);
		addr += depth;
	}
}

static int aic_core_init(const struct device *dev)
{
	const struct udc_aic_config *cfg = dev->config;
	uint32_t devspd;
	uint32_t val;
	int ret;

	/* Soft disconnect until the device is enabled. */
	aic_set_bits(dev, AIC_UDC_DCTL, AIC_UDC_DCTL_SDIS);

	/* AIC init handshake (verbatim from reference port). */
	aic_clear_bits(dev, AIC_UDC_GRSTCTL, AIC_UDC_GRSTCTL_INIT_B9);

	ret = aic_poll_set(dev, AIC_UDC_GAHBCFG, AIC_UDC_GAHBCFG_READY);
	if (ret != 0) {
		LOG_ERR("Core not ready, GAHBCFG 0x%08x", aic_rd(dev, AIC_UDC_GAHBCFG));
		return ret;
	}

	/* Core soft reset (self-clearing). */
	aic_set_bits(dev, AIC_UDC_GRSTCTL, AIC_UDC_GRSTCTL_CSFTRST);
	ret = aic_poll_clear(dev, AIC_UDC_GRSTCTL, AIC_UDC_GRSTCTL_CSFTRST);
	if (ret != 0) {
		LOG_ERR("Core soft reset timeout, GRSTCTL 0x%08x",
			aic_rd(dev, AIC_UDC_GRSTCTL));
		return ret;
	}

	/* Select internal 16-bit UTMI+ HS PHY. */
	aic_wr(dev, AIC_UDC_GUSBCFG, AIC_UDC_GUSBCFG_HS_UTMI);

	/* Device speed. */
	if (cfg->speed_idx == 2) {
		devspd = AIC_UDC_DCFG_DEVSPD_HS;
	} else {
		devspd = AIC_UDC_DCFG_DEVSPD_FS;
	}
	val = aic_rd(dev, AIC_UDC_DCFG);
	val &= ~AIC_UDC_DCFG_DEVSPD_MASK;
	val |= devspd;
	val |= AIC_UDC_DCFG_NZSTSOUTHSHK;
	aic_wr(dev, AIC_UDC_DCFG, val);

	/* AIC init handshake stage 1. */
	val = aic_rd(dev, AIC_UDC_GRSTCTL);
	val = (val & ~AIC_UDC_GRSTCTL_INIT_MASK) | AIC_UDC_GRSTCTL_INIT_SET;
	aic_wr(dev, AIC_UDC_GRSTCTL, val);
	ret = aic_poll_clear(dev, AIC_UDC_GRSTCTL, AIC_UDC_GRSTCTL_INIT_B2);
	if (ret != 0) {
		LOG_ERR("Core init stage1 timeout, GRSTCTL 0x%08x",
			aic_rd(dev, AIC_UDC_GRSTCTL));
		return ret;
	}

	/* AIC init handshake stage 2. */
	aic_set_bits(dev, AIC_UDC_GRSTCTL, AIC_UDC_GRSTCTL_INIT_B1);
	ret = aic_poll_clear(dev, AIC_UDC_GRSTCTL, AIC_UDC_GRSTCTL_INIT_B1);
	if (ret != 0) {
		LOG_ERR("Core init stage2 timeout, GRSTCTL 0x%08x",
			aic_rd(dev, AIC_UDC_GRSTCTL));
		return ret;
	}

	/* Mask all endpoint interrupts initially. */
	aic_wr(dev, AIC_UDC_DIEPMSK, 0);
	aic_wr(dev, AIC_UDC_DOEPMSK, 0);
	aic_wr(dev, AIC_UDC_DAINTMSK, 0);

	aic_fifo_setup(dev);

	aic_set_bits(dev, AIC_UDC_GRSTCTL, AIC_UDC_GRSTCTL_INIT_B9);

	return 0;
}

/* ------------------------------------------------------------------ */
/* FIFO data movement (Completer/PIO mode)                             */
/* ------------------------------------------------------------------ */

static void aic_read_fifo(const struct device *dev, uint8_t ep_idx,
			  struct net_buf *buf, size_t bcnt)
{
	const struct udc_aic_config *cfg = dev->config;
	mem_addr_t fifo = cfg->base + AIC_UDC_DFIFO(ep_idx);
	size_t len = buf ? MIN(bcnt, net_buf_tailroom(buf)) : 0;
	size_t i;

	for (i = 0; i + 4U <= len; i += 4U) {
		net_buf_add_le32(buf, sys_read32(fifo));
	}

	if (i < len) {
		uint8_t r[4];

		sys_put_le32(sys_read32(fifo), r);
		for (size_t j = 0; j < (len - i); j++) {
			net_buf_add_u8(buf, r[j]);
		}
		i += 4U;
	}

	/* Drain any remaining bytes that did not fit the buffer. */
	for (; i < bcnt; i += 4U) {
		(void)sys_read32(fifo);
	}
}

static void aic_read_setup(const struct device *dev, uint8_t ep_idx, size_t bcnt)
{
	const struct udc_aic_config *cfg = dev->config;
	struct udc_aic_data *priv = udc_get_private(dev);
	mem_addr_t fifo = cfg->base + AIC_UDC_DFIFO(ep_idx);
	size_t off;

	/*
	 * A SETUP transaction always carries exactly 8 bytes (2 words). Read
	 * both words unconditionally instead of trusting GRXSTSP.BCNT so that
	 * the whole packet (including wLength) is refreshed even if the byte
	 * count is under-reported; otherwise a stale wLength from a previous
	 * SETUP would truncate the following control transfer.
	 */
	sys_put_le32(sys_read32(fifo), &priv->setup[0]);
	sys_put_le32(sys_read32(fifo), &priv->setup[4]);

	/* Drain anything reported beyond the 8 SETUP bytes. */
	for (off = 8U; off < bcnt; off += 4U) {
		(void)sys_read32(fifo);
	}
}

static int aic_write_fifo(const struct device *dev, uint8_t ep_idx,
			  const uint8_t *src, size_t len, uint16_t mps)
{
	const struct udc_aic_config *cfg = dev->config;
	mem_addr_t fifo = cfg->base + AIC_UDC_DFIFO(ep_idx);
	size_t off = 0;

	/*
	 * The IN TX FIFO on this core is only about one max-packet deep, so a
	 * multi-packet transfer cannot be loaded all at once and must be
	 * streamed packet by packet as the core drains it onto the bus.
	 *
	 * Critically, each packet must be written to the FIFO *in full* before
	 * the core may send it. If only part of a max-packet is present when an
	 * IN token arrives, the core transmits a short packet, which the host
	 * interprets as the end of the transfer and aborts it. Therefore wait
	 * for room for the whole current packet (a full MPS, or the final
	 * short remainder) before pushing it, never a partial max-packet.
	 */
	while (off < len) {
		size_t pkt = MIN((size_t)mps, len - off);
		size_t pkt_words = DIV_ROUND_UP(pkt, 4U);
		uint32_t space = 0;

		for (uint32_t i = 0; i < AIC_UDC_POLL_TMO_US; i++) {
			space = aic_rd(dev, AIC_UDC_DTXFSTS(ep_idx)) & 0xFFFFU;
			if (space >= pkt_words) {
				break;
			}
			k_busy_wait(1);
		}

		if (space < pkt_words) {
			return -ETIMEDOUT;
		}

		for (size_t w = 0; w < pkt_words; w++) {
			uint32_t val = src[off];

			if (off + 1U < len) {
				val |= ((uint32_t)src[off + 1U]) << 8;
			}
			if (off + 2U < len) {
				val |= ((uint32_t)src[off + 2U]) << 16;
			}
			if (off + 3U < len) {
				val |= ((uint32_t)src[off + 3U]) << 24;
			}
			sys_write32(val, fifo);
			off += 4U;
		}
	}

	return 0;
}

/* ------------------------------------------------------------------ */
/* Transfer arming                                                     */
/* ------------------------------------------------------------------ */

static void aic_prep_rx(const struct device *dev,
			struct udc_ep_config *const ep_cfg,
			struct net_buf *buf)
{
	struct udc_aic_data *priv = udc_get_private(dev);
	uint8_t ep_idx = USB_EP_GET_IDX(ep_cfg->addr);
	uint16_t mps = udc_mps_ep_size(ep_cfg);
	uint32_t xfersize = net_buf_tailroom(buf);
	uint32_t pktcnt;
	uint32_t doeptsiz;
	uint32_t doepctl;

	if (xfersize == 0U) {
		xfersize = mps;
	}

	if (ep_idx == 0U) {
		if (xfersize > AIC_UDC_DOEPTSIZ0_XFERSIZE_MASK) {
			xfersize = ROUND_DOWN(AIC_UDC_DOEPTSIZ0_XFERSIZE_MASK, mps);
		}
		pktcnt = 1U;
		doeptsiz = (3U << AIC_UDC_DOEPTSIZ0_SUPCNT_SHIFT) |
			   AIC_UDC_DOEPTSIZ0_PKTCNT | xfersize;
	} else {
		if (xfersize > AIC_UDC_DEPTSIZN_XFERSIZE_MASK) {
			xfersize = ROUND_DOWN(AIC_UDC_DEPTSIZN_XFERSIZE_MASK, mps);
		}
		pktcnt = DIV_ROUND_UP(xfersize, mps);
		doeptsiz = (pktcnt << AIC_UDC_DEPTSIZN_PKTCNT_SHIFT) | xfersize;
	}

	priv->rx_len[ep_idx] = xfersize;
	aic_wr(dev, AIC_UDC_DOEPTSIZ(ep_idx), doeptsiz);

	doepctl = aic_rd(dev, AIC_UDC_DOEPCTL(ep_idx));
	doepctl |= AIC_UDC_DEPCTL_EPENA | AIC_UDC_DEPCTL_CNAK;
	aic_wr(dev, AIC_UDC_DOEPCTL(ep_idx), doepctl);
}

static int aic_tx_start(const struct device *dev,
			struct udc_ep_config *const ep_cfg,
			struct net_buf *buf)
{
	struct udc_aic_data *priv = udc_get_private(dev);
	uint8_t ep_idx = USB_EP_GET_IDX(ep_cfg->addr);
	uint16_t mps = udc_mps_ep_size(ep_cfg);
	uint32_t len = buf->len;
	uint32_t pktcnt;
	uint32_t dieptsiz;
	uint32_t diepctl;
	int ret;

	if (len != 0U) {
		if (ep_idx == 0U) {
			if (len > AIC_UDC_DIEPTSIZ0_XFERSIZE_MASK) {
				len = ROUND_DOWN(AIC_UDC_DIEPTSIZ0_XFERSIZE_MASK, mps);
			}
		} else if (len > AIC_UDC_DEPTSIZN_XFERSIZE_MASK) {
			len = ROUND_DOWN(AIC_UDC_DEPTSIZN_XFERSIZE_MASK, mps);
		}
		pktcnt = DIV_ROUND_UP(len, mps);
	} else {
		pktcnt = 1U;
	}

	if (ep_idx == 0U) {
		dieptsiz = (pktcnt << AIC_UDC_DIEPTSIZ0_PKTCNT_SHIFT) | len;
	} else {
		dieptsiz = (pktcnt << AIC_UDC_DEPTSIZN_PKTCNT_SHIFT) | len;
	}

	priv->tx_len[ep_idx] = len;
	aic_wr(dev, AIC_UDC_DIEPTSIZ(ep_idx), dieptsiz);

	diepctl = aic_rd(dev, AIC_UDC_DIEPCTL(ep_idx));
	if (!(diepctl & AIC_UDC_DEPCTL_USBACTEP)) {
		return -ENOENT;
	}
	diepctl |= AIC_UDC_DEPCTL_EPENA | AIC_UDC_DEPCTL_CNAK;
	aic_wr(dev, AIC_UDC_DIEPCTL(ep_idx), diepctl);

	if (len != 0U) {
		ret = aic_write_fifo(dev, ep_idx, buf->data, len, mps);
		if (ret != 0) {
			return ret;
		}
	}

	return 0;
}

static void aic_xfer_start(const struct device *dev,
			   struct udc_ep_config *const ep_cfg)
{
	struct net_buf *buf;

	buf = udc_buf_peek(ep_cfg);
	if (buf == NULL) {
		return;
	}

	if (USB_EP_DIR_IS_OUT(ep_cfg->addr)) {
		struct udc_buf_info *bi = udc_get_buf_info(buf);

		if (bi->setup) {
			/* SETUP is delivered via RxFIFO without arming. */
			return;
		}
		aic_prep_rx(dev, ep_cfg, buf);
	} else {
		if (aic_tx_start(dev, ep_cfg, buf) != 0) {
			buf = udc_buf_get(ep_cfg);
			(void)udc_submit_ep_event(dev, buf, -ECONNREFUSED);
			return;
		}
	}

	udc_ep_set_busy(ep_cfg, true);
}

/* ------------------------------------------------------------------ */
/* Interrupt handling                                                  */
/* ------------------------------------------------------------------ */

static void aic_handle_rxflvl(const struct device *dev)
{
	struct udc_aic_data *priv = udc_get_private(dev);
	struct udc_ep_config *ep_cfg;
	struct net_buf *buf;
	uint32_t grxstsp;
	uint32_t pktsts;
	uint32_t bcnt;
	uint8_t ep;

	grxstsp = aic_rd(dev, AIC_UDC_GRXSTSP);
	ep = grxstsp & AIC_UDC_GRXSTSP_EPNUM_MASK;
	bcnt = (grxstsp & AIC_UDC_GRXSTSP_BCNT_MASK) >> AIC_UDC_GRXSTSP_BCNT_SHIFT;
	pktsts = (grxstsp & AIC_UDC_GRXSTSP_PKTSTS_MASK) >> AIC_UDC_GRXSTSP_PKTSTS_SHIFT;

	switch (pktsts) {
	case AIC_UDC_PKTSTS_SETUP_DATA:
		aic_read_setup(dev, ep, bcnt);
		break;
	case AIC_UDC_PKTSTS_OUT_DATA:
		ep_cfg = udc_get_ep_cfg(dev, ep);
		buf = udc_buf_peek(ep_cfg);
		if (buf != NULL && udc_get_buf_info(buf)->setup) {
			/* Never append OUT data to a pending SETUP buffer. */
			buf = NULL;
		}
		/* RxFIFO must be drained even when there is no buffer. */
		aic_read_fifo(dev, ep, buf, bcnt);
		if (ep < AIC_UDC_MAX_EPS) {
			priv->rx_zlp[ep] = (bcnt == 0U);
		}
		break;
	default:
		break;
	}
}

static void aic_handle_in_complete(const struct device *dev, uint8_t ep_idx)
{
	struct udc_aic_data *priv = udc_get_private(dev);
	struct udc_ep_config *ep_cfg = udc_get_ep_cfg(dev, ep_idx | USB_EP_DIR_IN);
	struct net_buf *buf;

	buf = udc_buf_peek(ep_cfg);
	if (buf == NULL) {
		return;
	}

	net_buf_pull(buf, priv->tx_len[ep_idx]);

	if (buf->len != 0U && aic_tx_start(dev, ep_cfg, buf) == 0) {
		return;
	}

	/* Bulk IN as well as control: a transfer whose length is a multiple of
	 * the max packet size is terminated by a zero-length packet.
	 */
	if (udc_ep_buf_has_zlp(buf)) {
		udc_ep_buf_clear_zlp(buf);
		if (aic_tx_start(dev, ep_cfg, buf) == 0) {
			return;
		}
	}

	buf = udc_buf_get(ep_cfg);
	udc_ep_set_busy(ep_cfg, false);
	(void)udc_submit_ep_event(dev, buf, 0);

	ep_cfg = udc_get_ep_cfg(dev, ep_idx | USB_EP_DIR_IN);
	aic_xfer_start(dev, ep_cfg);
}

static void aic_handle_iepint(const struct device *dev)
{
	uint32_t daint = aic_rd(dev, AIC_UDC_DAINT) & 0xFFFFU;

	while (daint) {
		uint8_t n = find_lsb_set(daint) - 1U;
		uint32_t diepint = aic_rd(dev, AIC_UDC_DIEPINT(n));

		aic_wr(dev, AIC_UDC_DIEPINT(n), diepint);

		if (diepint & AIC_UDC_DEPINT_XFERCOMPL) {
			aic_handle_in_complete(dev, n);
		}

		daint &= ~BIT(n);
	}

	aic_wr(dev, AIC_UDC_GINTSTS, AIC_UDC_GINTSTS_IEPINT);
}

static void aic_handle_out_complete(const struct device *dev, uint8_t ep_idx)
{
	struct udc_aic_data *priv = udc_get_private(dev);
	struct udc_ep_config *ep_cfg = udc_get_ep_cfg(dev, ep_idx);
	struct net_buf *buf;
	uint32_t doeptsiz;
	uint32_t rxsize;
	uint32_t bcnt;

	buf = udc_buf_peek(ep_cfg);
	if (buf == NULL) {
		return;
	}

	if (udc_get_buf_info(buf)->setup) {
		/* SETUP is delivered through the SETUP interrupt path only. */
		return;
	}

	doeptsiz = aic_rd(dev, AIC_UDC_DOEPTSIZ(ep_idx));
	if (ep_idx == 0U) {
		rxsize = doeptsiz & AIC_UDC_DOEPTSIZ0_XFERSIZE_MASK;
	} else {
		rxsize = doeptsiz & AIC_UDC_DEPTSIZN_XFERSIZE_MASK;
	}
	bcnt = priv->rx_len[ep_idx] - rxsize;

	/*
	 * A zero-length packet adds no bytes, so bcnt stays a multiple of the
	 * max packet size and the buffer still has room. That packet already
	 * ended the transfer; re-arming here waits forever.
	 */
	if (!priv->rx_zlp[ep_idx] && bcnt != 0U &&
	    (bcnt % udc_mps_ep_size(ep_cfg)) == 0U && net_buf_tailroom(buf)) {
		aic_prep_rx(dev, ep_cfg, buf);
		return;
	}
	priv->rx_zlp[ep_idx] = false;

	buf = udc_buf_get(ep_cfg);
	udc_ep_set_busy(ep_cfg, false);
	(void)udc_submit_ep_event(dev, buf, 0);

	ep_cfg = udc_get_ep_cfg(dev, ep_idx);
	aic_xfer_start(dev, ep_cfg);
}

static void aic_handle_oepint(const struct device *dev)
{
	struct udc_aic_data *priv = udc_get_private(dev);
	uint32_t daint = (aic_rd(dev, AIC_UDC_DAINT) >> 16) & 0xFFFFU;

	while (daint) {
		uint8_t n = find_lsb_set(daint) - 1U;
		uint32_t doepint = aic_rd(dev, AIC_UDC_DOEPINT(n));

		aic_wr(dev, AIC_UDC_DOEPINT(n), doepint);

		if (doepint & AIC_UDC_DOEPINT_STSPHSERCVD) {
			/* Allow IN status stage after a control-read transfer. */
			aic_set_bits(dev, AIC_UDC_DCTL, AIC_UDC_DCTL_CGNPINNAK);
		}

		if (doepint & AIC_UDC_DOEPINT_SETUP) {
			k_sem_give(&priv->setup_sem);
		} else if (doepint & AIC_UDC_DEPINT_XFERCOMPL) {
			aic_handle_out_complete(dev, n);
		}

		daint &= ~BIT(n);
	}

	aic_wr(dev, AIC_UDC_GINTSTS, AIC_UDC_GINTSTS_OEPINT);
}

static void aic_handle_reset(const struct device *dev)
{
	struct udc_aic_data *priv = udc_get_private(dev);

	/* A bus reset marks the start of a real host session. */
	priv->bus_reset_seen = true;

	/* Clear device address; SETUP is received unsolicited via the RxFIFO. */
	aic_clear_bits(dev, AIC_UDC_DCFG, AIC_UDC_DCFG_DEVADDR_MASK);

	aic_wr(dev, AIC_UDC_DAINTMSK, BIT(0) | BIT(16));
	aic_wr(dev, AIC_UDC_DOEPMSK,
	       AIC_UDC_DEPINT_XFERCOMPL | AIC_UDC_DOEPINT_SETUP |
	       AIC_UDC_DOEPINT_STSPHSERCVD);
	aic_wr(dev, AIC_UDC_DIEPMSK,
	       AIC_UDC_DEPINT_XFERCOMPL | AIC_UDC_DIEPINT_TIMEOUT);

	udc_submit_event(dev, UDC_EVT_RESET, 0);
}

static void aic_isr(const struct device *dev)
{
	uint32_t gintsts;
	uint32_t gintmsk;

	gintmsk = aic_rd(dev, AIC_UDC_GINTMSK);
	gintsts = aic_rd(dev, AIC_UDC_GINTSTS) & gintmsk;

	if (gintsts & AIC_UDC_GINTSTS_USBRST) {
		aic_wr(dev, AIC_UDC_GINTSTS, AIC_UDC_GINTSTS_USBRST);
		aic_handle_reset(dev);
	}

	if (gintsts & AIC_UDC_GINTSTS_ENUMDONE) {
		aic_wr(dev, AIC_UDC_GINTSTS, AIC_UDC_GINTSTS_ENUMDONE);
	}

	if (gintsts & AIC_UDC_GINTSTS_RXFLVL) {
		aic_handle_rxflvl(dev);
	}

	if (gintsts & AIC_UDC_GINTSTS_IEPINT) {
		aic_handle_iepint(dev);
	}

	if (gintsts & AIC_UDC_GINTSTS_OEPINT) {
		aic_handle_oepint(dev);
	}

	if (gintsts & AIC_UDC_GINTSTS_USBSUSP) {
		struct udc_aic_data *priv = udc_get_private(dev);

		aic_wr(dev, AIC_UDC_GINTSTS, AIC_UDC_GINTSTS_USBSUSP);
		/*
		 * Ignore suspend before the first bus reset: it only reflects an
		 * idle, not-yet-enumerated bus and would otherwise leave the
		 * stack suspended and rejecting IN transfers.
		 */
		if (priv->bus_reset_seen && !udc_is_suspended(dev)) {
			udc_set_suspended(dev, true);
			udc_submit_event(dev, UDC_EVT_SUSPEND, 0);
		}
	}

	if (gintsts & AIC_UDC_GINTSTS_WKUPINT) {
		aic_wr(dev, AIC_UDC_GINTSTS, AIC_UDC_GINTSTS_WKUPINT);
		if (udc_is_suspended(dev)) {
			udc_set_suspended(dev, false);
			udc_submit_event(dev, UDC_EVT_RESUME, 0);
		}
	}
}

/* ------------------------------------------------------------------ */
/* Driver thread (control SETUP processing)                            */
/* ------------------------------------------------------------------ */

static void aic_thread(void *arg1, void *arg2, void *arg3)
{
	const struct device *dev = arg1;
	struct udc_aic_data *priv = udc_get_private(dev);

	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (true) {
		k_sem_take(&priv->setup_sem, K_FOREVER);
		udc_setup_received(dev, priv->setup);
	}
}

/* ------------------------------------------------------------------ */
/* UDC API                                                             */
/* ------------------------------------------------------------------ */

static int udc_aic_ep_enqueue(const struct device *dev,
			      struct udc_ep_config *const ep_cfg,
			      struct net_buf *buf)
{
	struct udc_aic_data *priv = udc_get_private(dev);
	k_spinlock_key_t key;

	udc_buf_put(ep_cfg, buf);

	if (ep_cfg->stat.halted) {
		return 0;
	}

	key = k_spin_lock(&priv->lock);
	if (!udc_ep_is_busy(ep_cfg)) {
		aic_xfer_start(dev, ep_cfg);
	}
	k_spin_unlock(&priv->lock, key);

	return 0;
}

static int udc_aic_ep_dequeue(const struct device *dev,
			      struct udc_ep_config *const ep_cfg)
{
	unsigned int lock_key;

	lock_key = irq_lock();
	udc_ep_cancel_queued(dev, ep_cfg);
	udc_ep_set_busy(ep_cfg, false);
	irq_unlock(lock_key);

	return 0;
}

static int udc_aic_ep_enable(const struct device *dev,
			     struct udc_ep_config *const ep_cfg)
{
	uint8_t ep_idx = USB_EP_GET_IDX(ep_cfg->addr);
	uint16_t mps = udc_mps_ep_size(ep_cfg);
	uint32_t eptype;
	uint32_t depctl;

	switch (ep_cfg->attributes & USB_EP_TRANSFER_TYPE_MASK) {
	case USB_EP_TYPE_CONTROL:
		eptype = AIC_UDC_EPTYPE_CTRL;
		break;
	case USB_EP_TYPE_BULK:
		eptype = AIC_UDC_EPTYPE_BULK;
		break;
	case USB_EP_TYPE_INTERRUPT:
		eptype = AIC_UDC_EPTYPE_INTR;
		break;
	case USB_EP_TYPE_ISO:
		eptype = AIC_UDC_EPTYPE_ISO;
		break;
	default:
		return -EINVAL;
	}

	if (USB_EP_DIR_IS_IN(ep_cfg->addr)) {
		depctl = AIC_UDC_DEPCTL_USBACTEP | AIC_UDC_DEPCTL_SNAK |
			 AIC_UDC_DEPCTL_SETD0PID |
			 (eptype << AIC_UDC_DEPCTL_EPTYPE_SHIFT) |
			 ((uint32_t)ep_idx << AIC_UDC_DEPCTL_TXFNUM_SHIFT);
		if (ep_idx == 0U) {
			depctl |= AIC_UDC_DEPCTL_MPS0_64;
		} else {
			depctl |= (mps & AIC_UDC_DEPCTL_MPS_MASK);
		}
		aic_wr(dev, AIC_UDC_DIEPCTL(ep_idx), depctl);
		aic_set_bits(dev, AIC_UDC_DAINTMSK, BIT(ep_idx));
	} else {
		depctl = AIC_UDC_DEPCTL_USBACTEP | AIC_UDC_DEPCTL_SNAK |
			 AIC_UDC_DEPCTL_SETD0PID |
			 (eptype << AIC_UDC_DEPCTL_EPTYPE_SHIFT);
		if (ep_idx == 0U) {
			depctl |= AIC_UDC_DEPCTL_MPS0_64;
		} else {
			depctl |= (mps & AIC_UDC_DEPCTL_MPS_MASK);
		}
		aic_wr(dev, AIC_UDC_DOEPCTL(ep_idx), depctl);
		aic_set_bits(dev, AIC_UDC_DAINTMSK, BIT(16U + ep_idx));
	}

	LOG_DBG("Enable ep 0x%02x", ep_cfg->addr);

	return 0;
}

static int udc_aic_ep_disable(const struct device *dev,
			      struct udc_ep_config *const ep_cfg)
{
	uint8_t ep_idx = USB_EP_GET_IDX(ep_cfg->addr);

	if (USB_EP_DIR_IS_IN(ep_cfg->addr)) {
		aic_clear_bits(dev, AIC_UDC_DIEPCTL(ep_idx), AIC_UDC_DEPCTL_USBACTEP);
		aic_clear_bits(dev, AIC_UDC_DAINTMSK, BIT(ep_idx));
	} else {
		aic_clear_bits(dev, AIC_UDC_DOEPCTL(ep_idx), AIC_UDC_DEPCTL_USBACTEP);
		aic_clear_bits(dev, AIC_UDC_DAINTMSK, BIT(16U + ep_idx));
	}

	LOG_DBG("Disable ep 0x%02x", ep_cfg->addr);

	return 0;
}

static int udc_aic_ep_set_halt(const struct device *dev,
			       struct udc_ep_config *const ep_cfg)
{
	uint8_t ep_idx = USB_EP_GET_IDX(ep_cfg->addr);
	uint32_t reg = USB_EP_DIR_IS_IN(ep_cfg->addr) ?
		       AIC_UDC_DIEPCTL(ep_idx) : AIC_UDC_DOEPCTL(ep_idx);

	aic_set_bits(dev, reg, AIC_UDC_DEPCTL_STALL);

	if (ep_idx != 0U) {
		ep_cfg->stat.halted = true;
	}

	return 0;
}

static int udc_aic_ep_clear_halt(const struct device *dev,
				 struct udc_ep_config *const ep_cfg)
{
	uint8_t ep_idx = USB_EP_GET_IDX(ep_cfg->addr);
	uint32_t reg = USB_EP_DIR_IS_IN(ep_cfg->addr) ?
		       AIC_UDC_DIEPCTL(ep_idx) : AIC_UDC_DOEPCTL(ep_idx);
	struct udc_aic_data *priv = udc_get_private(dev);
	k_spinlock_key_t key;

	aic_clear_bits(dev, reg, AIC_UDC_DEPCTL_STALL);
	aic_set_bits(dev, reg, AIC_UDC_DEPCTL_SETD0PID);
	ep_cfg->stat.halted = false;

	key = k_spin_lock(&priv->lock);
	if (!udc_ep_is_busy(ep_cfg)) {
		aic_xfer_start(dev, ep_cfg);
	}
	k_spin_unlock(&priv->lock, key);

	return 0;
}

static int udc_aic_set_address(const struct device *dev, const uint8_t addr)
{
	uint32_t dcfg = aic_rd(dev, AIC_UDC_DCFG);

	dcfg &= ~AIC_UDC_DCFG_DEVADDR_MASK;
	dcfg |= ((uint32_t)addr << AIC_UDC_DCFG_DEVADDR_SHIFT) &
		AIC_UDC_DCFG_DEVADDR_MASK;
	aic_wr(dev, AIC_UDC_DCFG, dcfg);

	return 0;
}

static int udc_aic_host_wakeup(const struct device *dev)
{
	aic_set_bits(dev, AIC_UDC_DCTL, AIC_UDC_DCTL_RWUSIG);
	k_msleep(2);
	aic_clear_bits(dev, AIC_UDC_DCTL, AIC_UDC_DCTL_RWUSIG);

	return 0;
}

static enum udc_bus_speed udc_aic_device_speed(const struct device *dev)
{
	uint32_t dsts = aic_rd(dev, AIC_UDC_DSTS);
	uint32_t spd = (dsts & AIC_UDC_DSTS_ENUMSPD_MASK) >> AIC_UDC_DSTS_ENUMSPD_SHIFT;

	return (spd == AIC_UDC_DSTS_ENUMSPD_HS) ? UDC_BUS_SPEED_HS : UDC_BUS_SPEED_FS;
}

static int udc_aic_enable(const struct device *dev)
{
	const struct udc_aic_config *cfg = dev->config;
	struct udc_aic_data *priv = udc_get_private(dev);

	priv->bus_reset_seen = false;

	if (udc_ep_enable_internal(dev, USB_CONTROL_EP_OUT,
				   USB_EP_TYPE_CONTROL, 64, 0)) {
		LOG_ERR("Failed to enable control OUT endpoint");
		return -EIO;
	}

	if (udc_ep_enable_internal(dev, USB_CONTROL_EP_IN,
				   USB_EP_TYPE_CONTROL, 64, 0)) {
		LOG_ERR("Failed to enable control IN endpoint");
		return -EIO;
	}

	/* Unmask core interrupts and turn on global interrupt generation. */
	aic_wr(dev, AIC_UDC_GINTSTS, 0xFFFFFFFFU);
	aic_wr(dev, AIC_UDC_GINTMSK,
	       AIC_UDC_GINTSTS_RXFLVL | AIC_UDC_GINTSTS_USBRST |
	       AIC_UDC_GINTSTS_ENUMDONE | AIC_UDC_GINTSTS_IEPINT |
	       AIC_UDC_GINTSTS_OEPINT | AIC_UDC_GINTSTS_USBSUSP |
	       AIC_UDC_GINTSTS_WKUPINT);
	aic_set_bits(dev, AIC_UDC_GAHBCFG, AIC_UDC_GAHBCFG_GLBL_INTR_EN);

	cfg->irq_enable_func(dev);

	/* Soft connect: signal attachment to the host. */
	aic_clear_bits(dev, AIC_UDC_DCTL, AIC_UDC_DCTL_SDIS);

	LOG_INF("Enable device");

	return 0;
}

static int udc_aic_disable(const struct device *dev)
{
	const struct udc_aic_config *cfg = dev->config;

	aic_set_bits(dev, AIC_UDC_DCTL, AIC_UDC_DCTL_SDIS);
	aic_clear_bits(dev, AIC_UDC_GAHBCFG, AIC_UDC_GAHBCFG_GLBL_INTR_EN);
	cfg->irq_disable_func(dev);

	if (udc_ep_disable_internal(dev, USB_CONTROL_EP_OUT)) {
		LOG_ERR("Failed to disable control OUT endpoint");
	}

	if (udc_ep_disable_internal(dev, USB_CONTROL_EP_IN)) {
		LOG_ERR("Failed to disable control IN endpoint");
	}

	LOG_INF("Disable device");

	return 0;
}

static int udc_aic_init(const struct device *dev)
{
	int ret;

	aic_phy_power(dev);

	ret = aic_phy_clock_reset(dev);
	if (ret != 0) {
		return ret;
	}

	ret = aic_core_init(dev);
	if (ret != 0) {
		return ret;
	}

	return 0;
}

static int udc_aic_shutdown(const struct device *dev)
{
	aic_set_bits(dev, AIC_UDC_DCTL, AIC_UDC_DCTL_SDIS);

	return 0;
}

static void udc_aic_lock(const struct device *dev)
{
	udc_lock_internal(dev, K_FOREVER);
}

static void udc_aic_unlock(const struct device *dev)
{
	udc_unlock_internal(dev);
}

static int udc_aic_driver_preinit(const struct device *dev)
{
	const struct udc_aic_config *cfg = dev->config;
	struct udc_aic_data *priv = udc_get_private(dev);
	struct udc_data *data = dev->data;
	uint16_t mps = 64;
	int err;

	k_mutex_init(&data->mutex);
	k_sem_init(&priv->setup_sem, 0, 10);

	data->caps.rwup = true;
	/*
	 * This is a DWC2 core: DCFG.DevAddr must be programmed while handling
	 * the SET_ADDRESS SETUP (before the status stage). The hardware sends
	 * the status ZLP from address 0 and switches to the new address only
	 * after the control transfer completes. Deferring set_address until
	 * after the status stage leaves the device unreachable at the new
	 * address, so the host endlessly re-enumerates.
	 */
	data->caps.addr_before_status = true;
	data->caps.mps0 = UDC_MPS0_64;
	if (cfg->speed_idx == 2) {
		data->caps.hs = true;
		mps = 512;
	}

	for (size_t i = 0; i < cfg->num_out_eps; i++) {
		cfg->ep_cfg_out[i].caps.out = 1;
		if (i == 0) {
			cfg->ep_cfg_out[i].caps.control = 1;
			cfg->ep_cfg_out[i].caps.mps = 64;
		} else {
			cfg->ep_cfg_out[i].caps.bulk = 1;
			cfg->ep_cfg_out[i].caps.interrupt = 1;
			cfg->ep_cfg_out[i].caps.mps = mps;
		}

		cfg->ep_cfg_out[i].addr = USB_EP_DIR_OUT | i;
		err = udc_register_ep(dev, &cfg->ep_cfg_out[i]);
		if (err != 0) {
			LOG_ERR("Failed to register endpoint");
			return err;
		}
	}

	for (size_t i = 0; i < cfg->num_in_eps; i++) {
		cfg->ep_cfg_in[i].caps.in = 1;
		if (i == 0) {
			cfg->ep_cfg_in[i].caps.control = 1;
			cfg->ep_cfg_in[i].caps.mps = 64;
		} else {
			cfg->ep_cfg_in[i].caps.bulk = 1;
			cfg->ep_cfg_in[i].caps.interrupt = 1;
			cfg->ep_cfg_in[i].caps.mps = mps;
		}

		cfg->ep_cfg_in[i].addr = USB_EP_DIR_IN | i;
		err = udc_register_ep(dev, &cfg->ep_cfg_in[i]);
		if (err != 0) {
			LOG_ERR("Failed to register endpoint");
			return err;
		}
	}

	k_thread_create(&priv->thread_data, cfg->thread_stk, cfg->thread_stk_sz,
			aic_thread, (void *)dev, NULL, NULL,
			K_PRIO_COOP(CONFIG_UDC_AIC_THREAD_PRIORITY),
			K_ESSENTIAL, K_NO_WAIT);
	k_thread_name_set(&priv->thread_data, dev->name);

	LOG_INF("Device %p (max. speed %d)", dev, cfg->speed_idx);

	return 0;
}

static const struct udc_api udc_aic_api = {
	.lock = udc_aic_lock,
	.unlock = udc_aic_unlock,
	.device_speed = udc_aic_device_speed,
	.init = udc_aic_init,
	.enable = udc_aic_enable,
	.disable = udc_aic_disable,
	.shutdown = udc_aic_shutdown,
	.set_address = udc_aic_set_address,
	.host_wakeup = udc_aic_host_wakeup,
	.ep_enable = udc_aic_ep_enable,
	.ep_disable = udc_aic_ep_disable,
	.ep_set_halt = udc_aic_ep_set_halt,
	.ep_clear_halt = udc_aic_ep_clear_halt,
	.ep_enqueue = udc_aic_ep_enqueue,
	.ep_dequeue = udc_aic_ep_dequeue,
};

#define UDC_AIC_DEVICE_DEFINE(n)						\
	K_THREAD_STACK_DEFINE(udc_aic_stack_##n, CONFIG_UDC_AIC_STACK_SIZE);	\
										\
	static void udc_aic_irq_enable_##n(const struct device *dev)		\
	{									\
		ARG_UNUSED(dev);						\
		IRQ_CONNECT(DT_INST_IRQN(n), DT_INST_IRQ(n, priority),		\
			    aic_isr, DEVICE_DT_INST_GET(n), 0);			\
		irq_enable(DT_INST_IRQN(n));					\
	}									\
										\
	static void udc_aic_irq_disable_##n(const struct device *dev)		\
	{									\
		ARG_UNUSED(dev);						\
		irq_disable(DT_INST_IRQN(n));					\
	}									\
										\
	static struct udc_ep_config						\
		udc_aic_ep_cfg_out_##n[DT_INST_PROP(n, num_out_eps)];		\
	static struct udc_ep_config						\
		udc_aic_ep_cfg_in_##n[DT_INST_PROP(n, num_in_eps)];		\
										\
	static const struct udc_aic_config udc_aic_config_##n = {		\
		.base = DT_INST_REG_ADDR(n),					\
		.syscfg_base = DT_REG_ADDR(DT_PHANDLE(DT_DRV_INST(n), artinchip_syscfg)),\
		.clk_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR_BY_NAME(n, device)),\
		.phy_clk = (clock_control_subsys_t)				\
			DT_INST_CLOCKS_CELL_BY_NAME(n, phy, clkid),		\
		.dev_clk = (clock_control_subsys_t)				\
			DT_INST_CLOCKS_CELL_BY_NAME(n, device, clkid),		\
		.phy_reset = RESET_DT_SPEC_INST_GET_BY_IDX(n, 0),		\
		.dev_reset = RESET_DT_SPEC_INST_GET_BY_IDX(n, 1),		\
		.num_in_eps = DT_INST_PROP(n, num_in_eps),			\
		.num_out_eps = DT_INST_PROP(n, num_out_eps),			\
		.ep_cfg_in = udc_aic_ep_cfg_in_##n,				\
		.ep_cfg_out = udc_aic_ep_cfg_out_##n,				\
		.irq_enable_func = udc_aic_irq_enable_##n,			\
		.irq_disable_func = udc_aic_irq_disable_##n,			\
		.thread_stk = udc_aic_stack_##n,				\
		.thread_stk_sz = K_THREAD_STACK_SIZEOF(udc_aic_stack_##n),	\
		.speed_idx = DT_ENUM_IDX_OR(DT_DRV_INST(n), maximum_speed, 1),	\
	};									\
										\
	static struct udc_aic_data udc_aic_priv_##n;				\
										\
	static struct udc_data udc_data_##n = {					\
		.mutex = Z_MUTEX_INITIALIZER(udc_data_##n.mutex),		\
		.priv = &udc_aic_priv_##n,					\
	};									\
										\
	DEVICE_DT_INST_DEFINE(n, udc_aic_driver_preinit, NULL,			\
			      &udc_data_##n, &udc_aic_config_##n,		\
			      POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE,	\
			      &udc_aic_api);

DT_INST_FOREACH_STATUS_OKAY(UDC_AIC_DEVICE_DEFINE)
