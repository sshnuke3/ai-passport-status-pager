// main/status_ui.c —— 状态牌三合一 UI。
// 状态屏:顶部全宽状态色带(可打扰=绿 / 专注中=红 / 离开=琥珀),
// 中间显示当前分段时长(专注中/离开 或 周期专注),底部打卡/离开 chips 与按键提示。
// 统计屏:周期汇总 4 行 + 最近事件日志。
#include "status_ui.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define COLOR_BG        0x050608
#define COLOR_INK       0xF4F1E8
#define COLOR_DIM       0x777C85
#define COLOR_TRACK     0x25282E
#define COLOR_FOCUS     0xF02B24
#define COLOR_AVAILABLE 0x36D765
#define COLOR_AWAY      0xFFB020
#define COLOR_AMBER_INK 0x1D1A16

LV_FONT_DECLARE(app_font_32);
LV_FONT_DECLARE(app_font_16);

/* 主视图对象 */
static lv_obj_t *s_main_scr;
static lv_obj_t *s_band;
static lv_obj_t *s_state_cn;
static lv_obj_t *s_state_en;
static lv_obj_t *s_dur_cap;
static lv_obj_t *s_dur_val;
static lv_obj_t *s_chip_punch;
static lv_obj_t *s_chip_leave;

/* 统计视图对象 */
static lv_obj_t *s_stats_scr;
static lv_obj_t *s_row_focus;
static lv_obj_t *s_row_punch;
static lv_obj_t *s_row_leave;
static lv_obj_t *s_row_segments;
static lv_obj_t *s_log_label;

static status_ui_view_t s_view = STATUS_UI_MAIN;

static lv_obj_t *container(lv_obj_t *parent, int x, int y, int w, int h) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t color) {
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    return obj;
}

static void center_width(lv_obj_t *obj, int y) {
    lv_obj_set_width(obj, 240);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(obj, 0, y);
}

static void fmt_hms(char *buf, size_t len, uint32_t sec) {
    uint32_t h = sec / 3600U;
    uint32_t m = (sec % 3600U) / 60U;
    uint32_t s = sec % 60U;
    if (h > 0) {
        snprintf(buf, len, "%u:%02u:%02u", (unsigned)h, (unsigned)m,
                 (unsigned)s);
    } else {
        snprintf(buf, len, "%02u:%02u", (unsigned)m, (unsigned)s);
    }
}

static void fmt_dur_zh(char *buf, size_t len, uint32_t sec) {
    if (sec < 60U) {
        snprintf(buf, len, "%u秒", (unsigned)sec);
    } else if (sec < 3600U) {
        snprintf(buf, len, "%u分", (unsigned)(sec / 60U));
    } else {
        snprintf(buf, len, "%u时%u分", (unsigned)(sec / 3600U),
                 (unsigned)((sec % 3600U) / 60U));
    }
}

static const char *ev_type_cn(uint8_t t) {
    switch (t) {
    case EV_POWER_ON:
        return "开机";
    case EV_STATE_CHANGE:
        return "切换";
    case EV_LEAVE:
        return "离开";
    case EV_RETURN:
        return "返回";
    case EV_PUNCH:
        return "打卡";
    default:
        return "?";
    }
}

static void build_log_text(const status_event_t *log, size_t log_count,
                           int64_t wall_offset_ms, bool have_wall,
                           char *out, size_t out_len) {
    if (log_count == 0) {
        snprintf(out, out_len, "暂无记录");
        return;
    }
    /* 取最近 12 条,最新在前。 */
    size_t n = log_count > 12 ? 12 : log_count;
    size_t start = log_count - n;
    size_t used = 0;
    out[0] = '\0';
    for (size_t k = 0; k < n; k++) {
        const status_event_t *e = &log[start + k];
        char line[64];
        if (have_wall) {
            time_t wall_sec =
                (time_t)(((int64_t)e->t_sec * 1000LL + wall_offset_ms) / 1000LL);
            struct tm tm;
            gmtime_r(&wall_sec, &tm);
            if (e->type == EV_RETURN) {
                char dur[24];
                fmt_dur_zh(dur, sizeof(dur), e->aux);
                snprintf(line, sizeof(line), "%02d:%02d %s %s\n", tm.tm_hour,
                         tm.tm_min, ev_type_cn(e->type), dur);
            } else {
                snprintf(line, sizeof(line), "%02d:%02d %s\n", tm.tm_hour,
                         tm.tm_min, ev_type_cn(e->type));
            }
        } else {
            if (e->type == EV_RETURN) {
                char dur[24];
                fmt_dur_zh(dur, sizeof(dur), e->aux);
                snprintf(line, sizeof(line), "T+%lu %s %s\n", (unsigned long)e->t_sec,
                         ev_type_cn(e->type), dur);
            } else {
                snprintf(line, sizeof(line), "T+%lu %s\n", (unsigned long)e->t_sec,
                         ev_type_cn(e->type));
            }
        }
        size_t room = out_len - used;
        size_t line_len = strlen(line);
        if (room <= line_len) break;
        memcpy(out + used, line, line_len + 1);
        used += line_len;
    }
}

