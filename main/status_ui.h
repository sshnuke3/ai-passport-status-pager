// main/status_ui.h —— 状态牌三合一 UI(全新设计,不依赖 demo 菜单外壳)。
// 两个视图:
//   STATUS_UI_MAIN   状态屏:全宽状态色带 + 大字状态 + 分段时长 + 打卡/离开
//   STATUS_UI_STATS  统计屏:周期汇总 + 最近事件日志
// 所有函数必须在持有 bsp_lvgl_lock() 时调用(LVGL 非线程安全)。
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"
#include "status_model.h"

typedef enum {
    STATUS_UI_MAIN = 0,
    STATUS_UI_STATS,
} status_ui_view_t;

/* 创建并加载状态屏(主视图)。 */
void status_ui_show_main(void);

/* 创建并加载统计屏。 */
void status_ui_show_stats(const status_model_t *model, uint64_t now_ms,
                          const status_event_t *log, size_t log_count);

/* 当前视图。 */
status_ui_view_t status_ui_current(void);

/* 按当前视图刷新(状态变化/每秒 tick 后调用)。 */
void status_ui_refresh(const status_model_t *model, uint64_t now_ms,
                       const status_event_t *log, size_t log_count);

/* 删除当前屏幕并释放对象(退出前调用)。 */
void status_ui_delete(void);