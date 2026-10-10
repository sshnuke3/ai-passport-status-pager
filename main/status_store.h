// main/status_store.h —— 状态牌持久化：模型 blob + 事件环形日志。
// 模式参考 demo/cat-themed-pomodoro-timer 的 pomodoro_store:CRC 校验 +
// 单深度队列异步落盘,避免在按键回调/UI 任务里做 NVS 写。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "status_model.h"

/* 初始化 NVS,加载模型与日志。失败会置位错误标志并给默认模型。 */
bool status_store_init(status_model_t *model);

/* 请求异步保存模型快照(可合并,落盘最终态)。 */
void status_store_request_save(const status_model_t *model);

/* 请求追加一条事件到环形日志(同时更新模型持久化字段并请求保存)。
 * 只在模型已准备好时记录。 */
bool status_store_append_event(const status_event_t *ev);

/* 按时间顺序把日志复制到 out(最多 max 条),返回实际条数。 */
size_t status_store_read_log(status_event_t *out, size_t max);

/* 清空周期统计字段与整个日志,并请求保存。
 * 该操作只影响当前周期与日志,历史全局累计保留。 */
void status_store_clear_cycle(const status_model_t *model);

bool status_store_has_error(void);