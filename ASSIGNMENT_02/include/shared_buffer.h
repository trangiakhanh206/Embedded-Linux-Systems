#ifndef SHARED_BUFFER_H
#define SHARED_BUFFER_H

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

#include "sms_common.h"

typedef struct {
    SensorData *data;

    size_t capacity;
    size_t head;
    size_t tail;
    size_t count;

    uint64_t total_produced;
    uint64_t total_consumed;
    uint64_t overflow_count;

    int closed;

    pthread_mutex_t mutex;
    pthread_cond_t not_empty;

} SharedBuffer;

typedef struct {
    size_t current_size;

    uint64_t total_produced;
    uint64_t total_consumed;
    uint64_t overflow_count;

} BufferStats;

/* Initialize circular buffer */
int shared_buffer_init(SharedBuffer *buffer, size_t capacity);

/* Insert one sample. Return 1 if inserted, 0 if full/closed. */
int shared_buffer_push(SharedBuffer *buffer, const SensorData *sample);

/* Wait for data. Return 1 if read, 0 if closed and empty. */
int shared_buffer_pop(SharedBuffer *buffer, SensorData *sample);

/* Wake consumers and prevent new insertions */
void shared_buffer_close(SharedBuffer *buffer);

/* Get buffer statistics */
void shared_buffer_get_stats(SharedBuffer *buffer, BufferStats *stats);

/* Release memory and synchronization objects after threads stop */
void shared_buffer_destroy(SharedBuffer *buffer);

#endif
