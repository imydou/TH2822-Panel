# 第三方资料与许可

- `references/th2822d-cli`：LHX369963，MIT，Copyright (c) 2026 LHX369963。完整许可保存在该仓库LICENSE与 `licenses/th2822d-cli-MIT.txt`。参考其协议行为和验证记录，新写C++核心，未运行其连机脚本。引用提交 e935bd9895c4bb0e667308bae22126edd549e199。
- Waveshare ESP32-S3-Touch-LCD-4.3 官方原理图和Demo：保留为原始参考资料；板级初始化依据官方08_lvgl_Porting。该示例waveshare_rgb_lcd_port.c头部声明Copyright2022 Espressif Systems、CC0-1.0，相关说明保留于board.c。其他下载内容遵守其各自许可证，不将整份PDF或ZIP声明为本项目原创。
- LVGL8.4.0：MIT。ESP-IDF5.4.4、esp_lvgl_port、esp_lcd_touch、GT911、USB Host CDC/CP210x组件：对应组件内许可（主要Apache-2.0），由官方Component Registry取得并锁定哈希。`managed_components/`保留LICENSE。
- SDL2：zlib，桌面模拟器使用本机安装的动态库，不打包库本体。
- Tonghui两份PDF：用户提供的产品册/手册，保留来源与Library身份，不作为代码授权内容再授权。
- Silicon Labs AN571 rev0.4：仅用于核对CP210x临时流控请求格式。

- Noto Sans SC：Google Fonts官方来源，SIL Open Font License 1.1，许可与copyright声明见fonts/OFL.txt；生成的中文子集沿用OFL。lv_font_conv1.5.3为MIT、FontTools4.60.1为MIT，用于离线字体生成，依赖锁文件保留。

- 同惠TH2822/TH2822A/TH2822C中文手册：用户新增Library附件与厂家20190912091407_647.pdf内容一致，保留来源，仅用于协议和能力核对。
