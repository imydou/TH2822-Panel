# TH2822 Series Touch Panel

ESP32-S3 原生 USB Host 触摸控制面板，面向 **TH2822 系列**。指定硬件为 **Waveshare ESP32-S3-Touch-LCD-4.3（非 B/C）**，800×480、ESP-IDF5.4.4、LVGL8.4。默认中文，可扩展多语言、保存语言偏好。

**0.3.3 仅使用查询采集：在触摸屏设置参数后自动读回并继续测量，无需同步或操作仪表RMT。** 未连接时显示独立连接页，身份确认后进入主界面。没有模拟读数、手动选型或主动上报模式。保留0.3.2已验收的显示源码与配置；用户现已连接TH2822E并报告基础通信正常，具体实测范围见验收记录，不扩大为全系列通过。

0.3.4新增当前参数标记、双列触摸菜单、重复选择不发令、全屏弹窗遮罩和明确的操作名称；已构建/主机验证，尚未刷板。0.3.3的真机通信结论不自动等于0.3.4整体验收通过。详见[交互借鉴与实现](docs/INTERACTION_REVIEW.md)。

## 功能

- CP210x专用USB Host驱动，按已知10c4:ea60/interface0选择；不将CP210x冒充通用CDC-ACM。
- 区分USB枚举、串口就绪、查询身份和已识别仪表；`*IDN?`自动映射能力，未知或不完整身份拒绝连接。
- A/C/D/E均使用自动查询采集，间隔可选1000/500/250ms；这是完成一轮后的间隔，不是仪表测量速度。
- 未连接页面只显示连接进度、错误、语言与重连；已识别主界面的仪表信息页显示真实型号、固件、序列号、完整IDN、USB VID/PID与已读回设置。
- 正式支持范围为TH2822A/C/D/E四个型号；仅返回TH2822系列名时提示型号不明确，拒绝建立仪表连接，不推断为任一型号。
- 主副读数、单位、OL/无效状态；L/C/R/Z，D/E的DCR；频率/电平按型号门控，C/E含100kHz。
- 设置通过单队列发送并读回验证；查询最多重试一次，设置不自动重放；错误可见，热插拔重连。
- 本地保持及主参数MIN/MAX/AVG，参数变化清统计；独立于仪表内置HOLD/REC。
- 不执行校准、永久设置、自动LCR、仪表内部统计或容差控制。仪表RATE需在仪表面板操作。

## 使用

1. UART Type-C负责供电/调试，原生USB Type-C通过确认过的OTG线接仪表；先阅读[接线条件](docs/HARDWARE.md)。
2. 上电或热插入后自动查询身份及设置。USB已枚举不代表仪表已连接。
3. 身份和设置确认后自动查询读数；主界面修改参数，等待自动读回确认即可继续采集。
4. 正常状态不显示同步按钮；失败时显示具体错误和“重新读回”，不重发不确定的设置命令。
5. 点击“仪表信息”查看缓存身份与设置，不额外发仪表命令；USB断开立即返回独立连接页。

仪表保持远程控制，使用触摸屏操作；不提供主动上报/RMT流程。详见[查询采集交互](docs/ACQUISITION.md)。

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

- `release/0.3.4/`：交互优化待实屏验收版本，未自动烧录。
- `release/0.3.3/`：成功构建后整理的bin/elf、bootloader/partition、配置、锁文件、源码ZIP与SHA256SUMS。
- [构建、烧录与原板备份](docs/BUILD_FLASH.md)
- [系列能力与待测矩阵](docs/SUPPORT.md)
- [验收记录](docs/VALIDATION.md)
- [语言资源与字体](docs/I18N.md)
- [进展历史](docs/PROGRESS.md)
- [第三方许可](THIRD_PARTY_NOTICES.md)

`core/`为协议/能力/接收/统计核心，`main/`为板级、USB、队列及LVGL界面；`tests/fixtures/`与`tests/ui_preview/`仅用于主机测试。历史备份、旧版本产物及原始资料保留，均不编入当前固件。
