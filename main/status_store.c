// main/status_store.c
#include "status_store.h"

#include <stddef.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#define STORE_MAGIC 0x53544154U /* "STAT" */
#define STORE_VERSION 1

/* 模型 blob:只持久化 status_model_t 的前半段(version..leaves_total)。
 * 运行时字段(now_ms 等)跨重启无意义,由 status_model_restore 重新初始化。 */
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint16_t reserved0; /* 与 status_model_t 前 8 字节对齐一致 */
    status_t state;
    uint32_t focus_cycle_sec;
    uint16_t punches_cycle;
    uint16_t focus_segments;
    uint16_t leaves_cycle;
    uint16_t reserved1;
    uint32_t punch_total;
    uint32_t focus_total_sec;
    uint32_t leaves_total;
    uint32_t crc32;
} model_blob_t;

/* 日志 blob: head=下一写入下标(单调递增,环取模), count=有效条数,
 * events 尾随存放。LOG_BYTES = 4 + 120*16 = 1924,单 NVS key 可容纳。 */
#define LOG_FIXED_BYTES (sizeof(uint32_t) + sizeof(uint32_t))
#define LOG_DATA_BYTES ((size_t)STATUS_LOG_CAPACITY * sizeof(status_event_t))
#define LOG_BYTES (LOG_FIXED_BYTES + LOG_DATA_BYTES)

typedef struct {
    uint32_t head;  /* 单调递增写入计数器 */
    uint32_t count; /* 已写有效条数(<=120) */
    status_event_t events[STATUS_LOG_CAPACITY];
} log_blob_t;

typedef enum {
    SAVE_MODEL = 1,
    SAVE_LOG,
} save_kind_t;

typedef struct {
    save_kind_t kind;
    model_blob_t model;
    log_blob_t log;
} save_req_t;

static const char *TAG = "status_store";
static QueueHandle_t s_queue;
static nvs_handle_t s_nvs;
static bool s_ready;
static volatile bool s_error;

/* 日志的内存副本(配合 status_model 状态与导出;NVS 只是它的持久化)。 */
static log_blob_t s_log;

static uint32_t crc32_bytes(const void *data, size_t len) {
    const uint8_t *bytes = data;
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < len; i++) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ (0xEDB88320U & (uint32_t)-(int32_t)(crc & 1));
        }
    }
    return ~crc;
}

static model_blob_t blob_from_model(const status_model_t *model) {
    model_blob_t blob;
    memset(&blob, 0, sizeof(blob));
    blob.magic = STORE_MAGIC;
    blob.version = STORE_VERSION;
    blob.size = sizeof(model_blob_t);
    blob.state = model->state;
    blob.focus_cycle_sec = model->focus_cycle_sec;
    blob.punches_cycle = model->punches_cycle;
    blob.focus_segments = model->focus_segments;
    blob.leaves_cycle = model->leaves_cycle;
    blob.punch_total = model->punch_total;
    blob.focus_total_sec = model->focus_total_sec;
    blob.leaves_total = model->leaves_total;
    blob.crc32 = crc32_bytes(&blob, offsetof(model_blob_t, crc32));
    return blob;
}

static bool model_from_blob(const model_blob_t *blob, status_model_t *model) {
    if (blob->magic != STORE_MAGIC || blob->version != STORE_VERSION ||
        blob->size != sizeof(*blob) ||
        blob->crc32 != crc32_bytes(blob, offsetof(model_blob_t, crc32))) {
        return false;
    }
    status_model_defaults(model);
    model->state = blob->state;
    model->focus_cycle_sec = blob->focus_cycle_sec;
    model->punches_cycle = blob->punches_cycle;
    model->focus_segments = blob->focus_segments;
    model->leaves_cycle = blob->leaves_cycle;
    model->punch_total = blob->punch_total;
    model->focus_total_sec = blob->focus_total_sec;
    model->leaves_total = blob->leaves_total;
    if (model->state >= STATUS_COUNT) return false;
    return true;
}

static void save_task(void *arg) {
    (void)arg;
    save_req_t req;
    while (true) {
        if (xQueueReceive(s_queue, &req, portMAX_DELAY) != pdTRUE) continue;
        esp_err_t err = ESP_ERR_INVALID_STATE;
        if (req.kind == SAVE_MODEL) {
            err = nvs_set_blob(s_nvs, "model", &req.model, sizeof(req.model));
        } else if (req.kind == SAVE_LOG) {
            err = nvs_set_blob(s_nvs, "log", &req.log,
                               LOG_FIXED_BYTES + req.log.count * sizeof(status_event_t));
        }
        if (err == ESP_OK) err = nvs_commit(s_nvs);
        if (err != ESP_OK) {
            s_error = true;
            ESP_LOGE(TAG, "NVS save failed: %s", esp_err_to_name(err));
        }
    }
}

