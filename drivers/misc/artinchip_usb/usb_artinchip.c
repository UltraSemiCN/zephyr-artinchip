/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT artinchip_d13x_usb

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#if defined(CONFIG_ARTINCHIP_USB_SHELL)
#include <zephyr/shell/shell.h>
#endif

#define AIC_USB_CLK_COUNT 3U
#define AIC_USB_RESET_COUNT 3U

#define AIC_USB_EHCI_CAPLENGTH_HCIVERSION 0x00U
#define AIC_USB_EHCI_HCSPARAMS            0x04U
#define AIC_USB_EHCI_HCCPARAMS            0x08U
#define AIC_USB_OHCI_HCREVISION           0x00U
#define AIC_USB_HOST_PHY_IF               0x800U
#define AIC_USB_HOST_AHB2STBUS_INSREG01   0x94U
#define AIC_USB_HOST_PHY_IF_UTMI          (BIT(0) | (0xEU << 8))
#define AIC_USB_HOST_FIFO_THRESHOLD       (32U | (127U << 16))
#define AIC_SYSCFG_USB0_CFG               0x40CU
#define AIC_SYSCFG_USB0_CFG_DRD_MODE      BIT(0)
#define AIC_USB_RESET_DELAY_US            300U

enum artinchip_usb_role {
	AIC_USB_ROLE_HOST,
	AIC_USB_ROLE_DEVICE,
	AIC_USB_ROLE_OTG,
};

enum artinchip_usb_block {
	AIC_USB_DEVICE,
	AIC_USB_HOST,
	AIC_USB_PHY,
};

struct artinchip_usb_clock {
	const char *name;
	const struct device *dev;
	clock_control_subsys_t id;
};

struct artinchip_usb_reset {
	const char *name;
	struct reset_dt_spec spec;
};

struct artinchip_usb_config {
	uintptr_t device_base;
	uintptr_t host_base;
	uintptr_t syscfg_base;
	enum artinchip_usb_role dr_mode;
	struct artinchip_usb_clock clocks[AIC_USB_CLK_COUNT];
	struct artinchip_usb_reset resets[AIC_USB_RESET_COUNT];
};

struct artinchip_usb_data {
	enum artinchip_usb_role role;
};

#if defined(CONFIG_ARTINCHIP_USB_SHELL)
static const char *artinchip_usb_role_string(enum artinchip_usb_role role)
{
	switch (role) {
	case AIC_USB_ROLE_HOST:
		return "host";
	case AIC_USB_ROLE_DEVICE:
		return "device";
	case AIC_USB_ROLE_OTG:
		return "otg";
	default:
		return "unknown";
	}
}
#endif

static enum artinchip_usb_role artinchip_usb_role_from_syscfg(const struct artinchip_usb_config *config,
						    enum artinchip_usb_role fallback)
{
	if ((sys_read32(config->syscfg_base + AIC_SYSCFG_USB0_CFG) &
	     AIC_SYSCFG_USB0_CFG_DRD_MODE) != 0U) {
		return AIC_USB_ROLE_DEVICE;
	}

	ARG_UNUSED(fallback);

	return AIC_USB_ROLE_HOST;
}

static void artinchip_usb_role_set_hw(const struct artinchip_usb_config *config, enum artinchip_usb_role role)
{
	uint32_t val = sys_read32(config->syscfg_base + AIC_SYSCFG_USB0_CFG);

	if (role == AIC_USB_ROLE_DEVICE) {
		val |= AIC_SYSCFG_USB0_CFG_DRD_MODE;
	} else {
		val &= ~AIC_SYSCFG_USB0_CFG_DRD_MODE;
	}

	sys_write32(val, config->syscfg_base + AIC_SYSCFG_USB0_CFG);
}

static void artinchip_usb_host_setup_hw(const struct artinchip_usb_config *config)
{
	uint32_t val;

	val = sys_read32(config->host_base + AIC_USB_HOST_PHY_IF);
	sys_write32(val | AIC_USB_HOST_PHY_IF_UTMI, config->host_base + AIC_USB_HOST_PHY_IF);
	sys_write32(AIC_USB_HOST_FIFO_THRESHOLD,
		    config->host_base + AIC_USB_HOST_AHB2STBUS_INSREG01);
}

