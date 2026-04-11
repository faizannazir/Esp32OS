/**
 * @file os_ipc.h
 * @brief Inter-Process Communication (IPC) Subsystem for ESP32OS
 *
 * Provides message queues, event groups, and shared memory for
 * inter-task communication and synchronization.
 *
 * Copyright (c) 2026 ESP32OS Contributors
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

/* ────────────────────────────────────────────────
   Configuration Constants
   ──────────────────────────────────────────────── */

/** Maximum number of message queues */
#define OS_IPC_MAX_QUEUES 8

/** Maximum number of event groups */
#define OS_IPC_MAX_EVENT_GROUPS 8

/** Maximum number of shared memory regions */
#define OS_IPC_MAX_SHM 4

/** Maximum name length for IPC objects */
#define OS_IPC_NAME_LEN 16

/** Default message queue size (number of messages) */
#define OS_IPC_QUEUE_DEFAULT_SIZE 10

/** Maximum message size in bytes */
#define OS_IPC_MAX_MSG_SIZE 256

/** Maximum shared memory region size */
#define OS_IPC_MAX_SHM_SIZE 4096

/* ────────────────────────────────────────────────
   Message Queue API
   ──────────────────────────────────────────────── */

/** Opaque message queue handle */
typedef struct os_msgq_s *os_msgq_t;

/**
 * @brief Create a message queue
 *
 * Creates a FIFO message queue for inter-task communication.
 * Messages are copied into the queue (not referenced).
 *
 * @param name      Unique name for the queue (max OS_IPC_NAME_LEN-1 chars)
 * @param msg_size  Size of each message in bytes (max OS_IPC_MAX_MSG_SIZE)
 * @param max_msgs  Maximum number of messages in queue
 *
 * @return Queue handle on success, NULL on failure
 */
os_msgq_t os_msgq_create(const char *name, size_t msg_size, uint8_t max_msgs);

/**
 * @brief Delete a message queue
 *
 * @param q Queue to delete
 *
 * @return ESP_OK on success
 */
esp_err_t os_msgq_delete(os_msgq_t q);

/**
 * @brief Send a message to queue
 *
 * Copies msg to queue. Blocks if queue is full until timeout.
 *
 * @param q          Queue handle
 * @param msg        Pointer to message data
 * @param timeout_ms Maximum time to wait (0 = don't wait, portMAX_DELAY = forever)
 *
 * @return ESP_OK on success, ESP_ERR_TIMEOUT if timeout, ESP_ERR_INVALID_ARG if invalid
 */
esp_err_t os_msgq_send(os_msgq_t q, const void *msg, uint32_t timeout_ms);

/**
 * @brief Receive a message from queue
 *
 * Copies message from queue to buf. Blocks until message available or timeout.
 *
 * @param q          Queue handle
 * @param buf        Buffer to receive message (must be >= msg_size)
 * @param timeout_ms Maximum time to wait
 *
 * @return ESP_OK on success, ESP_ERR_TIMEOUT if timeout, ESP_ERR_INVALID_ARG if invalid
 */
esp_err_t os_msgq_receive(os_msgq_t q, void *buf, uint32_t timeout_ms);

/**
 * @brief Get number of messages in queue
 *
 * @param q Queue handle
 *
 * @return Number of messages (0 if invalid)
 */
uint8_t os_msgq_count(os_msgq_t q);

/**
 * @brief Get number of free slots in queue
 *
 * @param q Queue handle
 *
 * @return Number of free slots (0 if invalid)
 */
uint8_t os_msgq_spaces_available(os_msgq_t q);

/**
 * @brief Check if queue is full
 *
 * @param q Queue handle
 *
 * @return true if full, false otherwise
 */
bool os_msgq_is_full(os_msgq_t q);

/**
 * @brief Find a queue by name
 *
 * @param name Queue name
 *
 * @return Queue handle or NULL if not found
 */
os_msgq_t os_msgq_find(const char *name);

/**
 * @brief List all message queues to file descriptor
 *
 * @param fd File descriptor for output (-1 for UART)
 */
void os_msgq_list(int fd);

/* ────────────────────────────────────────────────
   Event Group API
   ──────────────────────────────────────────────── */

/** Opaque event group handle */
typedef struct os_event_s *os_event_t;

/** Event bits type - allows up to 32 distinct events */
typedef uint32_t os_event_bits_t;