static bool request(save_kind_t kind, const status_model_t *model) {
    if (!s_ready) return false;
    save_req_t req;
    memset(&req, 0, sizeof(req));
    req.kind = kind;
    if (kind == SAVE_MODEL && model) {
        req.model = blob_from_model(model);
    } else if (kind == SAVE_LOG) {
        req.log = s_log;
    } else {
        return false;
    }
    if (xQueueOverwrite(s_queue, &req) != pdTRUE) {
        s_error = true;
        return false;
    }
    return true;
}

bool status_store_init(status_model_t *model) {
    if (!model) return false;
    if (s_ready) return true;

    status_model_defaults(model);
    memset(&s_log, 0, sizeof(s_log));

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs recovery: %s", esp_err_to_name(err));
        err = nvs_flash_erase();
        if (err == ESP_OK) err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(err));
        s_error = true;
        return false;
    }

    err = nvs_open("status", NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
        s_error = true;
        return false;
    }

    model_blob_t mblob;
    size_t size = sizeof(mblob);
    err = nvs_get_blob(s_nvs, "model", &mblob, &size);
    if (err == ESP_OK && size == sizeof(mblob)) {
        if (model_from_blob(&mblob, model)) {
            uint64_t now_ms = (uint64_t)esp_timer_get_time() / 1000ULL;
            status_model_restore(model, now_ms);
            ESP_LOGI(TAG, "Restored model: state=%d focus_sec=%lu punches=%u",
                     (int)model->state, (unsigned long)model->focus_cycle_sec,
                     model->punches_cycle);
        } else {
            ESP_LOGW(TAG, "Stored model invalid; using defaults");
        }
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Model load failed: %s", esp_err_to_name(err));
    }

    log_blob_t lblob;
    size = sizeof(lblob);
    err = nvs_get_blob(s_nvs, "log", &lblob, &size);
    if (err == ESP_OK && size >= LOG_FIXED_BYTES) {
        s_log.head = lblob.head;
        s_log.count = lblob.count;
        if (s_log.count > STATUS_LOG_CAPACITY) s_log.count = STATUS_LOG_CAPACITY;
        size_t data = size - LOG_FIXED_BYTES;
        if (data > s_log.count * sizeof(status_event_t)) {
            data = s_log.count * sizeof(status_event_t);
        }
        if (data > 0) memcpy(s_log.events, lblob.events, data);
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Log load failed: %s", esp_err_to_name(err));
    }

    s_queue = xQueueCreate(1, sizeof(save_req_t));
    if (!s_queue || xTaskCreate(save_task, "status_save", 3072, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create persistence worker");
        s_error = true;
        return false;
    }
    s_ready = true;
    return true;
}

void status_store_request_save(const status_model_t *model) {
    request(SAVE_MODEL, model);
}

bool status_store_append_event(const status_event_t *ev) {
    if (!s_ready || !ev) return false;
    uint32_t idx = s_log.head % STATUS_LOG_CAPACITY;
    s_log.events[idx] = *ev;
    s_log.head++;
    if (s_log.count < STATUS_LOG_CAPACITY) s_log.count++;
    return request(SAVE_LOG, NULL);
}

size_t status_store_read_log(status_event_t *out, size_t max) {
    if (!out || max == 0) return 0;
    size_t n = s_log.count < max ? s_log.count : max;
    if (n == 0) return 0;
    uint32_t start = (s_log.head >= s_log.count)
                         ? (s_log.head - (uint32_t)s_log.count) % STATUS_LOG_CAPACITY
                         : 0;
    for (size_t i = 0; i < n; i++) {
        out[i] = s_log.events[(start + i) % STATUS_LOG_CAPACITY];
    }
    return n;
}

void status_store_clear_cycle(const status_model_t *model) {
    if (!s_ready) return;
    memset(&s_log, 0, sizeof(s_log));
    /* 模型周期字段清零:通过临时拷贝置零再请求保存,避免外部直接改 model。 */
    if (model) {
        status_model_t tmp = *model;
        tmp.focus_cycle_sec = 0;
        tmp.punches_cycle = 0;
        tmp.focus_segments = 0;
        tmp.leaves_cycle = 0;
        request(SAVE_MODEL, &tmp);
    } else {
        status_model_t tmp;
        status_model_defaults(&tmp);
        request(SAVE_MODEL, &tmp);
    }
    request(SAVE_LOG, NULL);
    ESP_LOGI(TAG, "Cycle stats and log cleared");
}

bool status_store_has_error(void) {
    return s_error;
}