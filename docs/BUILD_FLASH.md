# 构建、备份和烧录

## 锁定工具链

实际使用 `/Users/liu/esp/v5.4.4/esp-idf`，Git标签v5.4.4；Python环境`/Users/liu/.espressif/python_env/idf5.4_py3.12_env`。IDF Xtensa工具链由本机IDF环境提供，版本信息见 `references/build.log` 与 `build/project_description.json`。所有直接与传递组件版本/内容哈希见 `dependencies.lock`。如重装，安装官方ESP-IDF5.4.4的esp32s3工具链，不需要执行参考CLI脚本。

```sh
export IDF_PYTHON_ENV_PATH=/Users/liu/.espressif/python_env/idf5.4_py3.12_env
. /Users/liu/esp/v5.4.4/esp-idf/export.sh
idf.py build
```

实际配置：ESP32-S3、16MB DIO80MHz flash、octal PSRAM80MHz，UART0控制台，FreeRTOS1kHz，CPU240MHz。`sdkconfig`保留本次完整配置；`sdkconfig.defaults`提供重建默认值。无OTA/联网功能。

## 已授权板卡与串口

用户明确说已接UART Type-C并允许刷入。本次仅使用 `/dev/cu.usbmodem5B901234251`，USB VID:PID `1a86:55d3`、序列号`5B90123425`（WCH USB Single Serial / CH343系列）。ROM工具确认ESP32-S3 rev0.2、8MB嵌入PSRAM、16MB Flash，MAC e8:3d:c1:f7:7f:c0。另一 `/dev/cu.usbmodem010NTXRBB0742` 是 LG 显示器控制，未访问。

每次复用脚本都重新验证串口VID/PID，不能默认同名串口总是同一设备。

## 原镜像保护

原应用描述：`rtc_pcf85063`、version1、2024-08-22 11:20:10、ESP-IDF v5.3-dev-1353-gb3f7e2c8a4。原分区NVS0x9000/0x6000、phy_init0xf000/0x1000、factory0x10000/0x500000。

高速读Flash在921600及460800发生串口噪声，115200稳定。最终完成**将被覆盖范围的1MB备份**，而非整个16MB；之前完整16MB尝试未完成，不将其称完整备份。

- `backups/pre-panel-header.bin`：0x0起64KB，65536字节。
- `backups/pre-panel-overwrite-region-1mb.bin`：0x0起1MB，1048576字节。
- 后者SHA256：`50b4fcd325b0e07e967b7bb53ed2480b43f2108264c552e10d04b8ebe4ec28ca`。
- 两次读出的前64KB完全相同。

首次安装时所有Flash写入（含扇区对齐）在1MB内，该副本只覆盖首次安装范围。0.3.5起已经超出此范围，当前必须使用下一节的1088KB扩展备份。NVS/phy_init地址大小与原表一致；首刷未擦除原NVS。0.2.0起语言选择保存到独立th2822panel/locale键，不擦除整个分区。备份可能包含用户配置，默认留本机、不上传、不提交Git。

## 0.3.5 扩展覆盖区备份

0.3.5 应用 992624 bytes，从0x10000写入，4KB对齐末端为0x103000，超出旧1MB覆盖区。已在首次写入这些额外扇区前读取0x100000起64KB：

- `backups/pre-v0.3.5-extension-100000-10000.bin`：65536 bytes，SHA256 `7d92157f485cb0442638e3a69e456725d0fbeaee9bf28c8254c375d5b5f5c7ba`。
- 与最初原机1MB备份拼接为 `backups/pre-panel-overwrite-region-1088k.bin`：1114112 bytes，SHA256 `4b94501fa7dba1ac0e9d694525b7b17ad5f23e86fe01ccbda9cc2f2e1cd2ff85`。原文件保留不覆盖。
- 若将来用户要求恢复最初覆盖区，须使用这个扩展备份（0x000000–0x10FFFF），仅恢复旧1MB已不足以撤销0.3.5的全部写入。本次未执行恢复。
- 本版仅刷应用0x10000，不写bootloader、分区表或NVS。后续写入的对齐末端超过0x110000前仍须继续扩展备份。

## 常规烧录

只有用户授权且确认当前物理串口后运行。先退出任何占用UART的monitor：

```sh
python -m esptool --chip esp32s3 --port /dev/cu.usbmodem5B901234251 --baud 115200 \
  --before default_reset --after hard_reset write_flash \
  --flash_mode dio --flash_size 16MB --flash_freq 80m \
  0x0 build/bootloader/bootloader.bin \
  0x8000 build/partition_table/partition-table.bin \
  0x10000 build/th2822_panel.bin
```

不使用erase_flash，不写eFuse/安全启动/加密设置。esptool仅擦除覆盖区的必要扇区并校验写入哈希。升级已经运行本项目且分区匹配的开发板时，仅写上面命令的 0x10000 应用镜像，保留引导、分区表和 NVS；首次安装才使用完整三镜像。0.4.9 应用为1012992字节，写入末端按4KB对齐为0x108000，已由现有0x110000字节扩展备份覆盖。后续任一写入越过0x110000前必须继续扩展原始覆盖区备份。备份覆盖范围不是应用分区容量，应同时通过分区大小检查。

运行日志（无仪表命令）：

```sh
python scripts/serial_diagnostics.py --port /dev/cu.usbmodem5B901234251 \
  --seconds 35 --output references/board-runtime.log
```

脚本不主动脉冲reset，但打开CH343串口可能触发板卡复位；仅发`state`；可选locale参数只改界面语言。0.3.0已移除固件模拟、自测虚拟读数入口。实体触摸应人工按屏幕确认。

## 恢复原覆盖区（仅按用户要求）

当前0.4.9的覆盖区已超出最初1MB；恢复本台板卡须使用1088KB扩展备份，前提是之后没有在0x110000以外修改原内容。它可恢复已备份范围的原引导、分区表、原应用前部与NVS，不是完整16MB镜像。此为恢复说明，不自动执行：

```sh
python -m esptool --chip esp32s3 --port VERIFIED_PORT --baud 115200 \
  write_flash 0x0 backups/pre-panel-overwrite-region-1088k.bin
```

恢复前校验备份哈希并确认其对应这块板。禁止把覆盖区备份误当完整16MB镜像用于其他板或整片擦除后的恢复。

## Git跟踪与本地资料

项目使用本地Git管理源码和可复现构建输入。跟踪CMake/Kconfig、sdkconfig（已核查无本机绝对路径）、sdkconfig.defaults、组件清单/依赖锁、语言源、生成的字体子集C源码及OFL许可证、字体来源哈希、主机测试与构建脚本。完整sdkconfig用于保留已实屏验证的配置；修改menuconfig后应检查差异再提交。

构建目录、managed_components、虚拟环境/node_modules、原始字体、references、backups、release与docs/screenshots均保留本地并由.gitignore排除。references中的用户PDF、原厂资料、视频与串口记录不随Git分发；资料来源见HARDWARE.md、SUPPORT.md和THIRD_PARTY_NOTICES.md，字体来源见fonts/SOURCE.json。生成的中间文件和日志不作为源码提交。

此仓库没有配置远程或执行push。重新构建可从锁定组件恢复依赖；普通固件构建直接使用跟踪的字体子集C文件，无需下载原始字体。
