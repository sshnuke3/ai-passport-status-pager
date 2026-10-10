// main/status_export.h —— 串口导出:基于 esp_console 的命令行接口。
// 命令:
//   dump        输出当前模型与事件日志(JSON 行)
//   settime <unix_ms>  注入墙钟偏移(设备本身无 RTC,用于让日志时间可读)
//   clear       清空周期统计与事件日志
#pragma once

#include <stdbool.h>

#include "status_model.h"

/* 注册 console 命令并启动 REPL。model 必须在调用期间保持有效。 */
bool status_export_init(status_model_t *model);

/* 供状态变化后立即调用:模型已由调用方更新,此函数只负责把新模型同步进导出上下文。 */
void status_export_sync(const status_model_t *model);