static int artinchip_usb_clock_set_one(const struct artinchip_usb_config *config, enum artinchip_usb_block block,
				  bool enable)
{
	return enable ? clock_control_on(config->clocks[block].dev, config->clocks[block].id) :
			clock_control_off(config->clocks[block].dev, config->clocks[block].id);
}

static int artinchip_usb_clock_set_all(const struct artinchip_usb_config *config, bool enable)
{
	int ret;

	for (uint8_t i = 0U; i < AIC_USB_CLK_COUNT; i++) {
		ret = artinchip_usb_clock_set_one(config, i, enable);
		if (ret != 0) {
			return ret;
		}
	}

	return 0;
}

static int artinchip_usb_reset_deassert_one(const struct artinchip_usb_config *config,
				       enum artinchip_usb_block block)
{
	return reset_line_deassert_dt(&config->resets[block].spec);
}

#if defined(CONFIG_ARTINCHIP_USB_SHELL)
static int artinchip_usb_reset_assert_one(const struct artinchip_usb_config *config, enum artinchip_usb_block block)
{
	return reset_line_assert_dt(&config->resets[block].spec);
}

static int artinchip_usb_block_prepare(const struct artinchip_usb_config *config, enum artinchip_usb_block block)
{
	int ret;

	ret = artinchip_usb_clock_set_one(config, AIC_USB_PHY, true);
	if (ret != 0) {
		return ret;
	}

	ret = artinchip_usb_clock_set_one(config, block, true);
	if (ret != 0) {
		return ret;
	}

	k_busy_wait(AIC_USB_RESET_DELAY_US);
	ret = artinchip_usb_reset_assert_one(config, AIC_USB_PHY);
	if (ret != 0) {
		return ret;
	}

	ret = artinchip_usb_reset_assert_one(config, block);
	if (ret != 0) {
		return ret;
	}

	k_busy_wait(AIC_USB_RESET_DELAY_US);
	ret = artinchip_usb_reset_deassert_one(config, AIC_USB_PHY);
	if (ret != 0) {
		return ret;
	}

	ret = artinchip_usb_reset_deassert_one(config, block);
	if (ret != 0) {
		return ret;
	}

	k_busy_wait(AIC_USB_RESET_DELAY_US);
	return 0;
}

static int artinchip_usb_prepare_role(const struct device *dev, enum artinchip_usb_role role)
{
	const struct artinchip_usb_config *config = dev->config;
	struct artinchip_usb_data *data = dev->data;
	enum artinchip_usb_block block;
	int ret;

	block = role == AIC_USB_ROLE_HOST ? AIC_USB_HOST : AIC_USB_DEVICE;
	artinchip_usb_role_set_hw(config, role);

	ret = artinchip_usb_block_prepare(config, block);
	if (ret != 0) {
		return ret;
	}

	if (role == AIC_USB_ROLE_HOST) {
		artinchip_usb_host_setup_hw(config);
	}

	data->role = role;
	return 0;
}
#endif

static int artinchip_usb_reset_deassert_all(const struct artinchip_usb_config *config)
{
	int ret;

	for (uint8_t i = 0U; i < AIC_USB_RESET_COUNT; i++) {
		ret = artinchip_usb_reset_deassert_one(config, i);
		if (ret != 0) {
			return ret;
		}
	}

	return 0;
}

static int artinchip_usb_init(const struct device *dev)
{
	const struct artinchip_usb_config *config = dev->config;
	struct artinchip_usb_data *data = dev->data;
	int ret;

	for (uint8_t i = 0U; i < AIC_USB_CLK_COUNT; i++) {
		if (!device_is_ready(config->clocks[i].dev)) {
			return -ENODEV;
		}
	}
	for (uint8_t i = 0U; i < AIC_USB_RESET_COUNT; i++) {
		if (!device_is_ready(config->resets[i].spec.dev)) {
			return -ENODEV;
		}
	}

	ret = artinchip_usb_clock_set_all(config, true);
	if (ret != 0) {
		return ret;
	}

	ret = artinchip_usb_reset_deassert_all(config);
	if (ret != 0) {
		return ret;
	}

	data->role = config->dr_mode;
	if (data->role != AIC_USB_ROLE_OTG) {
		artinchip_usb_role_set_hw(config, data->role);
	} else {
		data->role = artinchip_usb_role_from_syscfg(config, AIC_USB_ROLE_OTG);
	}
	if (data->role == AIC_USB_ROLE_HOST) {
		artinchip_usb_host_setup_hw(config);
	}

	return 0;
}

