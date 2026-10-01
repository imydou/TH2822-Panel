# TH2822 Series Touch Panel

ESP32-S3 原生 USB Host 触摸控制面板，面向 **TH2822 系列**。指定硬件为 **Waveshare ESP32-S3-Touch-LCD-4.3（非 B/C）**，800×480、ESP-IDF5.4.4、LVGL8.4。默认中文，可扩展多语言、保存语言偏好。

**0.3.2 无产品模拟模式、虚拟读数或手动选型。只有合法身份响应才显示仪表已连接。** 无仪表时显示`--`。0.3.2连续菜单操作已由用户确认“显示正常，没有乱跳”，保留0.2.3相同板级源码与显示配置；仪表不在现场，各型号端到端通信均尚未实测通过。

## 功能

- CP210x专用USB Host驱动，按已知10c4:ea60/interface0选择；不将CP210x冒充通用CDC-ACM。
- 区分USB枚举、串口就绪、查询身份和已识别仪表；`*IDN?`自动映射能力，未知或不完整身份拒绝连接。
- TH2822A/C/D/E均有官方手册依据，默认主动接收Auto Fetch，屏幕提供RMT操作说明；等待或接收期间无后台查询/心跳。
- 正式支持范围为TH2822A/C/D/E四个型号；仅返回TH2822系列名时提示型号不明确，拒绝建立仪表连接，不推断为任一型号。
- 主副读数、单位、OL/无效状态；L/C/R/Z，D/E的DCR；频率/电平按型号门控，C/E含100kHz。
- 设置通过单队列发送并读回验证；查询最多重试一次，设置不自动重放；错误可见，热插拔重连。
- 本地保持及主参数MIN/MAX/AVG，参数变化清统计；独立于仪表内置HOLD/REC。
- 不执行校准、永久设置、自动LCR、仪表内部统计或容差控制。仪表RATE需在仪表面板操作。

## 使用

1. UART Type-C负责供电/调试，原生USB Type-C通过确认过的OTG线接仪表；先阅读[接线条件](docs/HARDWARE.md)。
2. 上电或热插入后自动查询身份及设置。USB已枚举不代表仪表已连接。
3. 查询完毕进入远程：仪表LCD的RMT常亮时短按退本地，本地再短按开启主动发送；RMT闪烁表示发送。
4. 点击屏幕设置会中断主动发送；读回完毕按提示恢复。仪表上改过参数后，先点“同步设置”再恢复发送。
5. 无数据超过5秒显示无效读数、等待发送，保留已识别身份；接收说明可随时查看。备用查询采集由用户显式选择。

上报帧不带单位，主动模式的参数标签表示上次同步的设置。保持仪表设置不变，详情见[主动发送与RMT](docs/AUTO_FETCH.md)。

## 构建与验证

```sh
. /path/to/esp-idf-v5.4.4/export.sh
idf.py build
cmake -S tests -B build-host
cmake --build build-host
ctest --test-dir build-host --output-on-failure
python3 scripts/check_locales.py
```

直接依赖固定在`main/idf_component.yml`，完整依赖版本与哈希在`dependencies.lock`。16MB flash、8MB octal PSRAM的本次配置保存在`sdkconfig`及`sdkconfig.defaults`。

原生UI测试预览仅在主机测试目标，固定输入均明确标记，不编入固件：

```sh
cmake -S tests/ui_preview -B build-preview
cmake --build build-preview -j 8
./build-preview/panel_preview --capture docs/screenshots/v0.3-zh zh-CN
```

默认横屏0°；menuconfig的TH2822 panel选项提供180°完整帧翻转与触摸映射，该方向尚待实屏验收。未实现90°/270°布局。

UART115200诊断：`state`、`locale zh-CN`、`locale en`，及临时显示诊断`pclk 12/16/21`。没有模拟指令或任意仪表命令入口。

## 交付与证据

- `release/0.3.2/`：成功构建后整理的bin/elf、bootloader/partition、配置、锁文件、源码ZIP与SHA256SUMS。
- [构建、烧录与原板备份](docs/BUILD_FLASH.md)
- [系列能力与待测矩阵](docs/SUPPORT.md)
- [验收记录](docs/VALIDATION.md)
- [语言资源与字体](docs/I18N.md)
- [进展历史](docs/PROGRESS.md)
- [第三方许可](THIRD_PARTY_NOTICES.md)

`core/`为协议/能力/接收/统计核心，`main/`为板级、USB、队列及LVGL界面；`tests/fixtures/`与`tests/ui_preview/`仅用于主机测试。历史备份、旧版本产物及原始资料保留，均不编入当前固件。
