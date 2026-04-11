/**
 * @file os_ipc.c
 * @brief Inter-Process Communication Implementation
 *
 * Implements message queues, event groups, and shared memory using
 * FreeRTOS primitives for thread-safe inter-task communication.
 *
 * Copyright (c) 2026 ESP32OS Contributors
 * SPDX-License-Identifier: MIT
 */

#include "os_ipc.h"
#include "os_logging.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

#include <string.h>
#include <stdlib.h>

/* ────────────────────────────────────────────────
   Module Configuration
   ──────────────────────────────────────────────── */

#define TAG "OS_IPC"

/* ────────────────────────────────────────────────
   Internal Data Structures
   ──────────────────────────────────────────────── */

/** Message queue internal structure */
struct os_msgq_s {
    char name[OS_IPC_NAME_LEN];
    QueueHandle_t handle;
    size_t msg_size;
    uint8_t max_msgs;
    bool active;
};

/** Event group internal structure */
struct os_event_s {
    char name[OS_IPC_NAME_LEN];
    EventGroupHandle_t handle;
    bool active;
};

/** Shared memory internal structure */
struct os_shm_s {
    char name[OS_IPC_NAME_LEN];
    uint8_t *buffer;
    size_t size;
    bool active;
};

/* ────────────────────────────────────────────────
   Module State
   ──────────────────────────────────────────────── */

/** Message queue table */
static struct os_msgq_s s_queues[OS_IPC_MAX_QUEUES];

/** Event group table */
static struct os_event_s s_events[OS_IPC_MAX_EVENT_GROUPS];

/** Shared memory table */
static struct os_shm_s s_shm[OS_IPC_MAX_SHM];

/** Global mutex protecting all IPC tables */
static SemaphoreHandle_t s_mutex;

/** Module initialized flag */
static bool s_initialized = false;

/* ────────────────────────────────────────────────
   Private Helper Functions
   ──────────────────────────────────────────────── */

/**
 * @brief Find unused queue slot
 * @return Index or -1 if full
 */
