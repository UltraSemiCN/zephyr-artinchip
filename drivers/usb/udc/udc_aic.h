/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Register map and bit definitions for the ArtInChip USB 2.0 device
 * controller (vendor block "ArtInChip USBD").
 *
 * The controller is a Synopsys DWC2-derived device-only core, but it uses the
 * legacy "device subset" register layout, which is NOT compatible with the
 * modern snps,dwc2 map used by drivers/usb/udc/udc_dwc2.c:
 *
 *   - The OTG/host global registers are dropped and the remaining global
 *     registers are shifted (GAHBCFG@0x000, GRSTCTL-equivalent@0x004,
 *     GUSBCFG@0x008, GINTSTS@0x010, GINTMSK@0x014, GRXFSIZ@0x018,
 *     GRXSTSP@0x01C, GNPTXFSIZ@0x020).
 *   - The device control block lives at 0x200 (not 0x800).
 *   - Per-endpoint registers are organised as struct-of-arrays
 *     (e.g. DIEPCTL[n] = 0x220 + 4*n) instead of array-of-structs.
 *
 * The individual register *bit fields* are standard Synopsys DWC2 device-mode
 * semantics; only the register *offsets* differ. The offsets below were
 * recovered from the luban-lite reference port (AIC_UDC_RegDef) and verified
 * against the live controller during bring-up.
 */

#ifndef ZEPHYR_DRIVERS_USB_UDC_AIC_H
#define ZEPHYR_DRIVERS_USB_UDC_AIC_H

#include <zephyr/sys/util.h>

/* ----- Global registers (device subset) ----- */
#define AIC_UDC_GAHBCFG		0x000U	/* ahbbasic */
#define AIC_UDC_GRSTCTL		0x004U	/* usbdevinit: core soft reset + AIC init */
#define AIC_UDC_GUSBCFG		0x008U	/* usbphyif */
#define AIC_UDC_GUSBCFG2	0x00CU	/* usbulpiphy */
#define AIC_UDC_GINTSTS		0x010U	/* usbintsts */
#define AIC_UDC_GINTMSK		0x014U	/* usbintmsk */
#define AIC_UDC_GRXFSIZ		0x018U	/* rxfifosiz */
#define AIC_UDC_GRXSTSP		0x01CU	/* rxfifosts_pop */
#define AIC_UDC_GNPTXFSIZ	0x020U	/* ietxfifosiz (EP0/non-periodic TX FIFO) */
#define AIC_UDC_GRXSTSR		0x030U	/* rxfifosts_dbg (read-only peek) */
#define AIC_UDC_PHYCLKCTL	0x040U	/* phyclkctl */
#define AIC_UDC_DIEPTXF_BASE	0x060U	/* txfifosiz: DIEPTXF(n) = base + 4*(n-1) */

/* ----- Device control block ----- */
#define AIC_UDC_DCFG		0x200U	/* usbdevconf */
#define AIC_UDC_DCTL		0x204U	/* usbdevfunc */
#define AIC_UDC_DSTS		0x208U	/* usblinests */
#define AIC_UDC_DIEPMSK		0x20CU	/* inepintmsk */
#define AIC_UDC_DOEPMSK		0x210U	/* outepintmsk */
#define AIC_UDC_DAINT		0x214U	/* usbepint */
#define AIC_UDC_DAINTMSK	0x218U	/* usbepintmsk */

/* ----- Per-endpoint register arrays (index n, stride 4) ----- */
#define AIC_UDC_DIEPCTL(n)	(0x220U + 4U * (n))
#define AIC_UDC_DOEPCTL(n)	(0x260U + 4U * (n))
#define AIC_UDC_DIEPINT(n)	(0x2A0U + 4U * (n))
#define AIC_UDC_DOEPINT(n)	(0x2E0U + 4U * (n))
#define AIC_UDC_DIEPTSIZ(n)	(0x320U + 4U * (n))
#define AIC_UDC_DOEPTSIZ(n)	(0x360U + 4U * (n))
#define AIC_UDC_DIEPDMA(n)	(0x3A0U + 4U * (n))
#define AIC_UDC_DOEPDMA(n)	(0x3E0U + 4U * (n))
#define AIC_UDC_DTXFSTS(n)	(0x420U + 4U * (n))

/* Data FIFO push/pop window for endpoint n (Synopsys standard placement). */
#define AIC_UDC_DFIFO(n)	(0x1000U * ((n) + 1U))

#define AIC_UDC_MAX_EPS		16U

