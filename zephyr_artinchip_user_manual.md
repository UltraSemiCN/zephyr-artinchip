# ArtInChip 构建与打包

当前支持启动链：PBP → MCUboot → 签名 Zephyr（RAM-load）。
以下命令在包含 `zephyr-artinchip` 的 West 工作区根目录执行。

## 准备环境

安装 Zephyr 所需主机依赖和与源码版本匹配的 Zephyr SDK，运行 SDK 的
`setup.sh -c` 注册工具链。参见仓库的 `doc/develop/getting_started/index.rst`。

工作区结构：

```text
workspace_artinchip/
├── zephyr-artinchip/
├── zephyr-artinchip-tools/
├── .venv/
├── bootloader/
├── modules/
└── build/
```

首次初始化已有源码工作区：

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install west
pip install -r zephyr-artinchip/scripts/requirements-base.txt
west init -l zephyr-artinchip
west update
west zephyr-export
```

已初始化的工作区只需激活虚拟环境；切换源码版本后用 `west update` 同步模块。
打包工具的额外依赖见 `zephyr-artinchip-tools/README.md`。

## 编译

UltraSemi 5.5 寸、720x1280 MIPI 屏板型的官方 LVGL 示例：

定制仪表盘已迁入 `zephyr-artinchip-demo-apps/applications/dashboard`，
构建说明见该仓库的 `README.md`。

```bash
west build --sysbuild -p always \
  -b ultrasemi_d12p_usbdisp_demo88_nor_5_5/d125ccv \
  zephyr-artinchip/samples/subsys/display/lvgl \
  -d build/ultrasemi_d12p_usbdisp_demo88_nor_5_5_lvgl -- \
  -DSB_CONFIG_BOOTLOADER_MCUBOOT=y \
  -DSB_CONFIG_MCUBOOT_MODE_RAM_LOAD=y \
  -DSB_CONFIG_BOOT_SIGNATURE_TYPE_ECDSA_P256=y
```

使用标准 sysbuild 集成 MCUboot。生产签名密钥通过
`SB_CONFIG_BOOT_SIGNATURE_KEY_FILE` 指定；默认开发密钥仅用于测试。

## 打包与烧录

```bash
python zephyr-artinchip-tools/scripts/mk_image_zephyr.py \
  --build-dir build/ultrasemi_d12p_usbdisp_demo88_nor_5_5_lvgl
```

镜像位于 `build/ultrasemi_d12p_usbdisp_demo88_nor_5_5_lvgl/airburn/`，文件名由板型和版本信息生成。
使用 ArtInChip 原有烧录工具选择其中的 `.img` 文件；无需手工指定分区参数。
USB updater 仅在烧录时运行于 RAM，Flash 中的启动程序为 MCUboot。

UART0 使用 PA0/PA1，115200、8N1、关闭硬件流控，Shell 提示符由应用配置；dashboard 使用 `aic:~$`。
外置 NOR 使用 PB0～PB5。D125 启动及 Shell 已验证；最新验证未接屏，
显示功能仍需接屏测试。
