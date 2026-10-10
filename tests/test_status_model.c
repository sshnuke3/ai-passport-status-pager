// tests/test_status_model.c —— 状态牌核心模型主机测试
// 编译: cc -std=c11 -Wall -Wextra -Werror -Imain
//       tests/test_status_model.c main/status_model.c -o /tmp/test_status_model
#include "status_model.h"

#include <assert.h>
#include <stdio.h>

static void test_defaults(void) {
    status_model_t m;
    status_model_defaults(&m);
    assert(m.version == STATUS_MODEL_VERSION);
    assert(m.state == STATUS_AVAILABLE);
    assert(m.focus_cycle_sec == 0);
    assert(m.punch_total == 0);
    assert(m.focus_segments == 0);
}

static void test_cycle_wraps_forward_and_backward(void) {
    status_model_t m;
    status_model_defaults(&m);
    status_event_t ev;
    uint64_t now = 1000;

    /* AVAILABLE -> FOCUS */
    assert(status_model_cycle(&m, 1, now, &ev));
    assert(m.state == STATUS_FOCUS);
    assert(ev.type == EV_STATE_CHANGE);
    assert(ev.a == STATUS_AVAILABLE && ev.b == STATUS_FOCUS);
    /* FOCUS -> AWAY */
    assert(status_model_cycle(&m, 1, now + 1000, &ev));
    assert(m.state == STATUS_AWAY);
    assert(ev.type == EV_LEAVE);
    assert(m.leaves_cycle == 1 && m.punches_cycle == 1);
    /* AWAY -> AVAILABLE (回来打卡) */
    assert(status_model_cycle(&m, 1, now + 2000, &ev));
    assert(m.state == STATUS_AVAILABLE);
    assert(ev.type == EV_RETURN);
    assert(ev.aux == 1); /* 离开 1 秒 */
    assert(m.punches_cycle == 2);
    /* 逆向 AVAILABLE -> AWAY -> FOCUS -> AVAILABLE */
    assert(status_model_cycle(&m, -1, now + 3000, &ev));
    assert(m.state == STATUS_AWAY);
    assert(ev.type == EV_LEAVE);
    assert(status_model_cycle(&m, -1, now + 4000, &ev));
    assert(m.state == STATUS_FOCUS);
    assert(status_model_cycle(&m, -1, now + 5000, &ev));
    assert(m.state == STATUS_AVAILABLE);
    assert(ev.type == EV_STATE_CHANGE);
}

static void test_focus_accumulates_only_while_focusing(void) {
    status_model_t m;
    status_model_defaults(&m);
    status_event_t ev;
    uint64_t now = 0;

    assert(status_model_set_state(&m, STATUS_FOCUS, now, &ev));
    /* 专注 65 秒 */
    assert(status_model_set_state(&m, STATUS_AVAILABLE, now + 65000, &ev));
    assert(ev.type == EV_STATE_CHANGE);
    assert(m.focus_cycle_sec == 65);
    assert(m.focus_total_sec == 65);
    assert(m.focus_segments == 1);

    /* 可打扰期间不计专注 */
    assert(status_model_set_state(&m, STATUS_FOCUS, now + 100000, &ev));
    assert(status_model_set_state(&m, STATUS_AWAY, now + 110000, &ev));
    assert(m.focus_cycle_sec == 75);
    assert(m.focus_segments == 2);
}

static void test_focus_sec_live_count(void) {
    status_model_t m;
    status_model_defaults(&m);
    status_event_t ev;
    uint64_t now = 0;

    assert(status_model_set_state(&m, STATUS_FOCUS, now, &ev));
    assert(status_model_focus_sec(&m, now + 3000) == 3);
    assert(status_model_focus_sec(&m, now + 30500) == 30);
    /* 离开专注后冻结 */
    assert(status_model_set_state(&m, STATUS_AVAILABLE, now + 30500, &ev));
    assert(status_model_focus_sec(&m, now + 90500) == 30);
}

static void test_punch_counts(void) {
    status_model_t m;
    status_model_defaults(&m);
    status_event_t ev;
    uint64_t now = 0;

    assert(status_model_punch(&m, now, &ev));
    assert(ev.type == EV_PUNCH);
    assert(ev.a == STATUS_AVAILABLE);
    assert(m.punches_cycle == 1 && m.punch_total == 1);

    assert(status_model_set_state(&m, STATUS_AWAY, now + 1000, &ev));
    assert(status_model_set_state(&m, STATUS_FOCUS, now + 5000, &ev));
    /* 离开打卡 + 回来打卡 + 手动打卡 */
    assert(m.punches_cycle == 3 && m.punch_total == 3);
    assert(m.leaves_cycle == 1 && m.leaves_total == 1);
}

static void test_restore_normalizes(void) {
    status_model_t m;
    status_model_defaults(&m);
    m.version = 999; /* 非法版本 -> 重置 */
    status_model_restore(&m, 1000);
    assert(m.version == STATUS_MODEL_VERSION);
    assert(m.state == STATUS_AVAILABLE);

    status_model_defaults(&m);
    m.state = STATUS_AWAY;
    status_model_restore(&m, 5000);
    assert(m.state == STATUS_AWAY);
    assert(m.away_since_ms == 5000); /* 跨重启重新计时 */
    assert(m.focused_at_ms == 0);

    status_model_defaults(&m);
    m.state = STATUS_FOCUS;
    m.focus_cycle_sec = 123;
    status_model_restore(&m, 7000);
    assert(m.state == STATUS_FOCUS);
    assert(m.focused_at_ms == 7000);
    assert(m.focus_cycle_sec == 123); /* 保留持久化统计 */
}

static void test_wall_clock(void) {
    status_model_t m;
    status_model_defaults(&m);
    uint64_t now = 100000;

    status_model_tick(&m, now);
    int64_t wall = (int64_t)now + 1700000000000LL;
    status_model_set_wall(&m, wall);
    assert(status_model_wall_ms(&m) == wall);
}

int main(void) {
    test_defaults();
    test_cycle_wraps_forward_and_backward();
    test_focus_accumulates_only_while_focusing();
    test_focus_sec_live_count();
    test_punch_counts();
    test_restore_normalizes();
    test_wall_clock();
    printf("test_status_model: ALL PASS\n");
    return 0;
}