# 硬件核对与接线条件

## 依据与适用板

[微雪指定型号官方页](https://docs.waveshare.net/ESP32-S3-Touch-LCD-4.3/)、[官方原理图](https://files.waveshare.net/wiki/ESP32-S3-Touch-LCD-4.3/manual/ESP32-S3-Touch-LCD-4.3-Sch.pdf)、[官方示例下载](https://files.waveshare.net/wiki/ESP32-S3-LCD-4.3/ESP32-S3-Touch-LCD-4.3-Demo.zip)。本地副本在 `references/`。原理图第1页实际像素已核对USB、电源、RGB、触摸、FSUSB42区域。原理图下载含3页，实际板修订版本需实物核对。

指定型号屏为800×480 RGB565，WROOM-1 N16R8，16MB flash/8MB PSRAM，GT911触摸、CH422G扩展、FSUSB42复用，CH343P USB-UART。**不是4.3B、4.3C、4B或480×800屏的引脚表**。

| 用途 | 连接 |
|---|---|
| USB D-/D+ | GPIO19 / GPIO20，经过FSUSB42 |
| USB/CAN选择 | CH422G EXIO5：0 USB，1 CAN；本工程始终保持0 |
| 调试UART0 | GPIO43 TX / GPIO44 RX，经CH343P，独立Type-C口 |
| I2C | GPIO8 SDA / GPIO9 SCL，400kHz |
| GT911 | GPIO4 INT，CH422G EXIO1 reset；驱动采用轮询坐标 |
| 背光 | CH422G EXIO2 |
| LCD reset / SD CS | EXIO3 / EXIO4；SD禁用且CS保持高 |
| RGB控制 | PCLK7 / VSYNC3 / HSYNC46 / DE5 |
| RGB数据 D0..D15 | 14,38,18,17,10,39,0,45,48,47,21,1,2,42,41,40 |

LCD/GT911没有占用USB19/20，UART0也独立，因此外围总线能并行。固件禁用CAN、SD和Wi-Fi任务；RGB使用PSRAM双帧缓冲、SRAM bounce buffer、0.2.2请求21MHz像素时钟和官方示例porch（H总820/V总500，理论51.22Hz；非示波器实测）。EXIO5在触摸reset及背光变化期间也保持低，不直接照搬官方reset示例中的0x2C/0x2E（其bit5会选择CAN）。

## VBUS结论（有条件可用）

原理图中 UART Type-C 的供电网为 `VUART`，原生USB Type-C 的VBUS为 `VBUS`。Q4/AO3401与Q7/Q8组成VUART至VBUS路径；Q3/Q5/Q6涉及VUART和系统5V选择。电池充电/升压通过CS8501、D9/D10等形成另一供电路径。**原生VBUS不是一个可由软件控制且带限流检测的Host电源开关**，EXIO5只切换数据，不能控制VBUS。

两口CC1/CC2均5.1kΩ下拉，原生Type-C没有完整的源端Rp/DRP/PD角色控制。即使ESP32原生USB控制器切换为Host，也不会自动把Type-C电源角色变成合规的USB-C Source。不能声称任意C-C线即插即用。

在UART口稳定供5V时，原理图存在向原生VBUS供电的路径，LCD触摸与原生USB Host具备并行工作条件。**本项目已做芯片/驱动/启动软件验证，不等于测量过VBUS电压、限流或回灌电流。**实际板版本、线材、电源、仪表端口电气行为仍须下述检查。

建议接法：电脑/5V电源 → UART Type-C；原生USB Type-C → C插头转USB-A母座的USB2 OTG数据转接 → USB-A至Mini-B数据线 → TH2822。该旧式适配只用于该板5V Host实验链路，不是PD适配方案。不要接只有充电功能的线，也不要把原生口再接电脑/手机等Host。不要直接并联外部5V或使用可能反灌的供电Hub；若确需Hub，应使用明确阻断上行反灌的设备并单独验证。

上仪表前最小电气检查：

1. 确认实板丝印与原理图修订一致，无电池/外加5V造成第二路不受控供电。
2. UART独立供电时测原生VBUS对GND约5V，确认转接后Mini-B端电压与极性，检查带载压降及UART电源余量（官方板本身约450mA，外设另算）。
3. 仪表按手册电池/专用适配器供电；USB虚拟串口电源需求待实测，不能假设USB能供整台仪表。
4. 验证拔掉UART源或给另一端供电时没有向电脑端异常反灌；若无法验证，使用有明确隔离/防反灌规格的接法后再试。
5. 禁止测量带电电路；电容放电后连接。仪表校准/持久设置不在自动执行范围。

## USB串口选择

TH2822资料只保证虚拟串口，未保证全系列芯片一致。参考CLI实测为CP2102 `10c4:ea60`。本固件使用乐鑫 `usb_host_cp210x_vcp` C API（底层借用CDC Host传输层，不按CDC类请求控制CP210x）。枚举时打印完整VID/PID和设备/配置描述符；仅接受该VID/PID与interface0的bulk endpoints。未知芯片显示unsupported，需提供实际描述符扩展驱动，不能凭型号名称猜CDC。

9600/8N1；依据Silicon Labs [AN571 rev0.4](https://www.silabs.com/documents/public/application-notes/AN571.pdf) SET_FLOW禁用硬件和软件流控，保持DTR/RTS。此为串口桥临时配置，不写仪表校准或CP210x持久配置。DTR需求在全系仍待验证。

## 当前显示同步设置

0.2.3：LVGL绘制与RGB/GDMA中断在CPU0同核，防止双核同时占用PSRAM搬运带宽；双帧缓冲、20行SRAM bounce、64KB DCache/64B cache line。关闭每帧无条件DMA重启，保留IDF bounce丢帧自动恢复。界面初始渲染后700ms打开常亮背光，无背光PWM。

21MHz来自微雪[使用说明](https://docs.waveshare.net/ESP32-S3-Touch-LCD-4.3/Instructions-For-Use/)的性能配置；本工程沿用下载示例的H/V各4+8+8消隐，因此820×500。官方页面的41FPS未给完整时序，不能视为本工程实测帧率。未启用页面所述实验120MHz Flash/PSRAM。

180度选项以完整离屏RGB565帧逆序配合触摸坐标映射实现；不依赖RGB直接帧缓冲下不起作用的mirror标志。仅构建和几何单元验证，实屏方向/四角触摸仍待验收。