/* ----- GAHBCFG (ahbbasic) ----- */
#define AIC_UDC_GAHBCFG_GLBL_INTR_EN	BIT(0)

/*
 * GRSTCTL (usbdevinit). bit0 is the self-clearing core soft reset. The
 * remaining bits are an AIC-specific init handshake reproduced verbatim from
 * the reference port; see udc_aic.c aic_core_soft_reset()/aic_core_init().
 */
#define AIC_UDC_GRSTCTL_CSFTRST		BIT(0)
#define AIC_UDC_GRSTCTL_INIT_B1		BIT(1)
#define AIC_UDC_GRSTCTL_INIT_B2		BIT(2)
#define AIC_UDC_GRSTCTL_INIT_B7		BIT(7)
#define AIC_UDC_GRSTCTL_INIT_B9		BIT(9)
#define AIC_UDC_GRSTCTL_INIT_MASK	GENMASK(7, 3)
#define AIC_UDC_GRSTCTL_INIT_SET	(BIT(7) | BIT(2))

/* ahbbasic readiness bit polled before/after the core soft reset. */
#define AIC_UDC_GAHBCFG_READY		BIT(2)

/* ----- GUSBCFG (usbphyif): 16-bit UTMI+ HS PHY configuration ----- */
#define AIC_UDC_GUSBCFG_HS_UTMI		0x00001407U

/* ----- GINTSTS / GINTMSK (standard DWC2 bit positions) ----- */
#define AIC_UDC_GINTSTS_SOF		BIT(3)
#define AIC_UDC_GINTSTS_RXFLVL		BIT(4)
#define AIC_UDC_GINTSTS_USBSUSP		BIT(11)
#define AIC_UDC_GINTSTS_USBRST		BIT(12)
#define AIC_UDC_GINTSTS_ENUMDONE	BIT(13)
#define AIC_UDC_GINTSTS_IEPINT		BIT(18)
#define AIC_UDC_GINTSTS_OEPINT		BIT(19)
#define AIC_UDC_GINTSTS_WKUPINT		BIT(31)

/* ----- GRXSTSP ----- */
#define AIC_UDC_GRXSTSP_EPNUM_MASK	GENMASK(3, 0)
#define AIC_UDC_GRXSTSP_BCNT_MASK	GENMASK(14, 4)
#define AIC_UDC_GRXSTSP_BCNT_SHIFT	4U
#define AIC_UDC_GRXSTSP_PKTSTS_MASK	GENMASK(20, 17)
#define AIC_UDC_GRXSTSP_PKTSTS_SHIFT	17U

#define AIC_UDC_PKTSTS_OUT_NAK		1U
#define AIC_UDC_PKTSTS_OUT_DATA		2U
#define AIC_UDC_PKTSTS_OUT_DATA_DONE	3U
#define AIC_UDC_PKTSTS_SETUP_DONE	4U
#define AIC_UDC_PKTSTS_SETUP_DATA	6U

/* ----- DCFG ----- */
#define AIC_UDC_DCFG_DEVSPD_MASK	GENMASK(1, 0)
#define AIC_UDC_DCFG_DEVSPD_HS		0U
#define AIC_UDC_DCFG_DEVSPD_FS		1U
#define AIC_UDC_DCFG_NZSTSOUTHSHK	BIT(2)
#define AIC_UDC_DCFG_DEVADDR_MASK	GENMASK(10, 4)
#define AIC_UDC_DCFG_DEVADDR_SHIFT	4U

/* ----- DCTL ----- */
#define AIC_UDC_DCTL_RWUSIG		BIT(0)
#define AIC_UDC_DCTL_SDIS		BIT(1)
#define AIC_UDC_DCTL_CGNPINNAK		BIT(8)
#define AIC_UDC_DCTL_CGOUTNAK		BIT(10)

/* ----- DSTS ----- */
#define AIC_UDC_DSTS_SUSPSTS		BIT(0)
#define AIC_UDC_DSTS_ENUMSPD_MASK	GENMASK(2, 1)
#define AIC_UDC_DSTS_ENUMSPD_SHIFT	1U
#define AIC_UDC_DSTS_ENUMSPD_HS		0U
#define AIC_UDC_DSTS_ENUMSPD_FS		1U

