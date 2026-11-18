// SPDX-License-Identifier: GPL-3.0-or-later
// FreeRTOS primitives the shared sources use, on std::thread and condition
// variables. Ticks are milliseconds (portTICK_PERIOD_MS == 1).
#include <freertos/FreeRTOS.h>

#include <pthread.h>
#include <unistd.h>

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

extern "C" uint32_t millis(void);

namespace {

struct Sem {
    std::mutex m;
    std::condition_variable cv;
    UBaseType_t count;
    UBaseType_t max;
    bool mutex;              // mutex semantics: owner-tracked, recursive
    pthread_t owner{};
    unsigned depth = 0;
};

struct Queue {
    std::mutex m;
    std::condition_variable cv;
    std::deque<std::vector<uint8_t>> items;
    UBaseType_t len;
    UBaseType_t size;
};

template <typename Pred>
bool waitFor(std::unique_lock<std::mutex> &lk, std::condition_variable &cv, TickType_t wait, Pred p) {
    if (wait == portMAX_DELAY) { cv.wait(lk, p); return true; }
    return cv.wait_for(lk, std::chrono::milliseconds(wait), p);
}

thread_local bool tIsTask = false;

}  // namespace

extern "C" {

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char *name, uint32_t, void *arg,
                                   UBaseType_t, TaskHandle_t *out, BaseType_t) {
    try {
        std::thread t([fn, arg, n = std::string(name ? name : "task")] {
            tIsTask = true;
            pthread_setname_np(pthread_self(), n.substr(0, 15).c_str());
            fn(arg);
        });
        if (out) *out = (TaskHandle_t)(uintptr_t)t.native_handle();
        t.detach();
        return pdPASS;
    } catch (...) {
        return pdFAIL;
    }
}
BaseType_t xTaskCreate(TaskFunction_t fn, const char *name, uint32_t stack, void *arg,
                       UBaseType_t prio, TaskHandle_t *out) {
    return xTaskCreatePinnedToCore(fn, name, stack, arg, prio, out, tskNO_AFFINITY);
}
// Only ever called by a task on itself (NULL) in these sources.
void vTaskDelete(TaskHandle_t t) {
    if (t == nullptr || (pthread_t)(uintptr_t)t == pthread_self()) pthread_exit(nullptr);
}
void vTaskDelay(TickType_t ticks) { usleep((useconds_t)ticks * 1000); }
TickType_t xTaskGetTickCount(void) { return millis(); }
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return (TaskHandle_t)(uintptr_t)pthread_self(); }
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t) { return 4096; }
void vTaskSuspendAll(void) {}
BaseType_t xTaskResumeAll(void) { return pdFALSE; }

static SemaphoreHandle_t newSem(UBaseType_t max, UBaseType_t initial, bool mutex) {
    Sem *s = new Sem;
    s->count = initial;
    s->max = max;
    s->mutex = mutex;
    return s;
}
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return newSem(1, 1, true); }
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void) { return newSem(1, 1, true); }
SemaphoreHandle_t xSemaphoreCreateBinary(void) { return newSem(1, 0, false); }
SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t *buf) {
    SemaphoreHandle_t s = newSem(1, 0, false);
    if (buf) buf->impl = s;
    return s;
}
SemaphoreHandle_t xSemaphoreCreateCounting(UBaseType_t max, UBaseType_t initial) {
    return newSem(max, initial, false);
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t h, TickType_t wait) {
    if (!h) return pdFALSE;
    Sem *s = (Sem *)h;
    std::unique_lock<std::mutex> lk(s->m);
    if (s->mutex && s->depth > 0 && pthread_equal(s->owner, pthread_self())) {
        s->depth++;
        return pdTRUE;
    }
    if (!waitFor(lk, s->cv, wait, [s] { return s->count > 0; })) return pdFALSE;
    s->count--;
    if (s->mutex) { s->owner = pthread_self(); s->depth = 1; }
    return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t h) {
    if (!h) return pdFALSE;
    Sem *s = (Sem *)h;
    std::lock_guard<std::mutex> lk(s->m);
    if (s->mutex) {
        if (s->depth == 0) return pdFALSE;
        if (--s->depth > 0) return pdTRUE;
    }
    if (s->count >= s->max) return pdFALSE;
    s->count++;
    s->cv.notify_one();
    return pdTRUE;
}
BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t s, TickType_t wait) { return xSemaphoreTake(s, wait); }
BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t s) { return xSemaphoreGive(s); }
BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t s, BaseType_t *woken) {
    if (woken) *woken = pdFALSE;
    return xSemaphoreGive(s);
}
void vSemaphoreDelete(SemaphoreHandle_t h) { delete (Sem *)h; }

QueueHandle_t xQueueCreate(UBaseType_t len, UBaseType_t size) {
    Queue *q = new Queue;
    q->len = len;
    q->size = size;
    return q;
}
BaseType_t xQueueSend(QueueHandle_t h, const void *item, TickType_t wait) {
    if (!h) return pdFALSE;
    Queue *q = (Queue *)h;
    std::unique_lock<std::mutex> lk(q->m);
    if (!waitFor(lk, q->cv, wait, [q] { return q->items.size() < q->len; })) return errQUEUE_FULL;
    const uint8_t *p = (const uint8_t *)item;
    q->items.emplace_back(p, p + q->size);
    q->cv.notify_all();
    return pdPASS;
}
BaseType_t xQueueSendToBack(QueueHandle_t q, const void *item, TickType_t wait) { return xQueueSend(q, item, wait); }
BaseType_t xQueueSendFromISR(QueueHandle_t q, const void *item, BaseType_t *woken) {
    if (woken) *woken = pdFALSE;
    return xQueueSend(q, item, 0);
}
BaseType_t xQueueReceive(QueueHandle_t h, void *item, TickType_t wait) {
    if (!h) return pdFALSE;
    Queue *q = (Queue *)h;
    std::unique_lock<std::mutex> lk(q->m);
    if (!waitFor(lk, q->cv, wait, [q] { return !q->items.empty(); })) return pdFALSE;
    memcpy(item, q->items.front().data(), q->size);
    q->items.pop_front();
    q->cv.notify_all();
    return pdTRUE;
}
BaseType_t xQueueReset(QueueHandle_t h) {
    Queue *q = (Queue *)h;
    std::lock_guard<std::mutex> lk(q->m);
    q->items.clear();
    q->cv.notify_all();
    return pdPASS;
}
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t h) {
    Queue *q = (Queue *)h;
    std::lock_guard<std::mutex> lk(q->m);
    return (UBaseType_t)q->items.size();
}
void vQueueDelete(QueueHandle_t h) { delete (Queue *)h; }

}  // extern "C"
