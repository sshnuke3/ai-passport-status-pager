// main/status_model.h
// 状态牌三合一：纯状态机模型(不依赖 ESP-IDF / LVGL,可主机测试)。
//
// 三个状态:专注中 / 离开 / 可打扰。环向循环(方向见 status_model_cycle):
// 专注中 <-> 离开 <-> 可打扰 <-> 专注中。状态切换即打卡:
//   - 进入"离开"   -> EV_LEAVE  (离开打卡)
//   - 离开"离开"   -> EV_RETURN (回来打卡)
//   - 其他状态切换  -> EV_STATE_CHANGE(不增加打卡计数,不稀释打卡含义)
//   - 手动 OK 短按  -> EV_PUNCH
// 专注段统计:处于"专注中"的时间被累计到周期统计与全局累计。
//
// 时间说明:设备离线无 RTC,事件时间戳使用开机后的单调秒(uptime),
// 可通过 SETTIME 注入墙钟偏移,注入后 wall = uptime + offset。
// "周期统计"是自最近一次清零以来的统计(跨重启保留),而非自然日。

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define STATUS_MODEL_VERSION 1

#define STATUS_LOG_CAPACITY 120 /* 环形日志条数,NVS 中每条 16 字节 */

typedef enum {
    STATUS_FOCUS = 0, /* 专注中 */
    STATUS_AWAY,      /* 离开 */
    STATUS_AVAILABLE, /* 可打扰 */
    STATUS_COUNT
} status_t;

typedef enum {
    EV_POWER_ON = 1, /* 开机(记录恢复时仍在离开/专注的边界) */
    EV_STATE_CHANGE, /* 状态切换(非进出离开),a=旧状态 b=新状态 */
    EV_LEAVE,        /* 进入离开:a=离开前状态 */
    EV_RETURN,       /* 离开返回:a=返回后状态,aux=本次离开秒数 */
    EV_PUNCH,        /* 手动打卡:a=打卡时状态 */
} status_ev_type_t;

typedef struct {
    uint32_t t_sec; /* 事件发生的开机秒(uptime) */
    uint8_t type;   /* status_ev_type_t */
    uint8_t a;      /* 事件参数 a */
    uint8_t b;      /* 事件参数 b */
    uint8_t reserved;
    uint32_t aux; /* 附加数据,如离开时长秒 */
} status_event_t; /* 16 字节 */

typedef struct {
    uint16_t version;
    status_t state;
    /* 周期统计(自最近清零以来,跨重启保留) */
    uint32_t focus_cycle_sec;   /* 当前周期专注累计秒 */
    uint16_t punches_cycle;     /* 当前周期打卡次数(离开/回来/手动) */
    uint16_t focus_segments;    /* 当前周期专注段数 */
    uint16_t leaves_cycle;      /* 当前周期离开次数 */
    uint16_t reserved_u16;
    /* 全局累计 */
    uint32_t punch_total;       /* 历史打卡总次数 */
    uint32_t focus_total_sec;   /* 历史专注总秒数 */
    uint32_t leaves_total;      /* 历史离开总次数 */

    /* 运行时字段(不写入 NVS) */
    bool focus_active;       /* 当前是否处于专注段(用标志区分 uptime=0,勿改持久化前布局) */
    bool away_active;        /* 当前是否处于离开段 */
    uint64_t focused_at_ms;  /* 本次专注段开始 uptime,仅 focus_active 时有效 */
    uint64_t away_since_ms;  /* 本次离开开始 uptime,仅 away_active 时有效 */
    uint64_t last_change_ms; /* 上次状态变化 uptime */
    uint64_t now_ms;         /* 最近一次 tick 的 uptime */
    int64_t  wall_offset_ms; /* 墙钟偏移: wall = uptime + offset */
} status_model_t;

void status_model_defaults(status_model_t *model);

/* 从 NVS 读到 model 后调用,修正非法值与跨重启的运行时字段。 */
void status_model_restore(status_model_t *model, uint64_t now_ms);

/* 按方向循环切换状态:dir>0 专注->离开->可打扰->专注;dir<0 逆向。
 * 返回是否确实发生切换;发生切换时会把一条事件写入 out_ev(可空)。 */
bool status_model_cycle(status_model_t *model, int dir, uint64_t now_ms,
                        status_event_t *out_ev);

/* 直接设置状态(供导出/未来场景用),语义与 cycle 相同。 */
bool status_model_set_state(status_model_t *model, status_t ns, uint64_t now_ms,
                            status_event_t *out_ev);

/* 手动打卡,事件写入 out_ev(可空)。 */
bool status_model_punch(status_model_t *model, uint64_t now_ms,
                        status_event_t *out_ev);

/* 每秒由 UI 任务调用,刷新 now_ms,用于实时显示时长。 */
void status_model_tick(status_model_t *model, uint64_t now_ms);

/* 当前周期专注总秒数(含正在进行的专注段)。 */
uint32_t status_model_focus_sec(const status_model_t *model, uint64_t now_ms);

/* 注入墙钟偏移(wall_ms 为调用时刻的墙钟毫秒);下次 tick 生效。 */
void status_model_set_wall(status_model_t *model, int64_t wall_ms);

/* 最近一次已知墙钟(设置过才有效)。 */
int64_t status_model_wall_ms(const status_model_t *model);

/* 字符串显示辅助(值来自本文件,调用方可转成 UI 文案)。 */
const char *status_model_name(status_t s);