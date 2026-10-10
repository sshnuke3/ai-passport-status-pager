<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 状态牌三合一（Status Pager）

面向 AI Passport 穿戴设备的离线专注状态应用：

- **专注 / 可打扰 / 离开三态显示** —— 整屏色带随状态变色（红 / 绿 / 琥珀），
  不碰设备也能远远看清当前状态。
- **打卡与离开/返回签到** —— 短按确认键为当前专注段打卡；切到离开状态时会
  记录离开与返回时刻，并统计本次离开时长。
- **周期统计与事件日志** —— 统计屏展示当前周期的专注时长、打卡次数、离开
  次数与专注段数，以及最近事件；数据全部保存在本地 NVS。

应用完全离线运行，不依赖 Wi-Fi、BLE 或云账号，本版本也不需要手机配对方
案。数据可通过 USB-Serial-JTAG 控制台导出，供后续配套 App 同步。

## 功能一览

| 状态 | 色带 | 含义 |
| --- | --- | --- |
| 专注中（FOCUS） | 红 | 正在进行专注时段 |
| 离开（AWAY） | 琥珀 | 人已离开，自动记录离开/返回时间 |
| 可打扰（AVAILABLE） | 绿 | 可被打扰，显示周期累计专注时长 |

- 250 ms 界面刷新，分段时长与周期汇总实时更新。
- 本地事件日志保留最近 120 条，界面展示最新 12 条。
- 上电后自动从 NVS 恢复上次保存的状态与计数。
- 控制台提供模型导出、墙钟参考时间设置与周期清零命令（见下）。

## 硬件与构建前置条件

目标设备为 FoloToy AI Passport（ESP32-C3，8 MB Flash，无 PSRAM）。需要：

- 已激活的 ESP-IDF 5.5.3 环境
- `lv_font_conv` 1.5.3 在 `PATH` 中（仅在重新生成字库时需要）
- `assets/fonts/gen_fonts.sh` 用于重新生成内嵌中文字库子集；构建前必须存在
  `app_font_32.c` / `app_font_16.c` 两个产物

构建：

```bash
idf.py build
```

生成固件沿用仓库默认的最小分区表。

## 烧录与运行

```bash
idf.py -p /dev/ttyACM0 flash monitor
```

使用支持数据传输的 USB 线连接设备，上电即进入主状态屏。

## 按键操作

| 操作 | 效果 |
| --- | --- |
| 上 / 下 短按 | 循环切换状态：专注中 → 可打扰 → 离开 → 专注中 |
| 确认 短按 | 为当前专注段打卡 |
| 确认 长按 | 在状态屏与统计屏之间切换 |

按键回调保持非阻塞；所有界面更新都在 BSP LVGL 锁内完成。

## 控制台导出

USB-Serial-JTAG 控制台提供三条命令（每条响应输出一行 JSON）：

| 命令 | 用途 |
| --- | --- |
| `dump` | 以 JSON 输出当前模型、周期汇总与全部缓存事件 |
| `settime <epoch_sec>` | 设置墙钟参考时间，使日志显示 `HH:MM` 时间戳 |
| `clear` | 清空当前周期计数与事件日志 |

日志较长时 `dump` 会每行输出一条事件，首行为模型汇总。示例：

```text
> dump
{"model":{"state":"focus",...},"summary":{...}}
{"event":{"type":"punch","t_sec":123,...}}
```

## 源码结构

| 文件 | 用途 |
| --- | --- |
| `status_model.c/h` | 纯状态机与周期统计，独立于 ESP-IDF；由主机测试覆盖 |
| `status_store.c/h` | NVS 持久化：模型 blob + CRC、120 条环形事件日志、异步保存 worker |
| `status_export.c/h` | USB 控制台 REPL：`dump` / `settime` / `clear` 命令 |
| `status_ui.c/h` | LVGL 状态屏与统计屏，使用 32/16 px 中文子集字库 |
| `status_main.c` | `app_main` 入口：板级初始化、按键、UI 定时器与装配 |

开发中可运行聚焦主机测试：

```bash
cc -std=c11 -Wall -Wextra -Werror -Imain tests/test_status_model.c main/status_model.c -o /tmp/test_status_model && /tmp/test_status_model
```

## 扩展

模型/存储层刻意保持与 ESP-IDF 解耦：新增状态迁移或计数时先在
`status_model.c` 实现并用主机测试覆盖，再在界面或控制台暴露。后续配套 App
或 BLE 同步可直接消费 `dump` 输出，无需改动状态机。

新增中文界面文案前请先阅读
`docs/development/engineering/lvgl-chinese-fonts.md`：内嵌字库是刻意保持的精简
子集。