#if defined(CONFIG_ARTINCHIP_USB_SHELL)
static int artinchip_usb_reset_toggle_one(const struct artinchip_usb_config *config,
				     enum artinchip_usb_block block)
{
	return reset_line_toggle_dt(&config->resets[block].spec);
}

static int artinchip_usb_reset_toggle_all(const struct artinchip_usb_config *config)
{
	int ret;

	for (uint8_t i = 0U; i < AIC_USB_RESET_COUNT; i++) {
		ret = artinchip_usb_reset_toggle_one(config, i);
		if (ret != 0) {
			return ret;
		}
	}

	return 0;
}

static const struct device *artinchip_usb_shell_dev(void)
{
	return DEVICE_DT_GET(DT_INST(0, artinchip_d13x_usb));
}

static const char *artinchip_usb_clock_status_string(enum clock_control_status status)
{
	switch (status) {
	case CLOCK_CONTROL_STATUS_ON:
		return "on";
	case CLOCK_CONTROL_STATUS_OFF:
		return "off";
	default:
		return "unknown";
	}
}

static void artinchip_usb_shell_print_block(const struct shell *sh,
				       const struct artinchip_usb_config *config,
				       enum artinchip_usb_block block)
{
	enum clock_control_status clk_status;
	uint32_t rate = 0U;
	uint8_t rst_status = 0U;

	clk_status = clock_control_get_status(config->clocks[block].dev, config->clocks[block].id);
	(void)clock_control_get_rate(config->clocks[block].dev, config->clocks[block].id, &rate);
	(void)reset_status_dt(&config->resets[block].spec, &rst_status);

	shell_print(sh, "%-6s clock=%-7s rate=%u Hz reset=%s", config->clocks[block].name,
		    artinchip_usb_clock_status_string(clk_status), rate,
		    rst_status != 0U ? "asserted" : "deasserted");
}

static int artinchip_usb_shell_status(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *dev = artinchip_usb_shell_dev();
	const struct artinchip_usb_config *config = dev->config;
	struct artinchip_usb_data *data = dev->data;
	uint32_t usb0_cfg;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!device_is_ready(dev)) {
		shell_error(sh, "USB device is not ready");
		return -ENODEV;
	}

	shell_print(sh, "device: %s", dev->name);
	shell_print(sh, "base: device=0x%08lx host=0x%08lx",
		    (unsigned long)config->device_base, (unsigned long)config->host_base);
	usb0_cfg = sys_read32(config->syscfg_base + AIC_SYSCFG_USB0_CFG);
	data->role = artinchip_usb_role_from_syscfg(config, data->role);
	shell_print(sh, "role: %s (default=%s, USB0_CFG=0x%08x)", artinchip_usb_role_string(data->role),
		    artinchip_usb_role_string(config->dr_mode), usb0_cfg);
	artinchip_usb_shell_print_block(sh, config, AIC_USB_DEVICE);
	artinchip_usb_shell_print_block(sh, config, AIC_USB_HOST);
	artinchip_usb_shell_print_block(sh, config, AIC_USB_PHY);

	shell_print(sh, "ehci cap/version=0x%08x hcsparams=0x%08x hccparams=0x%08x",
		    sys_read32(config->host_base + AIC_USB_EHCI_CAPLENGTH_HCIVERSION),
		    sys_read32(config->host_base + AIC_USB_EHCI_HCSPARAMS),
		    sys_read32(config->host_base + AIC_USB_EHCI_HCCPARAMS));
	shell_print(sh, "ohci hcrevision=0x%08x",
		    sys_read32(config->host_base + AIC_USB_OHCI_HCREVISION));
	shell_print(sh, "device regs: 0x%08x 0x%08x 0x%08x 0x%08x",
		    sys_read32(config->device_base), sys_read32(config->device_base + 0x04U),
		    sys_read32(config->device_base + 0x08U),
		    sys_read32(config->device_base + 0x0CU));

	return 0;
}

