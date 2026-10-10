// main/status_main.c —— 状态牌三合一应用入口。
// 启动流程:BSP 显示/LVGL -> NVS 存储恢复 -> 串口导出(console) -> 按键 -> UI。
// 按键语义:
//   上/下 短按   状态屏:循环切换状态(专注中 -> 离开 -> 可打扰)
//   确定  短按   状态屏:手动打卡
//   确定  长按   状态屏:进入统计屏;统计屏:返回状态屏
#include <string.h>

#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "lvgl.h"

#include "status_export.h"
#include "status_model.h"
#include "status_store.h"
#include "status_ui.h"

static const char *TAG = "status_main";

static status_model_t s_model;
static lv_timer_t *s_ui_timer;

static uint64_t now_ms(void) {
    return (uint64_t)esp_timer_get_time() / 1000ULL;
}

static size_t read_log_cached(status_event_t *out) {
    return status_store_read_log(out, STATUS_LOG_CAPACITY);
}

/* 模型变化后:入日志 + 请求保存 + 刷 UI。在 LVGL 锁内调用。 */
static void on_model_changed(status_event_t *ev, uint64_t t) {
    if (ev && ev->type != 0) {
        status_store_append_event(ev);
    }
    status_store_request_save(&s_model);
    status_event_t log[STATUS_LOG_CAPACITY];
    size_t n = read_log_cached(log);
    status_ui_refresh(&s_model, t, log, n);
}

/* 按键回调运行在 button 组件的任务里,操作模型与 LVGL 都必须加锁。 */
static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user) {
    (void)user;
    if (!bsp_lvgl_lock(500)) return;

    uint64_t t = now_ms();
    bool changed = false;
    status_event_t evt;
    memset(&evt, 0, sizeof(evt));

    if (status_ui_current() == STATUS_UI_STATS) {
        if (btn == BSP_BTN_OK && (ev == BSP_BTN_CLICK || ev == BSP_BTN_LONG)) {
            status_ui_show_main();
            status_event_t log[STATUS_LOG_CAPACITY];
            size_t n = read_log_cached(log);
            status_ui_refresh(&s_model, t, log, n);
        }
        /* 统计页日志只显示最近 12 条,不响应上/下滚动 */
    } else {
        if (ev == BSP_BTN_CLICK) {
            if (btn == BSP_BTN_UP) {
                changed = status_model_cycle(&s_model, -1, t, &evt);
            } else if (btn == BSP_BTN_DOWN) {
                changed = status_model_cycle(&s_model, 1, t, &evt);
            } else if (btn == BSP_BTN_OK) {
                changed = status_model_punch(&s_model, t, &evt);
            }
        } else if (ev == BSP_BTN_LONG && btn == BSP_BTN_OK) {
            status_event_t log[STATUS_LOG_CAPACITY];
            size_t n = read_log_cached(log);
            status_ui_show_stats(&s_model, t, log, n);
        }
    }

    if (changed) on_model_changed(&evt, t);
    bsp_lvgl_unlock();
}

/* LVGL 定时器(LVGL 任务内运行,自带锁):tick 模型 + 刷新时长显示。 */
static void ui_timer_cb(lv_timer_t *timer) {
    (void)timer;
    uint64_t t = now_ms();
    status_model_tick(&s_model, t);
    status_event_t log[STATUS_LOG_CAPACITY];
    size_t n = read_log_cached(log);
    status_ui_refresh(&s_model, t, log, n);
}

void app_main(void) {
    ESP_LOGI(TAG, "Status Pager (三合一状态牌) 启动");

    bsp_i2c_init();

    // 屏幕是 UI 载体,失败就打清楚日志后退出,不做串口降级。
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG,
                 "显示/LVGL 初始化失败,应用无法继续。"
                 "检查 SPI 接线(MOSI=%d SCLK=%d CS=%d DC=%d BL=%d)",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL);
        return;
    }
    bsp_display_backlight(100);

    // 存储恢复;失败时以默认模型继续(数据不同步落盘)。
    if (!status_store_init(&s_model)) {
        ESP_LOGW(TAG, "存储初始化失败,将以默认模型继续,关闭电源后数据丢失");
    }

    // 开机边界事件(存储就绪才记录)
    {
        status_event_t ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_POWER_ON;
        ev.a = (uint8_t)s_model.state;
        ev.t_sec = (uint32_t)(now_ms() / 1000ULL);
        status_store_append_event(&ev);
    }

    // 串口导出:dump / settime / clear(基于 USB-Serial/JTAG console)
    if (!status_export_init(&s_model)) {
        ESP_LOGW(TAG, "console 初始化失败,串口导出命令不可用");
    }

    if (bsp_button_init(on_key, NULL) != ESP_OK) {
        ESP_LOGW(TAG, "按键初始化失败,只能看不能操作");
    }

    if (bsp_lvgl_lock(1000)) {
        status_ui_show_main();
        status_event_t log[STATUS_LOG_CAPACITY];
        size_t n = read_log_cached(log);
        status_ui_refresh(&s_model, now_ms(), log, n);
        s_ui_timer = lv_timer_create(ui_timer_cb, 250, NULL);
        bsp_lvgl_unlock();
    }

    ESP_LOGI(TAG, "就绪:上/下 切换状态,确认 打卡,长按确认 统计");
}