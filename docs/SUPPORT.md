# 系列支持证据与能力

能力有官方文档、驱动已实现、主机测试通过、仪表实测通过是不同层级。用户于2026-10-07确认 TH2822E / VER4.5.2307 已完成当前项目功能的实机验证。A/C/D及其他固件版本仍无本项目完整实机验证结果。

## 原文来源

- 用户产品册`references/TH2822.pdf`第2页（印刷27页），已查看像素核对A/C/D/E频率与电平列。
- 用户D/E手册`references/TH2822E Operation Manual.pdf`，标题TH2822D/E、V1.0.0。通信印刷78–98页，Auto Fetch/RMT为82–84页。
- 用户补充`references/TH2822-AC-User-Manual.pdf`：可见封面明确TH2822、TH2822A、TH2822C。84页，RMT/Auto Fetch印刷55–57页（PDF57–59），SCPI58–67页，规格68–69页（PDF70–71），均已核对，能力矩阵与RMT页已查看像素。
- 此用户附件经Library prepare_materialize与官方helper落地，Library ID `libfile_ca74965df464819183b420d95eb1156a`。与[同惠官方原稿](https://www.tonghui.com.cn/upload/UploadAction2/20190912091407_647.pdf)完全相同，SHA256为`3251fbd43f4a6cc2e434463743022fe1f9fd3db62acf79d6db51fd367c2e070d`。PDF元数据沿用其他产品模板；判断适用型号以可见封面、正文和厂家来源为准。封面未标版本，不将URL上传日期当固件版本。

| 型号 | 标称频率 Hz | 电平 Vrms | 主参数 | 文档依据 | 本项目实机 |
|---|---|---|---|---|---|
| TH2822A | 100/120/1000/10000 | 固定0.6 | L/C/R/Z | A/C手册55–57页 | 未联调 |
| TH2822C | 100/120/1000/10000/100000 | 固定0.6 | L/C/R/Z | 同上 | 未联调 |
| TH2822D | 100/120/1000/10000 | 0.3/0.6/1 | L/C/R/Z/DCR | D/E手册82–84页 | 未联调 |
| TH2822E | 100/120/1000/10000/100000 | 0.3/0.6/1 | L/C/R/Z/DCR | 同上 | VER4.5.2307：已完成实机验证（用户确认） |
| 未知后缀 | 禁用 | 禁用 | 禁用 | 不猜测 | 未支持，需资料 |

A/C的0.6V来自明确规格，不发送其手册未提供的VOLT命令。旧手册说明标称120Hz实际为120.048Hz；界面与SCPI菜单使用标称档位。A的10kHz以该型号手册及用户规格列为依据，不采用缺项的简表。

DCR仅D/E启用，并禁用频率、电平、等效电路、副参数设置及IMPB查询。副参数只发送D/Q/THETA/ESR，NULL只接收不作setter。更改主参数后以真实读回的副参数为准，不自动恢复旧参数。

本项目将TH2822作为系列名称，正式目标仅A/C/D/E。旧资料封面中的无后缀名称仅作为来源原文保留，不建立第五个型号profile。身份响应仅为TH2822时无法确定型号，提示型号不明确并禁用仪表操作。

## 身份与采集

- 9600、8N1、无流控；ASCII，发送单LF，接收CR/LF/CRLF及分片/粘包。
- *IDN?必须返回型号、固件、序列号三个非空字段。只按精确型号边界自动识别TH2822A/C/D/E；TH2822EX不能冒认E。未知型号不建立仪表连接，无手动profile。
- 0.3.3仅保留查询采集，连接后自动开始。参数修改后自动读回完整上下文并继续FETCH，无需同步/RMT；详见ACQUISITION.md。
- 每次FETCH前核对上下文；采集间隔不是仪表RATE。设置只执行一次并读回，不重放不确定的setter。
- 查询前的主动测量帧按类型分流，不能混作身份/设置响应。每次响应最多1800ms/32行；同一无副作用query最多一次重试，重试前建立有限静默。setter不重放，失败不盲目回滚。
- RX1024字节、单行191字节、UI队列8项。超长、非法字节、溢出、错误字段、NaN/Inf等使数据无效并显示错误。
- AC三个字段，DCR两个字段；比较字段接受手册NR1有符号整数与参考D固件的N。产品不提供Auto Fetch模式；不猜其他扩展。
- 当前保持仅冻结面板主界面；MIN/MAX/AVG 使用仪表原生 REC 按需读回，不计算本地统计。原生当前值的连续 FETCH 路径仅在实测 E / VER4.5.2307 启用，其他型号/固件采用保守手动读回。无全系列同等吞吐率或固件时序保证。

## 第三方实测参考的边界

[th2822d-cli](https://github.com/LHX369963/th2822d-cli)，MIT，提交e935bd9895c4bb0e667308bae22126edd549e199。已审阅README/LICENSE/protocol/catalog/validation/transport-firmware，未运行其连机脚本。

其TH2822D VER4.5.2307、CP2102 10c4:ea60记录仅属于该项目。800ms setter等待仅匹配该D身份；当前项目实测 E / VER4.5.2307 使用100ms后首次读回，可能因仪表未响应进入后续等待。其他型号/固件使用1200ms保守初值，仍待实测。SLOW丢指令、DTR/HUPCL、副参数重置不能视为全系实测。嵌入式保持USB串口打开，没有POSIX HUPCL。

E10/E11/E12显示在仪表LCD，不虚构SYST:ERR?等错误队列。当前正式固件提供用户触发的原生 TOL 和 REC 操作；不执行仪表 RATE、AUTO LCR、OPEN/SHORT、校准、上电记忆或恢复出厂。TOL 取基准需显式确认，统计选择查询具有显示切换及提示音副作用，见 TOLERANCE.md 和 REC_TIMING.md。

## 原生AUTO LCR调查（2026-10-07）

用户明确要求仅调查原生AUTO，不添加开发板替代算法。D/E V1.0.0手册印刷42–43页明确描述自动选择主副参数与串并联，开启方式为长按面板AUTO；SCPI印刷92页`FUNCtion:impa`合法设置参数仅L/C/R/Z/DCR，查询允许NULL但没有定义NULL表示AUTO。不能推断`FUNC:IMPA AUTO`可用，也不把NULL作为可写模式。

已核对官方产品页的Auto LCR功能说明，以及参考CLI的命令目录和Windows软件对应说明；均未找到可验证的原生AUTO远程入口。官方网页/下载页本次出现403/502，未取得新的协议补充。缺少厂家针对TH2822E VER4.5.2307的AUTO开/关与状态查询指令，以及自动选择L/C/R后读取数据类型/单位的约定。待取得依据后才能实现和实机验证；这一阶段尚未试发猜测指令；用户随后明确授权的实机试探见下文。

## 原生AUTO授权试探与只读对照

在用户明确授权猜测命令后，仅对TH2822E VER4.5.2307做固定候选试验。临时诊断由UART显式触发，在唯一USB工作线程内暂停正常轮询，不含校准、复位或保存命令；每条setter后留5秒观察，再读回与恢复原设置。

| 候选设置 | 串口观察 | 恢复结果 |
|---|---|---|
| `FUNC:IMPA AUTO` | 主参数仍C，无额外响应 | 核对通过 |
| `FUNC:IMPA NULL` | 主参数仍C，无额外响应 | 核对通过 |
| `FUNC:IMPA `（空参数） | 主参数仍C，无额外响应 | 核对通过 |
| `FUNC:AUTO ON` | 主参数仍C，无额外响应 | 核对通过 |
| `FUNC:IMPA:AUTO ON` | 主参数仍C，无额外响应 | 核对通过 |
| `FUNC:MODE AUTO` | 主参数仍C，无额外响应 | 核对通过 |

每次恢复并核对的基线为C/D/PAL/120Hz/1V。用户报告发送候选时有滴声；手册印刷98–99页说明错误命令/参数会终止执行、显示错误码并鸣叫，E10未知命令、E11参数错误、E12语法错误。未记录到逐条LCD码，不能具体归类；无串口响应不能视为执行成功，主参数未变也不能独立证明AUTO开关状态。

用户随后指出LCD同时显示AUTO与C，并明确确认AUTO亮着。更换完全被动的只读诊断固件（USB配置后不自动发IDN/任何SCPI），经用户确认后首先查询FUNC:IMPA?，结果如下：

| 只读查询 | 原始返回 |
|---|---|
| `FUNC:IMPA?` | `C` |
| `FUNC:IMPB?` | `NULL` |
| `FUNC:EQU?` | `PAL` |
| `FREQ?` | `1kHz` |
| `VOLT?` | `0.6V` |
| `FETCh?` | 三字段NR3/NR3/N测量帧 |
| `*IDN?` | TH2822E Handheld LCR Meter / VER4.5.2307 / 非空序列号 |

用户在该组查询结束后再次确认AUTO仍亮。因此本台实证支持“AUTO为自动选择状态，C为当前选中类型”，普通只读查询没有关闭AUTO；不能从FUNC:IMPA?返回C判定AUTO关闭。后续确认AUTO亮着时读回的参数为1kHz/0.6V，这轮没有强制恢复成旧的120Hz/1V。

再试6个独立状态查询：`FUNC:AUTO?`、`FUNC:IMPA:AUTO?`、`FUNC:AUTO:STAT?`、`FUNC:IMPA:MODE?`、`FUNC:MODE?`、`AUTO?`，均在2500ms内未收到响应，每次建立1200ms有限静默后继续。前后FUNC:IMPA?与FETCh?均正常。这只说明本轮未得到可用状态接口，不能证明不存在其他未公开指令。

结论：可以在仪表原本已启用AUTO时读取当前参数和数值，但尚无经过确认的远程AUTO开启/关闭/状态命令。产品没有新增AUTO选项或替代算法。FETCH不携带参数类型，变化中的自动识别读数与类型如何严格对应仍需后续核实，不能把普通参数快照当作独立AUTO状态。正式源码不保留诊断入口，原始日志及临时源仅在本地references/auto-probe与references/auto-readonly，排除Git。


## 本机直连AUTO与RATE排查（2026-10-07）

用户改为TH2822E直接接Mac，授权串口测试并要求联网搜索。仅访问`/dev/cu.usbserial-0001`（CP2102 10c4:ea60），未打开开发板UART或烧录。9600 8N1、无流控、单LF、DTR/RTS保持、关闭HUPCL、独占打开；每组精确核对同一E VER4.5.2307及序列号。

用户确认基线为AUTO亮、SLOW。正常查询返回C/NULL/PAL/1kHz/0.6V和有效三字段FETCH。只读候选每条等待2500ms，前置接收窗口1200ms，记录原始字节和UTC；每组前后IDN及组后FETCH正常。以下18条均无响应，不将其直接等同于LCD E10：

| 组 | 候选查询 |
|---|---|
| 速度1 | `RATE?`、`SPEED?`、`FUNC:RATE?`、`FUNC:SPEED?`、`MEAS:RATE?`、`SENS:RATE?` |
| 速度2 | `FUNC:APER?`、`APER?`、`FUNC:APERture?`、`SENSe:APERture?`、`MEASure:RATE?`、`SENSe:SPEEd?` |
| AUTO新增 | `AUTO:STAT?`、`FUNC:IMPA:AUTO:STAT?`、`FUNC:LCR:AUTO?`、`FUNC:LCR?`、`FUNC:IMPA:SEL?`、`FUNC:IMPA:RANG:AUTO?` |

单独发送`RATE FAST`后停止发令，串口无响应；用户明确观察：仍SLOW、有滴声、LCD E10、AUTO亮。这条设置命令在本台固件上被作为未知命令拒绝，无需继续盲试同一根命令的SLOW/数字参数。随后单独发送`FUNC:RATE FAST`，串口同样无响应；用户再次确认SLOW、E10、AUTO亮。两条速度setter均有本机LCD拒绝证据，最后状态仍AUTO/SLOW，已关闭串口且不再发令。

联网重新核对GitHub最新main仍为e935bd9895c4bb0e667308bae22126edd549e199，公开issues列表为空。其[命令目录](https://github.com/LHX369963/th2822d-cli/blob/main/docs/protocol/catalog.md)无AUTO/RATE入口；[monitor说明](https://github.com/LHX369963/th2822d-cli/blob/main/docs/usage/monitor.md)明确轮询不修改仪表RATE；[Windows对应说明](https://github.com/LHX369963/th2822d-cli/blob/main/docs/windows-parity.md)仅分析LCR Software 2.0.0，不能据此断言其他版本没有隐藏命令。

同惠英文产品/下载页403，但此次中文官网可访问。[TH2822软件搜索页](https://www.tonghui.com.cn/services_software.html?title=TH2822)列出“TH2822上位机软件3.0.1”（条目69）。按页面公开下载按钮请求`POST /wapi/downloadFile`、ptype=3/pid=69/sign=0，返回code40000、success=false、要求登录；没有取得软件文件，未绕过登录、未执行安装包。此为新可追踪线索，不证明3.0.1具备AUTO或速度控制；需用户提供从官网正常下载的原包才能静态检查。

D/E V1.0.0印刷42–44页描述AUTO及FAST/SLOW的面板操作，90–99页完整通信命令和错误码未列它们的远程控制。需要的是补充远程协议，不是缺少面板功能说明。具体缺口：AUTO开/关/状态、FAST/SLOW设置/查询，以及AUTO切换类型时如何与FETCH数据对应。没有找到有效指令前，不在正式固件添加假控制或用轮询速度冒充仪表速度。

原始记录与临时固定候选脚本保存在本地`references/direct-serial/`，不纳入固件或发布包；未做校准、复位、上电记忆或保存设置。本次无产品代码修改，未重新构建或烧录。

## 官方上位机3.0.1包检查

用户已提供官网RAR，完整下载并静态拆解；原先缺少安装包的阻碍已解除。内部主程序含V3.0.1@160617，但PE版本资源仍标2.0.0.0。已提取压缩主数据及主界面，找到常规参数/频率/采集命令，未找到原生AUTO或FAST/SLOW入口；auto错误对话框和Baud Rate不能算作仪表功能证据。未运行软件或向仪表发送新命令。哈希、资源偏移、命令清单与限制见[官方软件静态检查](SOFTWARE_ANALYSIS.md)。后续仍缺厂家明确的原生AUTO/测量速度远程协议，不能把静态无命中当作固件绝无隐藏命令的证明。