static int artinchip_usb_shell_role(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *dev = artinchip_usb_shell_dev();
	const struct artinchip_usb_config *config = dev->config;
	struct artinchip_usb_data *data = dev->data;
	enum artinchip_usb_role role;

	if (argc == 1U) {
		data->role = artinchip_usb_role_from_syscfg(config, data->role);
		shell_print(sh, "%s", artinchip_usb_role_string(data->role));
		return 0;
	}

	if (strcmp(argv[1], "host") == 0) {
		role = AIC_USB_ROLE_HOST;
	} else if (strcmp(argv[1], "device") == 0 || strcmp(argv[1], "peripheral") == 0) {
		role = AIC_USB_ROLE_DEVICE;
	} else {
		return -EINVAL;
	}

	artinchip_usb_role_set_hw(config, role);
	data->role = role;
	if (role == AIC_USB_ROLE_HOST) {
		artinchip_usb_host_setup_hw(config);
	}
	shell_print(sh, "USB0 PHY role switched to %s", artinchip_usb_role_string(role));

	return 0;
}

static int artinchip_usb_shell_power(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *dev = artinchip_usb_shell_dev();
	const struct artinchip_usb_config *config = dev->config;
	bool enable;
	int ret;

	if (argc != 2U) {
		return -EINVAL;
	}

	if (strcmp(argv[1], "on") == 0) {
		enable = true;
	} else if (strcmp(argv[1], "off") == 0) {
		enable = false;
	} else {
		return -EINVAL;
	}

	ret = artinchip_usb_clock_set_all(config, enable);
	if (ret != 0) {
		shell_error(sh, "USB clock %s failed: %d", enable ? "on" : "off", ret);
		return ret;
	}

	shell_print(sh, "USB clocks %s", enable ? "enabled" : "disabled");
	return 0;
}

static int artinchip_usb_shell_block_power(const struct shell *sh, size_t argc, char **argv,
				      enum artinchip_usb_block block)
{
	const struct device *dev = artinchip_usb_shell_dev();
	const struct artinchip_usb_config *config = dev->config;
	bool enable;
	int ret;

	if (argc != 2U) {
		return -EINVAL;
	}

	if (strcmp(argv[1], "on") == 0) {
		enable = true;
	} else if (strcmp(argv[1], "off") == 0) {
		enable = false;
	} else {
		return -EINVAL;
	}

	ret = artinchip_usb_clock_set_one(config, block, enable);
	if (ret != 0) {
		shell_error(sh, "%s clock %s failed: %d", config->clocks[block].name,
			    enable ? "on" : "off", ret);
		return ret;
	}

	shell_print(sh, "%s clock %s", config->clocks[block].name, enable ? "enabled" : "disabled");
	return 0;
}

static int artinchip_usb_shell_block_status(const struct shell *sh, enum artinchip_usb_block block)
{
	const struct device *dev = artinchip_usb_shell_dev();
	const struct artinchip_usb_config *config = dev->config;

	artinchip_usb_shell_print_block(sh, config, block);

	if (block == AIC_USB_DEVICE) {
		shell_print(sh, "device regs: 0x%08x 0x%08x 0x%08x 0x%08x",
			    sys_read32(config->device_base), sys_read32(config->device_base + 0x04U),
			    sys_read32(config->device_base + 0x08U),
			    sys_read32(config->device_base + 0x0CU));
	} else if (block == AIC_USB_HOST) {
		shell_print(sh, "ehci cap/version=0x%08x hcsparams=0x%08x hccparams=0x%08x",
			    sys_read32(config->host_base + AIC_USB_EHCI_CAPLENGTH_HCIVERSION),
			    sys_read32(config->host_base + AIC_USB_EHCI_HCSPARAMS),
			    sys_read32(config->host_base + AIC_USB_EHCI_HCCPARAMS));
		shell_print(sh, "ohci hcrevision=0x%08x",
			    sys_read32(config->host_base + AIC_USB_OHCI_HCREVISION));
	} else {
		shell_print(sh, "USB0_CFG=0x%08x",
			    sys_read32(config->syscfg_base + AIC_SYSCFG_USB0_CFG));
	}

	return 0;
}