static int find_free_queue_slot(void)
{
    for (int i = 0; i < OS_IPC_MAX_QUEUES; i++) {
        if (!s_queues[i].active) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief Find unused event group slot
 * @return Index or -1 if full
 */
static int find_free_event_slot(void)
{
    for (int i = 0; i < OS_IPC_MAX_EVENT_GROUPS; i++) {
        if (!s_events[i].active) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief Find unused shared memory slot
 * @return Index or -1 if full
 */
static int find_free_shm_slot(void)
{
    for (int i = 0; i < OS_IPC_MAX_SHM; i++) {
        if (!s_shm[i].active) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief Validate queue handle
 */
static inline bool is_valid_queue(os_msgq_t q)
{
    return (q != NULL && q->active);
}

/**
 * @brief Validate event handle
 */
static inline bool is_valid_event(os_event_t ev)
{
    return (ev != NULL && ev->active);
}

/**
 * @brief Validate shared memory handle
 */
static inline bool is_valid_shm(os_shm_t shm)
{
    return (shm != NULL && shm->active);
}

/* ────────────────────────────────────────────────
   Public API - Initialization
   ──────────────────────────────────────────────── */

esp_err_t os_ipc_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        OS_LOGE(TAG, "Failed to create IPC mutex");
        return ESP_ERR_NO_MEM;
    }

    memset(s_queues, 0, sizeof(s_queues));
    memset(s_events, 0, sizeof(s_events));
    memset(s_shm, 0, sizeof(s_shm));

    s_initialized = true;

    OS_LOGI(TAG, "IPC initialized: %d queues, %d events, %d shm",
            OS_IPC_MAX_QUEUES, OS_IPC_MAX_EVENT_GROUPS, OS_IPC_MAX_SHM);
    return ESP_OK;
}

void os_ipc_deinit(void)
{
    if (!s_initialized) {
        return;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    /* Block new API use while teardown is in progress */
    s_initialized = false;

    /* Delete all queues */
    for (int i = 0; i < OS_IPC_MAX_QUEUES; i++) {
        if (s_queues[i].active && s_queues[i].handle != NULL) {
            vQueueDelete(s_queues[i].handle);
            s_queues[i].active = false;
        }
    }

    /* Delete all event groups */
    for (int i = 0; i < OS_IPC_MAX_EVENT_GROUPS; i++) {
        if (s_events[i].active && s_events[i].handle != NULL) {
            vEventGroupDelete(s_events[i].handle);
            s_events[i].active = false;
        }
    }

    /* Mark all shared memory as inactive */
    for (int i = 0; i < OS_IPC_MAX_SHM; i++) {
        free(s_shm[i].buffer);
        s_shm[i].buffer = NULL;
        s_shm[i].size = 0;
        s_shm[i].active = false;
    }

    xSemaphoreGive(s_mutex);

    vSemaphoreDelete(s_mutex);
    s_mutex = NULL;

    OS_LOGI(TAG, "IPC deinitialized");
}

/* ────────────────────────────────────────────────
   Public API - Message Queues
   ──────────────────────────────────────────────── */

os_msgq_t os_msgq_create(const char *name, size_t msg_size, uint8_t max_msgs)
{
    if (!s_initialized || name == NULL || msg_size == 0 || msg_size > OS_IPC_MAX_MSG_SIZE) {
        OS_LOGE(TAG, "Invalid msgq create params");
        return NULL;
    }

    if (max_msgs == 0) {
        max_msgs = OS_IPC_QUEUE_DEFAULT_SIZE;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    /* Check for duplicate name */
    for (int i = 0; i < OS_IPC_MAX_QUEUES; i++) {
        if (s_queues[i].active && strcmp(s_queues[i].name, name) == 0) {
            OS_LOGW(TAG, "Queue '%s' already exists", name);
            xSemaphoreGive(s_mutex);
            return NULL;
        }
    }

    int idx = find_free_queue_slot();
    if (idx < 0) {
        OS_LOGE(TAG, "No free queue slots");
        xSemaphoreGive(s_mutex);
        return NULL;
    }

    /* Create FreeRTOS queue */
    QueueHandle_t q = xQueueCreate(max_msgs, msg_size);
    if (q == NULL) {
        OS_LOGE(TAG, "xQueueCreate failed");
        xSemaphoreGive(s_mutex);
        return NULL;
    }

    /* Initialize structure */
    s_queues[idx].handle = q;
    s_queues[idx].msg_size = msg_size;
    s_queues[idx].max_msgs = max_msgs;
    strncpy(s_queues[idx].name, name, OS_IPC_NAME_LEN - 1);
    s_queues[idx].name[OS_IPC_NAME_LEN - 1] = '\0';
    s_queues[idx].active = true;

    xSemaphoreGive(s_mutex);

    OS_LOGI(TAG, "Queue '%s' created: size=%u, msg=%zu bytes", name, max_msgs, msg_size);
    return &s_queues[idx];
}

esp_err_t os_msgq_delete(os_msgq_t q)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (!is_valid_queue(q)) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_INVALID_ARG;
    }

    vQueueDelete(q->handle);
    q->active = false;

    xSemaphoreGive(s_mutex);

    OS_LOGI(TAG, "Queue '%s' deleted", q->name);
    return ESP_OK;
}

esp_err_t os_msgq_send(os_msgq_t q, const void *msg, uint32_t timeout_ms)
{
    if (!is_valid_queue(q) || msg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    TickType_t ticks = (timeout_ms == portMAX_DELAY) ?
                       portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);

    BaseType_t ret = xQueueSend(q->handle, msg, ticks);

    return (ret == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t os_msgq_receive(os_msgq_t q, void *buf, uint32_t timeout_ms)
{
    if (!is_valid_queue(q) || buf == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    TickType_t ticks = (timeout_ms == portMAX_DELAY) ?
                       portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);

    BaseType_t ret = xQueueReceive(q->handle, buf, ticks);

    return (ret == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

uint8_t os_msgq_count(os_msgq_t q)
{
    if (!is_valid_queue(q)) {
        return 0;
    }

    return (uint8_t)uxQueueMessagesWaiting(q->handle);
}

uint8_t os_msgq_spaces_available(os_msgq_t q)
{
    if (!is_valid_queue(q)) {
        return 0;
    }

    return (uint8_t)uxQueueSpacesAvailable(q->handle);
}

bool os_msgq_is_full(os_msgq_t q)
{
    if (!is_valid_queue(q)) {
        return false;
    }

    return xQueueIsQueueFullFromISR(q->handle) == pdTRUE;
}

os_msgq_t os_msgq_find(const char *name)
{
    if (!s_initialized || name == NULL) {
        return NULL;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    for (int i = 0; i < OS_IPC_MAX_QUEUES; i++) {
        if (s_queues[i].active && strcmp(s_queues[i].name, name) == 0) {
            xSemaphoreGive(s_mutex);
            return &s_queues[i];
        }
    }

    xSemaphoreGive(s_mutex);
    return NULL;
}

void os_msgq_list(int fd)
{
    (void)fd; /* Unused in this implementation */

    if (!s_initialized) {
        return;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    OS_LOGI(TAG, "Active message queues:");
    for (int i = 0; i < OS_IPC_MAX_QUEUES; i++) {
        if (s_queues[i].active) {
            uint8_t count = (uint8_t)uxQueueMessagesWaiting(s_queues[i].handle);
            OS_LOGI(TAG, "  [%d] '%s': %u/%u messages, %zu bytes/msg",
                    i, s_queues[i].name, count, s_queues[i].max_msgs, s_queues[i].msg_size);
        }
    }

    xSemaphoreGive(s_mutex);
}

/* ────────────────────────────────────────────────
   Public API - Event Groups
   ──────────────────────────────────────────────── */

os_event_t os_event_create(const char *name)
{
    if (!s_initialized || name == NULL) {
        return NULL;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    /* Check for duplicate */
    for (int i = 0; i < OS_IPC_MAX_EVENT_GROUPS; i++) {
        if (s_events[i].active && strcmp(s_events[i].name, name) == 0) {
            xSemaphoreGive(s_mutex);
            return NULL;
        }
    }

    int idx = find_free_event_slot();
    if (idx < 0) {
        xSemaphoreGive(s_mutex);
        return NULL;
    }

    EventGroupHandle_t eg = xEventGroupCreate();
    if (eg == NULL) {
        xSemaphoreGive(s_mutex);
        return NULL;
    }

    s_events[idx].handle = eg;
    strncpy(s_events[idx].name, name, OS_IPC_NAME_LEN - 1);
    s_events[idx].name[OS_IPC_NAME_LEN - 1] = '\0';
    s_events[idx].active = true;

    xSemaphoreGive(s_mutex);

    OS_LOGI(TAG, "Event group '%s' created", name);
    return &s_events[idx];
}

esp_err_t os_event_delete(os_event_t ev)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (!is_valid_event(ev)) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_INVALID_ARG;
    }

    vEventGroupDelete(ev->handle);
    ev->active = false;

    xSemaphoreGive(s_mutex);

    OS_LOGI(TAG, "Event group '%s' deleted", ev->name);
    return ESP_OK;
}

esp_err_t os_event_set(os_event_t ev, os_event_bits_t bits)
{
    if (!is_valid_event(ev)) {
        return ESP_ERR_INVALID_ARG;
    }

    xEventGroupSetBits(ev->handle, bits);
    return ESP_OK;
}

esp_err_t os_event_clear(os_event_t ev, os_event_bits_t bits)
{
    if (!is_valid_event(ev)) {
        return ESP_ERR_INVALID_ARG;
    }

    xEventGroupClearBits(ev->handle, bits);
    return ESP_OK;
}

os_event_bits_t os_event_get(os_event_t ev)
{
    if (!is_valid_event(ev)) {
        return 0;
    }

    return xEventGroupGetBits(ev->handle);
}

os_event_bits_t os_event_wait(os_event_t ev, os_event_bits_t bits,
                                 bool clear_on_exit, bool wait_for_all,
                                 uint32_t timeout_ms)
{
    if (!is_valid_event(ev) || bits == 0) {
        return 0;
    }

    TickType_t ticks = (timeout_ms == portMAX_DELAY) ?
                       portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);

    BaseType_t clear = clear_on_exit ? pdTRUE : pdFALSE;
    BaseType_t wait_all = wait_for_all ? pdTRUE : pdFALSE;

    return xEventGroupWaitBits(ev->handle, bits, clear, wait_all, ticks);
}

os_event_t os_event_find(const char *name)
{
    if (!s_initialized || name == NULL) {
        return NULL;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    for (int i = 0; i < OS_IPC_MAX_EVENT_GROUPS; i++) {
        if (s_events[i].active && strcmp(s_events[i].name, name) == 0) {
            xSemaphoreGive(s_mutex);
            return &s_events[i];
        }
    }

    xSemaphoreGive(s_mutex);
    return NULL;
}

void os_event_list(int fd)
{
    (void)fd;

    if (!s_initialized) {
        return;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    OS_LOGI(TAG, "Active event groups:");
    for (int i = 0; i < OS_IPC_MAX_EVENT_GROUPS; i++) {
        if (s_events[i].active) {
            EventBits_t bits = xEventGroupGetBits(s_events[i].handle);
            OS_LOGI(TAG, "  [%d] '%s': bits=0x%08X", i, s_events[i].name, (unsigned)bits);
        }
    }

    xSemaphoreGive(s_mutex);
}

/* ────────────────────────────────────────────────
   Public API - Shared Memory
   ──────────────────────────────────────────────── */

os_shm_t os_shm_create(const char *name, size_t size)
{
    if (!s_initialized || name == NULL || size == 0 || size > OS_IPC_MAX_SHM_SIZE) {
        return NULL;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    /* Check for duplicate */
    for (int i = 0; i < OS_IPC_MAX_SHM; i++) {
        if (s_shm[i].active && strcmp(s_shm[i].name, name) == 0) {
            xSemaphoreGive(s_mutex);
            return NULL;
        }
    }

    int idx = find_free_shm_slot();
    if (idx < 0) {
        xSemaphoreGive(s_mutex);
        return NULL;
    }

    uint8_t *buf = calloc(1, size);
    if (buf == NULL) {
        xSemaphoreGive(s_mutex);
        return NULL;
    }

    s_shm[idx].buffer = buf;
    s_shm[idx].size = size;
    strncpy(s_shm[idx].name, name, OS_IPC_NAME_LEN - 1);
    s_shm[idx].name[OS_IPC_NAME_LEN - 1] = '\0';
    s_shm[idx].active = true;

    xSemaphoreGive(s_mutex);

    OS_LOGI(TAG, "Shared memory '%s' created: %zu bytes", name, size);
    return &s_shm[idx];
}

esp_err_t os_shm_delete(os_shm_t shm)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (!is_valid_shm(shm)) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_INVALID_ARG;
    }

    free(shm->buffer);
    shm->buffer = NULL;
    shm->size = 0;
    shm->active = false;

    xSemaphoreGive(s_mutex);

    OS_LOGI(TAG, "Shared memory '%s' deleted", shm->name);
    return ESP_OK;
}

void *os_shm_get_ptr(os_shm_t shm)
{
    if (!s_initialized || s_mutex == NULL) {
        return NULL;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    void *ptr = is_valid_shm(shm) ? shm->buffer : NULL;
    xSemaphoreGive(s_mutex);

    return ptr;
}

size_t os_shm_get_size(os_shm_t shm)
{
    if (!s_initialized || s_mutex == NULL) {
        return 0;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    size_t size = is_valid_shm(shm) ? shm->size : 0;
    xSemaphoreGive(s_mutex);

    return size;
}

os_shm_t os_shm_find(const char *name)
{
    if (!s_initialized || name == NULL) {
        return NULL;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    for (int i = 0; i < OS_IPC_MAX_SHM; i++) {
        if (s_shm[i].active && strcmp(s_shm[i].name, name) == 0) {
            xSemaphoreGive(s_mutex);
            return &s_shm[i];
        }
    }

    xSemaphoreGive(s_mutex);
    return NULL;
}

void os_shm_list(int fd)
{
    (void)fd;

    if (!s_initialized) {
        return;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    OS_LOGI(TAG, "Active shared memory regions:");
    for (int i = 0; i < OS_IPC_MAX_SHM; i++) {
        if (s_shm[i].active) {
            OS_LOGI(TAG, "  [%d] '%s': %zu bytes", i, s_shm[i].name, s_shm[i].size);
        }
    }

    xSemaphoreGive(s_mutex);
}

void os_ipc_print_status(int fd)
{
    (void)fd;

    if (!s_initialized) {
        OS_LOGI(TAG, "IPC not initialized");
        return;
    }

    uint8_t q_count = 0, ev_count = 0, shm_count = 0;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    for (int i = 0; i < OS_IPC_MAX_QUEUES; i++) {
        if (s_queues[i].active) q_count++;
    }
    for (int i = 0; i < OS_IPC_MAX_EVENT_GROUPS; i++) {
        if (s_events[i].active) ev_count++;
    }
    for (int i = 0; i < OS_IPC_MAX_SHM; i++) {
        if (s_shm[i].active) shm_count++;
    }

    xSemaphoreGive(s_mutex);

    OS_LOGI(TAG, "IPC Status: %d/%d queues, %d/%d events, %d/%d shm",
            q_count, OS_IPC_MAX_QUEUES, ev_count, OS_IPC_MAX_EVENT_GROUPS,
            shm_count, OS_IPC_MAX_SHM);
}