/* ----- DIEPMSK / DOEPMSK / DIEPINT / DOEPINT ----- */
#define AIC_UDC_DEPINT_XFERCOMPL	BIT(0)
#define AIC_UDC_DEPINT_EPDISBLD		BIT(1)
#define AIC_UDC_DIEPINT_TIMEOUT		BIT(3)
#define AIC_UDC_DOEPINT_SETUP		BIT(3)
#define AIC_UDC_DOEPINT_STSPHSERCVD	BIT(5)
#define AIC_UDC_DIEPINT_INEPNAKEFF	BIT(6)

/* ----- DEPCTL (DIEPCTL/DOEPCTL) ----- */
#define AIC_UDC_DEPCTL_MPS_MASK		GENMASK(10, 0)
#define AIC_UDC_DEPCTL_MPS0_MASK	GENMASK(1, 0)
#define AIC_UDC_DEPCTL_MPS0_64		0U
#define AIC_UDC_DEPCTL_MPS0_32		1U
#define AIC_UDC_DEPCTL_MPS0_16		2U
#define AIC_UDC_DEPCTL_MPS0_8		3U
#define AIC_UDC_DEPCTL_USBACTEP		BIT(15)
#define AIC_UDC_DEPCTL_NAKSTS		BIT(17)
#define AIC_UDC_DEPCTL_EPTYPE_MASK	GENMASK(19, 18)
#define AIC_UDC_DEPCTL_EPTYPE_SHIFT	18U
#define AIC_UDC_DEPCTL_STALL		BIT(21)
#define AIC_UDC_DEPCTL_TXFNUM_SHIFT	22U
#define AIC_UDC_DEPCTL_TXFNUM_MASK	GENMASK(25, 22)
#define AIC_UDC_DEPCTL_CNAK		BIT(26)
#define AIC_UDC_DEPCTL_SNAK		BIT(27)
#define AIC_UDC_DEPCTL_SETD0PID		BIT(28)
#define AIC_UDC_DEPCTL_SETD1PID		BIT(29)
#define AIC_UDC_DEPCTL_EPDIS		BIT(30)
#define AIC_UDC_DEPCTL_EPENA		BIT(31)

#define AIC_UDC_EPTYPE_CTRL		0U
#define AIC_UDC_EPTYPE_ISO		1U
#define AIC_UDC_EPTYPE_BULK		2U
#define AIC_UDC_EPTYPE_INTR		3U

/* ----- DIEPTSIZ / DOEPTSIZ ----- */
#define AIC_UDC_DIEPTSIZ0_XFERSIZE_MASK	GENMASK(6, 0)
#define AIC_UDC_DIEPTSIZ0_PKTCNT_SHIFT	19U
#define AIC_UDC_DIEPTSIZ0_PKTCNT_MASK	GENMASK(20, 19)

#define AIC_UDC_DOEPTSIZ0_XFERSIZE_MASK	GENMASK(6, 0)
#define AIC_UDC_DOEPTSIZ0_PKTCNT	BIT(19)
#define AIC_UDC_DOEPTSIZ0_SUPCNT_SHIFT	29U
#define AIC_UDC_DOEPTSIZ0_SUPCNT_MASK	GENMASK(30, 29)

#define AIC_UDC_DEPTSIZN_XFERSIZE_MASK	GENMASK(18, 0)
#define AIC_UDC_DEPTSIZN_PKTCNT_SHIFT	19U
#define AIC_UDC_DEPTSIZN_PKTCNT_MASK	GENMASK(28, 19)

/* ----- SYSCFG / USB PHY bring-up (shared with quirk header) ----- */
#define AIC_SYSCFG_USB0_CFG		0x40CU
#define AIC_SYSCFG_USB0_CFG_DRD_MODE	BIT(0)
#define AIC_SYSCFG_USB_PLL_COM		0x400U
#define AIC_SYSCFG_USB_PLL_GEN_EN	BIT(16)
#define AIC_SYSCFG_USB0_REXT		0x048U
#define AIC_SYSCFG_USB0_REXT_CAL_EN	BIT(8)
#define AIC_SYSCFG_USB0_REXT_CAL_VAL	0x60U
#define AIC_SYSCFG_USB0_REXT_CAL_MSK	0xFFU

/* USB PHY PLL lock status inside the controller window (usb_base + 0x8078). */
#define AIC_USB_PHY_PLL_STS		0x8078U
#define AIC_USB_PHY_PLL_LOCKED		BIT(23)
#define AIC_USB_PHY_PLL_LOCK_TMO_US	20000U
#define AIC_USB_RESET_DELAY_US		300U

#endif /* ZEPHYR_DRIVERS_USB_UDC_AIC_H */