static int artinchip_usb_shell_block_reset(const struct shell *sh, enum artinchip_usb_block block)
{
	const struct device *dev = artinchip_usb_shell_dev();
	const struct artinchip_usb_config *config = dev->config;
	int ret;

	ret = artinchip_usb_reset_toggle_one(config, block);
	if (ret != 0) {
		shell_error(sh, "%s reset failed: %d", config->resets[block].name, ret);
		return ret;
	}
	if (block == AIC_USB_HOST) {
		artinchip_usb_host_setup_hw(config);
	}

	shell_print(sh, "%s reset toggled", config->resets[block].name);
	return 0;
}

static int artinchip_usb_shell_device_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	return artinchip_usb_shell_block_status(sh, AIC_USB_DEVICE);
}

static int artinchip_usb_shell_device_power(const struct shell *sh, size_t argc, char **argv)
{
	return artinchip_usb_shell_block_power(sh, argc, argv, AIC_USB_DEVICE);
}

static int artinchip_usb_shell_device_reset(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	return artinchip_usb_shell_block_reset(sh, AIC_USB_DEVICE);
}

static int artinchip_usb_shell_device_prepare(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *dev = artinchip_usb_shell_dev();
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ret = artinchip_usb_prepare_role(dev, AIC_USB_ROLE_DEVICE);
	if (ret != 0) {
		shell_error(sh, "USB device prepare failed: %d", ret);
		return ret;
	}

	shell_print(sh, "USB device block prepared");
	return 0;
}

static int artinchip_usb_shell_host_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	return artinchip_usb_shell_block_status(sh, AIC_USB_HOST);
}

static int artinchip_usb_shell_host_power(const struct shell *sh, size_t argc, char **argv)
{
	return artinchip_usb_shell_block_power(sh, argc, argv, AIC_USB_HOST);
}

static int artinchip_usb_shell_host_reset(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	return artinchip_usb_shell_block_reset(sh, AIC_USB_HOST);
}

static int artinchip_usb_shell_host_prepare(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *dev = artinchip_usb_shell_dev();
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ret = artinchip_usb_prepare_role(dev, AIC_USB_ROLE_HOST);
	if (ret != 0) {
		shell_error(sh, "USB host prepare failed: %d", ret);
		return ret;
	}

	shell_print(sh, "USB host block prepared");
	return 0;
}

static int artinchip_usb_shell_phy_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	return artinchip_usb_shell_block_status(sh, AIC_USB_PHY);
}

static int artinchip_usb_shell_phy_power(const struct shell *sh, size_t argc, char **argv)
{
	return artinchip_usb_shell_block_power(sh, argc, argv, AIC_USB_PHY);
}

static int artinchip_usb_shell_phy_reset(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	return artinchip_usb_shell_block_reset(sh, AIC_USB_PHY);
}