/* ---------- 主视图 ---------- */

static void refresh_main(const status_model_t *model, uint64_t now_ms) {
    if (s_view != STATUS_UI_MAIN || !s_main_scr) return;

    const char *cn;
    const char *en;
    uint32_t band;
    uint32_t ink = COLOR_INK;
    const char *cap;
    uint64_t dur_ms = 0;

    switch (model->state) {
    case STATUS_FOCUS:
        cn = "专注中";
        en = "FOCUS";
        band = COLOR_FOCUS;
        cap = "当前专注";
        if (model->focus_active && now_ms > model->focused_at_ms) {
            dur_ms = now_ms - model->focused_at_ms;
        }
        break;
    case STATUS_AWAY:
        cn = "离开";
        en = "AWAY";
        band = COLOR_AWAY;
        ink = COLOR_AMBER_INK;
        cap = "已离开";
        if (model->away_active && now_ms > model->away_since_ms) {
            dur_ms = now_ms - model->away_since_ms;
        }
        break;
    default:
        cn = "可打扰";
        en = "AVAILABLE";
        band = COLOR_AVAILABLE;
        cap = "周期专注";
        dur_ms = (uint64_t)status_model_focus_sec(model, now_ms) * 1000ULL;
        break;
    }

    lv_obj_set_style_bg_color(s_band, lv_color_hex(band), 0);
    lv_label_set_text(s_state_cn, cn);
    lv_label_set_text(s_state_en, en);
    lv_obj_set_style_text_color(s_state_cn, lv_color_hex(ink), 0);
    lv_obj_set_style_text_color(s_state_en, lv_color_hex(ink), 0);
    lv_label_set_text(s_dur_cap, cap);

    char buf[24];
    fmt_hms(buf, sizeof(buf), (uint32_t)(dur_ms / 1000ULL));
    lv_label_set_text(s_dur_val, buf);

    lv_label_set_text_fmt(s_chip_punch, "打卡 %u", model->punches_cycle);
    lv_label_set_text_fmt(s_chip_leave, "离开 %u", model->leaves_cycle);
}

static void main_build(void) {
    s_main_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_main_scr, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_main_scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_main_scr, 0, 0);
    lv_obj_set_style_pad_all(s_main_scr, 0, 0);
    lv_obj_clear_flag(s_main_scr, LV_OBJ_FLAG_SCROLLABLE);

    /* 顶部全宽状态色带 */
    s_band = container(s_main_scr, 0, 0, 240, 116);
    lv_obj_set_style_bg_color(s_band, lv_color_hex(COLOR_AVAILABLE), 0);
    lv_obj_set_style_bg_opa(s_band, LV_OPA_COVER, 0);

    s_state_cn = label(s_band, &app_font_32, COLOR_INK);
    lv_obj_set_width(s_state_cn, 240);
    lv_obj_set_style_text_align(s_state_cn, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_state_cn, 0, 22);

    s_state_en = label(s_band, &app_font_16, COLOR_INK);
    lv_obj_set_width(s_state_en, 240);
    lv_obj_set_style_text_align(s_state_en, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_state_en, 0, 78);

    /* 中部:分段时长 */
    s_dur_cap = label(s_main_scr, &app_font_16, COLOR_DIM);
    center_width(s_dur_cap, 138);

    s_dur_val = label(s_main_scr, &app_font_32, COLOR_INK);
    center_width(s_dur_val, 162);

    /* 底部:打卡/离开 chips 与按键提示 */
    s_chip_punch = label(s_main_scr, &app_font_16, COLOR_INK);
    lv_obj_set_pos(s_chip_punch, 20, 236);
    s_chip_leave = label(s_main_scr, &app_font_16, COLOR_INK);
    lv_obj_set_pos(s_chip_leave, 150, 236);

    lv_obj_t *hint1 = label(s_main_scr, &app_font_16, COLOR_DIM);
    center_width(hint1, 274);
    lv_label_set_text(hint1, "上/下 切换 · 确认 打卡");

    lv_obj_t *hint2 = label(s_main_scr, &app_font_16, COLOR_DIM);
    center_width(hint2, 298);
    lv_label_set_text(hint2, "长按确认 查看统计");

    lv_screen_load(s_main_scr);
}

/* ---------- 统计视图 ---------- */

