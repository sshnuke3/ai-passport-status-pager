// main/status_export.c —— 串口导出:基于 esp_console 的命令行接口。
// 命令:
//   dump                   输出当前模型与事件日志(JSON 行)
//   settime <unix_ms>      注入墙钟偏移(设备本身无 RTC,用于让日志时间可读)
//   clear                  清空周期统计与事件日志
//
// 设备默认把 USB-Serial/JTAG 作为 console,因此这些命令通过 USB 串口输入。
#include "status_export.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "bsp_display.h"
#include "esp_console.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "status_store.h"

static const char *TAG = "status_export";

/* 导出上下文持有模型指针:模型由 main 创建并保持有效。 */
static status_model_t *s_model;

static const char *ev_type_name(uint8_t t) {
    switch (t) {
    case EV_POWER_ON:
        return "power_on";
    case EV_STATE_CHANGE:
        return "state_change";
    case EV_LEAVE:
        return "leave";
    case EV_RETURN:
        return "return";
    case EV_PUNCH:
        return "punch";
    default:
        return "?";
    }
}

/* 若已注入墙钟会返回 UTC 时间串,否则返回 uptime 秒。 */
static void print_event_time(const status_event_t *ev, int64_t wall_offset_ms,
                             bool have_wall) {
    if (have_wall) {
        time_t wall_sec = (time_t)((int64_t)ev->t_sec * 1000LL + wall_offset_ms) / 1000;
        struct tm tm;
        gmtime_r(&wall_sec, &tm);
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
        printf("\"wall\":\"%s\",", buf);
    }
    printf("\"uptime_sec\":%" PRIu32, ev->t_sec);
}

static int cmd_dump(int argc, char **argv) {
    (void)argc;
    (void)argv;
    if (!bsp_lvgl_lock(500)) {
        printf("{\"cmd\":\"dump\",\"error\":\"locked\"}\n");
        return 1;
    }
    if (!s_model) {
        bsp_lvgl_unlock();
        printf("{\"cmd\":\"dump\",\"error\":\"no model\"}\n");
        return 1;
    }
    const status_model_t *m = s_model;
    const bool have_wall = (m->wall_offset_ms != 0);

    printf("{\"cmd\":\"model\",\"state\":\"%s\",\"state_id\":%d,"
           "\"focus_cycle_sec\":%" PRIu32 ",\"punches_cycle\":%u,"
           "\"focus_segments\":%u,\"leaves_cycle\":%u,"
           "\"punch_total\":%" PRIu32 ",\"focus_total_sec\":%" PRIu32 ","
           "\"leaves_total\":%" PRIu32 ",\"have_wall\":%s}\n",
           status_model_name(m->state), (int)m->state, m->focus_cycle_sec,
           m->punches_cycle, m->focus_segments, m->leaves_cycle, m->punch_total,
           m->focus_total_sec, m->leaves_total, have_wall ? "true" : "false");

    status_event_t evs[STATUS_LOG_CAPACITY];
    size_t n = status_store_read_log(evs, STATUS_LOG_CAPACITY);
    for (size_t i = 0; i < n; i++) {
        const status_event_t *e = &evs[i];
        printf("{\"cmd\":\"event\",\"seq\":%zu,\"type\":\"%s\",\"a\":%u,\"b\":%u,"
               "\"aux\":%" PRIu32 ",",
               i, ev_type_name(e->type), e->a, e->b, e->aux);
        print_event_time(e, m->wall_offset_ms, have_wall);
        printf("}\n");
    }
    bsp_lvgl_unlock();
    return 0;
}