/**
 * @brief Create an event group
 *
 * Event groups allow tasks to wait for multiple events.
 *
 * @param name Unique name for the event group
 *
 * @return Event group handle on success, NULL on failure
 */
os_event_t os_event_create(const char *name);

/**
 * @brief Delete an event group
 *
 * @param ev Event group to delete
 *
 * @return ESP_OK on success
 */
esp_err_t os_event_delete(os_event_t ev);

/**
 * @brief Set event bits
 *
 * Sets one or more bits in the event group. Unblocks tasks waiting
 * for these bits.
 *
 * @param ev   Event group handle
 * @param bits Bits to set (OR combination)
 *
 * @return ESP_OK on success
 */
esp_err_t os_event_set(os_event_t ev, os_event_bits_t bits);

/**
 * @brief Clear event bits
 *
 * @param ev   Event group handle
 * @param bits Bits to clear (OR combination)
 *
 * @return ESP_OK on success
 */
esp_err_t os_event_clear(os_event_t ev, os_event_bits_t bits);

/**
 * @brief Get current event bits
 *
 * @param ev Event group handle
 *
 * @return Current bit values (0 if invalid)
 */
os_event_bits_t os_event_get(os_event_t ev);

/**
 * @brief Wait for event bits
 *
 * Blocks until specified bits are set or timeout occurs.
 *
 * @param ev            Event group handle
 * @param bits          Bits to wait for (OR combination)
 * @param clear_on_exit If true, clear bits before returning
 * @param wait_for_all  If true, wait for ALL bits; if false, ANY bit
 * @param timeout_ms    Maximum wait time
 *
 * @return Bits that were set (0 if timeout)
 */
os_event_bits_t os_event_wait(os_event_t ev, os_event_bits_t bits,
                                 bool clear_on_exit, bool wait_for_all,
                                 uint32_t timeout_ms);

/**
 * @brief Find an event group by name
 *
 * @param name Event group name
 *
 * @return Event group handle or NULL if not found
 */
os_event_t os_event_find(const char *name);

/**
 * @brief List all event groups to file descriptor
 *
 * @param fd File descriptor for output (-1 for UART)
 */
void os_event_list(int fd);

/* ────────────────────────────────────────────────
   Shared Memory API
   ──────────────────────────────────────────────── */

/** Opaque shared memory handle */
typedef struct os_shm_s *os_shm_t;

/**
 * @brief Create a shared memory region
 *
 * Allocates dynamically-sized memory that can be accessed by multiple tasks.
 * Memory is zero-initialized on creation.
 *
 * @param name Unique name for the region (max OS_IPC_NAME_LEN-1 chars)
 * @param size Size in bytes (max OS_IPC_MAX_SHM_SIZE)
 *
 * @return Shared memory handle on success, NULL on failure
 */
os_shm_t os_shm_create(const char *name, size_t size);

/**
 * @brief Delete a shared memory region
 *
 * @param shm Shared memory handle
 *
 * @return ESP_OK on success
 */
esp_err_t os_shm_delete(os_shm_t shm);

/**
 * @brief Get pointer to shared memory
 *
 * @param shm Shared memory handle
 *
 * @return Pointer to memory (NULL if invalid)
 */
void *os_shm_get_ptr(os_shm_t shm);

/**
 * @brief Get size of shared memory region
 *
 * @param shm Shared memory handle
 *
 * @return Size in bytes (0 if invalid)
 */
size_t os_shm_get_size(os_shm_t shm);

/**
 * @brief Find a shared memory region by name
 *
 * @param name Shared memory name
 *
 * @return Shared memory handle or NULL if not found
 */
os_shm_t os_shm_find(const char *name);

/**
 * @brief List all shared memory regions to file descriptor
 *
 * @param fd File descriptor for output (-1 for UART)
 */
void os_shm_list(int fd);

/* ────────────────────────────────────────────────
   IPC Subsystem Management
   ──────────────────────────────────────────────── */

/**
 * @brief Initialize the IPC subsystem
 *
 * Must be called before using any IPC functions.
 *
 * @return ESP_OK on success
 */
esp_err_t os_ipc_init(void);

/**
 * @brief Deinitialize the IPC subsystem
 *
 * Destroys all IPC objects.
 */
void os_ipc_deinit(void);

/**
 * @brief Print IPC subsystem status
 *
 * @param fd File descriptor for output (-1 for UART)
 */
void os_ipc_print_status(int fd);

#ifdef __cplusplus
}
#endif