static void refresh_stats(const status_model_t *model, int64_t wall_offset_ms,
                          bool have_wall, const status_event_t *log,
                          size_t log_count) {
    if (s_view != STATUS_UI_STATS || !s_stats_scr) return;

    char buf[48];
    fmt_hms(buf, sizeof(buf), model->focus_cycle_sec);
    lv_label_set_text_fmt(s_row_focus, "专注时长  %s", buf);
    lv_label_set_text_fmt(s_row_punch, "打卡次数  %u", model->punches_cycle);
    lv_label_set_text_fmt(s_row_leave, "离开次数  %u", model->leaves_cycle);
    lv_label_set_text_fmt(s_row_segments, "专注段数  %u", model->focus_segments);

    static char log_buf[640];
    build_log_text(log, log_count, wall_offset_ms, have_wall, log_buf,
                   sizeof(log_buf));
    lv_label_set_text(s_log_label, log_buf);
}

static void stats_build(const status_model_t *model, uint64_t now_ms,
                        const status_event_t *log, size_t log_count) {
    (void)now_ms;
    s_stats_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_stats_scr, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_stats_scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_stats_scr, 0, 0);
    lv_obj_set_style_pad_all(s_stats_scr, 0, 0);
    lv_obj_clear_flag(s_stats_scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = label(s_stats_scr, &app_font_16, COLOR_INK);
    center_width(title, 16);
    lv_label_set_text(title, "周期统计");

    s_row_focus = label(s_stats_scr, &app_font_16, COLOR_INK);
    lv_obj_set_pos(s_row_focus, 20, 52);
    s_row_punch = label(s_stats_scr, &app_font_16, COLOR_INK);
    lv_obj_set_pos(s_row_punch, 20, 78);
    s_row_leave = label(s_stats_scr, &app_font_16, COLOR_INK);
    lv_obj_set_pos(s_row_leave, 20, 104);
    s_row_segments = label(s_stats_scr, &app_font_16, COLOR_INK);
    lv_obj_set_pos(s_row_segments, 20, 130);

    lv_obj_t *div = container(s_stats_scr, 12, 158, 216, 2);
    lv_obj_set_style_bg_color(div, lv_color_hex(COLOR_TRACK), 0);
    lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);

    lv_obj_t *log_title = label(s_stats_scr, &app_font_16, COLOR_DIM);
    lv_obj_set_pos(log_title, 20, 172);
    lv_label_set_text(log_title, "最近事件");

    s_log_label = label(s_stats_scr, &app_font_16, COLOR_INK);
    lv_obj_set_pos(s_log_label, 20, 198);
    lv_obj_set_width(s_log_label, 200);
    lv_obj_set_style_text_line_space(s_log_label, 4, 0);

    lv_obj_t *hint = label(s_stats_scr, &app_font_16, COLOR_DIM);
    center_width(hint, 298);
    lv_label_set_text(hint, "确认 返回");

    refresh_stats(model, model->wall_offset_ms, model->wall_offset_ms != 0, log,
                  log_count);
    lv_screen_load(s_stats_scr);
}

/* ---------- 对外接口 ---------- */

void status_ui_show_main(void) {
    if (s_stats_scr) {
        lv_obj_delete(s_stats_scr);
        s_stats_scr = NULL;
    }
    s_row_focus = s_row_punch = s_row_leave = s_row_segments = NULL;
    s_log_label = NULL;
    if (!s_main_scr) main_build();
    s_view = STATUS_UI_MAIN;
    lv_screen_load(s_main_scr);
}

void status_ui_show_stats(const status_model_t *model, uint64_t now_ms,
                          const status_event_t *log, size_t log_count) {
    if (s_main_scr) {
        lv_obj_delete(s_main_scr);
        s_main_scr = NULL;
    }
    s_band = s_state_cn = s_state_en = NULL;
    s_dur_cap = s_dur_val = NULL;
    s_chip_punch = s_chip_leave = NULL;
    if (!s_stats_scr) {
        stats_build(model, now_ms, log, log_count);
    }
    s_view = STATUS_UI_STATS;
}

status_ui_view_t status_ui_current(void) { return s_view; }

void status_ui_refresh(const status_model_t *model, uint64_t now_ms,
                       const status_event_t *log, size_t log_count) {
    if (s_view == STATUS_UI_MAIN) {
        refresh_main(model, now_ms);
    } else {
        refresh_stats(model, model->wall_offset_ms, model->wall_offset_ms != 0,
                      log, log_count);
    }
}

void status_ui_delete(void) {
    if (s_main_scr) {
        lv_obj_delete(s_main_scr);
        s_main_scr = NULL;
    }
    if (s_stats_scr) {
        lv_obj_delete(s_stats_scr);
        s_stats_scr = NULL;
    }
    s_band = s_state_cn = s_state_en = NULL;
    s_dur_cap = s_dur_val = NULL;
    s_chip_punch = s_chip_leave = NULL;
    s_row_focus = s_row_punch = s_row_leave = s_row_segments = NULL;
    s_log_label = NULL;
}