static int cmd_settime(int argc, char **argv) {
    if (!bsp_lvgl_lock(500)) {
        printf("{\"cmd\":\"settime\",\"error\":\"locked\"}\n");
        return 1;
    }
    if (!s_model) {
        bsp_lvgl_unlock();
        printf("{\"cmd\":\"settime\",\"error\":\"no model\"}\n");
        return 1;
    }
    if (argc < 2) {
        bsp_lvgl_unlock();
        printf("usage: settime <unix_ms>\n");
        return 1;
    }
    char *end = NULL;
    long long ms = strtoll(argv[1], &end, 10);
    if (end == argv[1] || *end != '\0') {
        bsp_lvgl_unlock();
        printf("{\"cmd\":\"settime\",\"error\":\"invalid unix_ms\"}\n");
        return 1;
    }
    /* 先刷新 now_ms,再注入偏移,保证 wall = 调用时刻墙钟。 */
    uint64_t now_ms = (uint64_t)esp_timer_get_time() / 1000ULL;
    status_model_tick(s_model, now_ms);
    status_model_set_wall(s_model, (int64_t)ms);
    printf("{\"cmd\":\"settime\",\"ok\":true,\"wall_ms\":%" PRId64 "}\n",
           status_model_wall_ms(s_model));
    bsp_lvgl_unlock();
    return 0;
}

static int cmd_clear(int argc, char **argv) {
    (void)argc;
    (void)argv;
    if (!bsp_lvgl_lock(500)) {
        printf("{\"cmd\":\"clear\",\"error\":\"locked\"}\n");
        return 1;
    }
    if (!s_model) {
        bsp_lvgl_unlock();
        printf("{\"cmd\":\"clear\",\"error\":\"no model\"}\n");
        return 1;
    }
    /* 先清零内存模型周期字段,再让 store 落盘清零后的模型并清空日志。
     * 全局累计(punch_total/focus_total_sec/leaves_total)保持保留。 */
    s_model->focus_cycle_sec = 0;
    s_model->punches_cycle = 0;
    s_model->focus_segments = 0;
    s_model->leaves_cycle = 0;
    status_store_clear_cycle(s_model);
    printf("{\"cmd\":\"clear\",\"ok\":true}\n");
    bsp_lvgl_unlock();
    return 0;
}

bool status_export_init(status_model_t *model) {
    if (!model) return false;
    s_model = model;

    esp_console_repl_config_t repl_config = {
        .max_history_len = 8,
        .history_save_path = NULL,
        .task_stack_size = 4096,
        .task_priority = 2,
        .task_core_id = tskNO_AFFINITY,
        .prompt = "status>",
        .max_cmdline_length = 0,
    };
    esp_console_dev_usb_serial_jtag_config_t dev_config =
        ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();

    esp_console_repl_t *repl = NULL;
    esp_err_t err = esp_console_new_repl_usb_serial_jtag(&dev_config, &repl_config,
                                                         &repl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "REPL init failed: %s", esp_err_to_name(err));
        return false;
    }

    esp_console_register_help_command();

    const esp_console_cmd_t dump_cmd = {
        .command = "dump",
        .help = "dump model and event log as JSON lines",
        .hint = NULL,
        .func = &cmd_dump,
    };
    const esp_console_cmd_t settime_cmd = {
        .command = "settime",
        .help = "set wall clock offset: settime <unix_ms>",
        .hint = "<unix_ms>",
        .func = &cmd_settime,
    };
    const esp_console_cmd_t clear_cmd = {
        .command = "clear",
        .help = "clear cycle stats and event log",
        .hint = NULL,
        .func = &cmd_clear,
    };
    err = esp_console_cmd_register(&dump_cmd);
    if (err != ESP_OK) ESP_LOGW(TAG, "register dump failed: %s", esp_err_to_name(err));
    err = esp_console_cmd_register(&settime_cmd);
    if (err != ESP_OK) ESP_LOGW(TAG, "register settime failed: %s", esp_err_to_name(err));
    err = esp_console_cmd_register(&clear_cmd);
    if (err != ESP_OK) ESP_LOGW(TAG, "register clear failed: %s", esp_err_to_name(err));

    err = esp_console_start_repl(repl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "REPL start failed: %s", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "Console ready: try 'dump', 'settime <unix_ms>', 'clear'");
    return true;
}

void status_export_sync(const status_model_t *model) {
    /* 模型由 main 持有,导出上下文始终引用同一实例,此函数仅为 API 兼容。 */
    (void)model;
}