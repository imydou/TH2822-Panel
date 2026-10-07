# 官方TH2822上位机3.0.1静态检查

2026-10-07，用户提供从同惠官网下载的`20200518171724482.rar`，用于继续调查TH2822E原生AUTO和FAST/SLOW的远程控制。结论：此次从应用资源提取出的界面文字和命令常量中，没有找到这两个功能的远程入口。不能据此证明仪表固件内部绝无未公开命令。

## 文件来源与版本

- 厂家条目：[TH2822上位机软件3.0.1](https://www.tonghui.com.cn/services_software.html?title=TH2822)。此前下载要求登录，本次用户提供的附件已完整落地，文件缺失阻碍已解除。
- RAR：137395667 bytes；SHA256 `65775591d5fdf4f841457814acf87d63b5dddb2d67a6dd87cce614db86014cc7`。
- 包内目录：`LCR Software V3.0.1@160617（台湾）/Volume`。
- 实际应用位于`bin/dp/data.cab`，CAB共12个文件，含LCR Software.exe、INI、ALIASES、TLB、COM配置和图像/报表辅助文件；安装器其他目录主要是运行库。未安装这些组件。
- EXE：2161152 bytes；SHA256 `725f038986ec4f610b7b6e0dc2b12c51366f1e2d1dd29f61a5c53cb61fc91161`。
- PE版本资源仍为FileVersion/ProductVersion 2.0.0.0、CompanyName BK；主程序解压数据中明确存在`V3.0.1@160617`，位于下述DFDS解压偏移0xBF8。因此不能仅凭Windows版本属性把本包认定为旧2.0.0或与参考项目文件完全相同；未取得旧EXE做二进制比对。
- 包内应用/清单时间为2016-06-17，RAR名称像2020年时间戳；二者均不作为仪表固件发布日期。

## 检查方法和复核位置

未运行setup.exe、MSI、实际应用或第三方分析脚本。用已有bsdtar/7-Zip作为归档读取工具，随后用本次编写的Python标准库脚本读取RSRC目录与zlib压缩资源。7-Zip能列RAR、读取未压缩CAB项，但不支持部分RAR压缩方法，已使用系统bsdtar成功读取这些项。PE列表提示Checksum error；记录为文件头校验和警告，不将它当作已证明的文件损坏或安全结论。

可复跑脚本和原始文件保存在本地`references/software-3.0.1/`，排除Git。`analyze.py`仅读文件、提取数据、写分析结果，不连接串口。解析3850个资源段，扫描得到444个达到流末尾的zlib流；主数据、主界面、初始化数据均与资源中声明的解压长度独立核对通过。

| 关键证据 | EXE中zlib起点 | 解压长度 | 输出 |
|---|---|---|---|
| 主程序DFDS数据 | 0x1EF920 | 26981 bytes | analysis/1ef920.strings.txt |
| 主界面FPHb数据 | 0x20AE4C | 20455 bytes | analysis/20ae4c.strings.txt |
| 初始化相关DFDS数据 | 0xC11DC | 1705 bytes | analysis/c11dc.strings.txt |

`analysis/summary.json`保存EXE哈希、命令常量和关键词命中；`resources.json`/`streams.json`保存资源映射。RSRC块目录与段字段布局同时对照了[公开格式解析实现](https://github.com/nicemonday/VietSubLabView/blob/master/LVblock.py)，未执行其代码。

## 提取结果

下表是应用内嵌常量，不等于本台E已经接受，更不等于全系列支持。没有把这些字符串自动发送给仪表。

| 类别 | 找到的常量举例 |
|---|---|
| 身份/读数 | `*IDN?`、`fetc?` |
| 主参数 | `func:impa L/C/R/Z/DCR`（各自完整常量） |
| 副参数 | `func:impb d/q/theta/esr`（各自完整常量） |
| 频率 | `freq 100hz/120hz/1khz/10khz/100khz`（各自完整常量） |
| 等效电路 | `func:equ ser`、`func:equ pal` |
| 容差 | `calc:tol:stat on/off`、`calc:tol:nom?`、`calc:tol:bin 1/2/3/4` |
| 相对测量 | `calc:rel:stat on/off`、`calc:rel:valu?` |
| 原生AUTO、FAST/SLOW | 未找到相应命令常量 |

主界面资源可读选项含DCR、DEG/ESR、100Hz至100kHz、Series/Parallel、Start/Stop、Record Data、Clear Data、容差档位等；主程序数据含Primary、Secondary、Frequency、Measurement、Tolerance、Relative等栏目，未找到AUTO/FAST/SLOW控制文字。Start/Stop或采集按钮本身不能推导成仪表测量速度控制。

关键词交叉核对：

- 解压资源中“auto”命中的是LabVIEW的`auto error handling dialog`，不是自动LCR。
- “Rate”命中`Serial Settings:Baud Rate?`（串口波特率），不是测量RATE；其他子串命中也不当作仪表命令。
- 解压流中无FAST/SLOW/SPEED测量控制命中；原始EXE的ASCII（不分大小写）及UTF-16 LE/BE中亦未找到FAST/SLOW/SPEED。
- 检查解压流的GBK“自动/速度/快速/慢速”未命中。
- 软件包含BK878B/BK879B/BK880相关名称和分支文字。相对测量及容差BIN等常量与当前D/E手册不完全一致，不能因为出现在官方包就用于本台E，更不能泛化到其他机型。

## 结论及未解决部分

结合本台TH2822E VER4.5.2307此前对`RATE FAST`和`FUNC:RATE FAST`显示E10的实证，这份软件暂未提供可用于解决AUTO和FAST/SLOW的新增命令依据。没有新增无依据的固件控件，也没有用本地识别或轮询速度替代它们。

本次属于静态资源检查，未恢复全部编译控制流、未动态观察Windows界面、未抓取上位机真实串口流量。资源中未找到不代表逻辑上不存在动态拼接命令或厂家未公开协议；也不能断言仪表硬件不支持远程控制。

后续最小协议需求可直接交厂家确认：

> TH2822E，固件VER4.5.2307，9600 8N1。请确认原生AUTO LCR和FAST/SLOW是否可远程设置/查询；若支持，请提供精确命令、参数、响应、适用固件及执行等待时间。AUTO下FUNC:IMPA?返回当前C，AUTO仍亮；RATE FAST和FUNC:RATE FAST报E10。另请说明AUTO自动切换L/C/R时，如何保证查询的参数类型与FETCh?读数对应。若未开放，请明确说明。

这段询问仅作为本地记录，未替用户向厂家或GitHub发送消息。本次未打开仪表串口、未执行校准/保存/复位、未构建或烧录固件。
