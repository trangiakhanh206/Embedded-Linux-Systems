#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shared_buffer.h"

/*
 * Initialize circular buffer.
 * Memory is allocated only once during startup.
 */
int shared_buffer_init(SharedBuffer *buffer, size_t capacity)
{
    if (buffer == NULL || capacity == 0)
        return -1;

    memset(buffer, 0, sizeof(*buffer));

    buffer->data = calloc(capacity, sizeof(SensorData));

    if (buffer->data == NULL) {
        perror("calloc");
        return -1;
    }

    buffer->capacity = capacity;

    if (pthread_mutex_init(&buffer->mutex, NULL) != 0) {
        free(buffer->data);
        buffer->data = NULL;
        return -1;
    }

    if (pthread_cond_init(&buffer->not_empty, NULL) != 0) {
        pthread_mutex_destroy(&buffer->mutex);

        free(buffer->data);
        buffer->data = NULL;

        return -1;
    }

    return 0;
}

/*
 * Producer inserts a sensor sample.
 *
 * If buffer is full:
 * - Drop the sample
 * - Increment overflow_count
 *
 * Return:
 * 1 = inserted
 * 0 = buffer full or closed
 */
int shared_buffer_push(SharedBuffer *buffer, const SensorData *sample)
{
    if (buffer == NULL || sample == NULL)
        return 0;

    pthread_mutex_lock(&buffer->mutex);

    if (buffer->closed) {
        pthread_mutex_unlock(&buffer->mutex);
        return 0;
    }

    if (buffer->count == buffer->capacity) {
        buffer->overflow_count++;

        pthread_mutex_unlock(&buffer->mutex);
        return 0;
    }

    buffer->data[buffer->head] = *sample;

    buffer->head = (buffer->head + 1) % buffer->capacity;

    buffer->count++;
    buffer->total_produced++;

    pthread_cond_signal(&buffer->not_empty);

    pthread_mutex_unlock(&buffer->mutex);

    return 1;
}

/*
 * Consumer removes one sensor sample.
 *
 * If buffer is empty:
 * - Wait on condition variable
 *
 * Return:
 * 1 = sample received
 * 0 = buffer closed and empty
 */
int shared_buffer_pop(SharedBuffer *buffer, SensorData *sample)
{
    if (buffer == NULL || sample == NULL)
        return 0;

    pthread_mutex_lock(&buffer->mutex);

    while (buffer->count == 0 && !buffer->closed) {
        pthread_cond_wait(&buffer->not_empty, &buffer->mutex);
    }

    if (buffer->count == 0 && buffer->closed) {
        pthread_mutex_unlock(&buffer->mutex);
        return 0;
    }

    *sample = buffer->data[buffer->tail];

    buffer->tail = (buffer->tail + 1) % buffer->capacity;

    buffer->count--;
    buffer->total_consumed++;

    pthread_mutex_unlock(&buffer->mutex);

    return 1;
}

/*
 * Close buffer and wake waiting consumers.
 */
void shared_buffer_close(SharedBuffer *buffer)
{
    if (buffer == NULL)
        return;

    pthread_mutex_lock(&buffer->mutex);

    buffer->closed = 1;

    pthread_cond_broadcast(&buffer->not_empty);

    pthread_mutex_unlock(&buffer->mutex);
}

/*
 * Read statistics safely.
 */
void shared_buffer_get_stats(SharedBuffer *buffer, BufferStats *stats)
{
    if (buffer == NULL || stats == NULL)
        return;

    pthread_mutex_lock(&buffer->mutex);

    stats->current_size = buffer->count;

    stats->total_produced = buffer->total_produced;
    stats->total_consumed = buffer->total_consumed;
    stats->overflow_count = buffer->overflow_count;

    pthread_mutex_unlock(&buffer->mutex);
}

/*
 * Destroy buffer after producer and consumer threads have stopped.
 */
void shared_buffer_destroy(SharedBuffer *buffer)
{
    if (buffer == NULL)
        return;

    pthread_cond_destroy(&buffer->not_empty);
    pthread_mutex_destroy(&buffer->mutex);

    free(buffer->data);

    buffer->data = NULL;
}
