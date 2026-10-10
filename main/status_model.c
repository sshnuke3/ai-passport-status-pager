// main/status_model.c
#include "status_model.h"

#include <stddef.h>
#include <string.h>

static uint64_t sec(uint64_t ms) { return ms / 1000ULL; }

static void emit(status_event_t *out_ev, uint64_t now_ms, uint8_t type,
                 uint8_t a, uint8_t b, uint32_t aux) {
    if (!out_ev) return;
    out_ev->t_sec = (uint32_t)sec(now_ms);
    out_ev->type = type;
    out_ev->a = a;
    out_ev->b = b;
    out_ev->reserved = 0;
    out_ev->aux = aux;
}

/* 离开当前状态:若处于专注中,结算该专注段。离开段的结束由
 * set_state 的 EV_RETURN 分支统一处理(需要读取离开时长)。 */
static void leave_current(status_model_t *model, uint64_t now_ms) {
    if (model->focus_active) {
        uint64_t d = now_ms > model->focused_at_ms ? now_ms - model->focused_at_ms : 0;
        uint32_t dsec = (uint32_t)(d / 1000ULL);
        model->focus_cycle_sec += dsec;
        model->focus_total_sec += dsec;
        model->focus_segments++;
        model->focus_active = false;
        model->focused_at_ms = 0;
    }
}

void status_model_defaults(status_model_t *model) {
    if (!model) return;
    memset(model, 0, sizeof(*model));
    model->version = STATUS_MODEL_VERSION;
    model->state = STATUS_AVAILABLE;
}

void status_model_restore(status_model_t *model, uint64_t now_ms) {
    if (!model) return;
    if (model->version != STATUS_MODEL_VERSION || model->state >= STATUS_COUNT) {
        status_model_defaults(model);
        model->now_ms = now_ms;
        return;
    }
    /* 跨重启:正在进行的专注/离开段重新计时(无 RTC,只能近似)。 */
    model->focus_active = (model->state == STATUS_FOCUS);
    model->away_active = (model->state == STATUS_AWAY);
    model->focused_at_ms = model->focus_active ? now_ms : 0;
    model->away_since_ms = model->away_active ? now_ms : 0;
    model->last_change_ms = now_ms;
    model->now_ms = now_ms;
}

bool status_model_set_state(status_model_t *model, status_t ns, uint64_t now_ms,
                            status_event_t *out_ev) {
    if (!model || ns >= STATUS_COUNT || ns == model->state) return false;
    status_t old = model->state;

    leave_current(model, now_ms);

    if (ns == STATUS_AWAY) {
        model->away_since_ms = now_ms;
        model->away_active = true;
        model->leaves_cycle++;
        model->leaves_total++;
        model->punches_cycle++;
        model->punch_total++;
        emit(out_ev, now_ms, EV_LEAVE, (uint8_t)old, 0, 0);
    } else if (old == STATUS_AWAY) {
        uint64_t away_ms = model->away_active && model->away_since_ms != 0
                               ? (now_ms > model->away_since_ms
                                      ? now_ms - model->away_since_ms
                                      : 0)
                               : 0;
        model->away_active = false;
        model->away_since_ms = 0;
        model->punches_cycle++;
        model->punch_total++;
        emit(out_ev, now_ms, EV_RETURN, (uint8_t)ns, 0, (uint32_t)(away_ms / 1000ULL));
    } else {
        emit(out_ev, now_ms, EV_STATE_CHANGE, (uint8_t)old, (uint8_t)ns, 0);
    }

    if (ns == STATUS_FOCUS) {
        model->focused_at_ms = now_ms;
        model->focus_active = true;
    }

    model->state = ns;
    model->last_change_ms = now_ms;
    model->now_ms = now_ms;
    return true;
}

bool status_model_cycle(status_model_t *model, int dir, uint64_t now_ms,
                        status_event_t *out_ev) {
    if (!model || dir == 0) return false;
    int ns = (int)model->state + (dir > 0 ? 1 : -1);
    if (ns < 0) ns = STATUS_COUNT - 1;
    if (ns >= STATUS_COUNT) ns = 0;
    return status_model_set_state(model, (status_t)ns, now_ms, out_ev);
}

bool status_model_punch(status_model_t *model, uint64_t now_ms,
                        status_event_t *out_ev) {
    if (!model) return false;
    model->punches_cycle++;
    model->punch_total++;
    model->now_ms = now_ms;
    emit(out_ev, now_ms, EV_PUNCH, (uint8_t)model->state, 0, 0);
    return true;
}

void status_model_tick(status_model_t *model, uint64_t now_ms) {
    if (!model) return;
    model->now_ms = now_ms;
}

uint32_t status_model_focus_sec(const status_model_t *model, uint64_t now_ms) {
    if (!model) return 0;
    uint32_t total = model->focus_cycle_sec;
    if (model->state == STATUS_FOCUS && model->focus_active) {
        uint64_t d = now_ms > model->focused_at_ms ? now_ms - model->focused_at_ms : 0;
        total += (uint32_t)(d / 1000ULL);
    }
    return total;
}

void status_model_set_wall(status_model_t *model, int64_t wall_ms) {
    if (!model) return;
    model->wall_offset_ms = wall_ms - (int64_t)model->now_ms;
}

int64_t status_model_wall_ms(const status_model_t *model) {
    if (!model) return 0;
    return (int64_t)model->now_ms + model->wall_offset_ms;
}

const char *status_model_name(status_t s) {
    switch (s) {
    case STATUS_FOCUS:
        return "focus";
    case STATUS_AVAILABLE:
        return "available";
    case STATUS_AWAY:
        return "away";
    default:
        return "?";
    }
}