static int artinchip_usb_shell_reset(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *dev = artinchip_usb_shell_dev();
	const struct artinchip_usb_config *config = dev->config;
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ret = artinchip_usb_reset_toggle_all(config);
	if (ret != 0) {
		shell_error(sh, "USB reset failed: %d", ret);
		return ret;
	}

	shell_print(sh, "USB reset toggled");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(artinchip_usb_device_cmds,
	SHELL_CMD(status, NULL, "Show USB device-controller status.",
		  artinchip_usb_shell_device_status),
	SHELL_CMD_ARG(power, NULL, "Enable or disable this USB block clock: power <on|off>.",
		      artinchip_usb_shell_device_power, 2, 0),
	SHELL_CMD(reset, NULL, "Toggle USB device-controller reset.",
		  artinchip_usb_shell_device_reset),
	SHELL_CMD(prepare, NULL, "Switch PHY to device role and prepare USBD.",
		  artinchip_usb_shell_device_prepare),
	SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(artinchip_usb_host_cmds,
	SHELL_CMD(status, NULL, "Show USB host-controller status.", artinchip_usb_shell_host_status),
	SHELL_CMD_ARG(power, NULL, "Enable or disable this USB block clock: power <on|off>.",
		      artinchip_usb_shell_host_power, 2, 0),
	SHELL_CMD(reset, NULL, "Toggle USB host-controller reset.", artinchip_usb_shell_host_reset),
	SHELL_CMD(prepare, NULL, "Switch PHY to host role and prepare USBH0.",
		  artinchip_usb_shell_host_prepare),
	SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(artinchip_usb_phy_cmds,
	SHELL_CMD(status, NULL, "Show USB PHY status.", artinchip_usb_shell_phy_status),
	SHELL_CMD_ARG(power, NULL, "Enable or disable this USB block clock: power <on|off>.",
		      artinchip_usb_shell_phy_power, 2, 0),
	SHELL_CMD(reset, NULL, "Toggle USB PHY reset.", artinchip_usb_shell_phy_reset),
	SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(
	artinchip_usb_cmds,
	SHELL_CMD(status, NULL, "Show USB clock/reset and controller registers.",
		  artinchip_usb_shell_status),
	SHELL_CMD_ARG(role, NULL, "Show or set USB0 PHY role: role [host|device].",
		      artinchip_usb_shell_role, 1, 1),
	SHELL_CMD(device, &artinchip_usb_device_cmds, "USB device-controller diagnostics.", NULL),
	SHELL_CMD(host, &artinchip_usb_host_cmds, "USB host-controller diagnostics.", NULL),
	SHELL_CMD(phy, &artinchip_usb_phy_cmds, "USB PHY diagnostics.", NULL),
	SHELL_CMD_ARG(power, NULL, "Enable or disable USB clocks: power <on|off>.",
		      artinchip_usb_shell_power, 2, 0),
	SHELL_CMD_ARG(reset, NULL, "Toggle USB device/host/PHY resets.", artinchip_usb_shell_reset,
		      1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(aicusb, &artinchip_usb_cmds, "ArtInChip USB diagnostics.", NULL);
#endif

#define AIC_USB_CLOCK_SPEC(inst, idx, label)                                                       \
	{                                                                                          \
		.name = label,                                                                      \
		.dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR_BY_IDX(inst, idx)),                        \
		.id = (clock_control_subsys_t)DT_INST_CLOCKS_CELL_BY_IDX(inst, idx, clkid),          \
	}

#define AIC_USB_RESET_SPEC(inst, idx, label)                                                       \
	{                                                                                          \
		.name = label,                                                                      \
		.spec = RESET_DT_SPEC_GET_BY_IDX(DT_DRV_INST(inst), idx),                           \
	}

#define AIC_USB_INIT(inst)                                                                         \
	static struct artinchip_usb_data artinchip_usb_data_##inst;                                           \
	static const struct artinchip_usb_config artinchip_usb_config_##inst = {                              \
		.device_base = DT_INST_REG_ADDR_BY_NAME(inst, device),                              \
		.host_base = DT_INST_REG_ADDR_BY_NAME(inst, host),                                  \
		.syscfg_base = DT_REG_ADDR(DT_INST_PHANDLE(inst, artinchip_syscfg)),                \
		.dr_mode = DT_INST_ENUM_IDX_OR(inst, dr_mode, AIC_USB_ROLE_OTG),                  \
		.clocks = {                                                                         \
			AIC_USB_CLOCK_SPEC(inst, 0, "device"),                                      \
			AIC_USB_CLOCK_SPEC(inst, 1, "host"),                                        \
			AIC_USB_CLOCK_SPEC(inst, 2, "phy"),                                         \
		},                                                                                  \
		.resets = {                                                                         \
			AIC_USB_RESET_SPEC(inst, 0, "device"),                                      \
			AIC_USB_RESET_SPEC(inst, 1, "host"),                                        \
			AIC_USB_RESET_SPEC(inst, 2, "phy"),                                         \
		},                                                                                  \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(inst, artinchip_usb_init, NULL, &artinchip_usb_data_##inst,                     \
			      &artinchip_usb_config_##inst,                                                \
			      POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);

DT_INST_FOREACH_STATUS_OKAY(AIC_USB_INIT)
