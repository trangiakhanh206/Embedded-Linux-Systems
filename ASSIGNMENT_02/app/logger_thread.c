#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#include "logger_thread.h"
#include "sms_common.h"

/*
 * Initialize thread-safe log queue.
 */
int log_queue_init(LogQueue *queue)
{
    if (queue == NULL)
        return -1;

    memset(queue, 0, sizeof(*queue));

    if (pthread_mutex_init(&queue->mutex, NULL) != 0)
        return -1;

    if (pthread_cond_init(&queue->not_empty, NULL) != 0) {
        pthread_mutex_destroy(&queue->mutex);
        return -1;
    }

    return 0;
}

/*
 * Insert one log message.
 *
 * Return:
 * 1 = success
 * 0 = queue full or closed
 */
int log_queue_push(LogQueue *queue, const char *message)
{
    if (queue == NULL || message == NULL)
        return 0;

    pthread_mutex_lock(&queue->mutex);

    if (queue->closed) {
        pthread_mutex_unlock(&queue->mutex);
        return 0;
    }

    if (queue->count >= SMS_LOG_QUEUE_SIZE) {
        queue->dropped_messages++;
        pthread_mutex_unlock(&queue->mutex);
        return 0;
    }

    LogMessage *entry = &queue->messages[queue->head];

    snprintf(entry->text, sizeof(entry->text), "%s", message);

    queue->head = (queue->head + 1) % SMS_LOG_QUEUE_SIZE;
    queue->count++;
    queue->total_enqueued++;

    pthread_cond_signal(&queue->not_empty);

    pthread_mutex_unlock(&queue->mutex);

    return 1;
}

/*
 * Receive one log message.
 *
 * Return:
 *  1 = message received
 *  0 = queue closed and empty
 * -1 = timed out after up to 1 second
 *
 * Timed waiting allows the logger to flush periodically
 * even when no new messages arrive.
 */
int log_queue_pop(LogQueue *queue, LogMessage *message)
{
    if (queue == NULL || message == NULL)
        return 0;

    pthread_mutex_lock(&queue->mutex);

    while (queue->count == 0 && !queue->closed) {
        struct timespec deadline;

        if (clock_gettime(CLOCK_REALTIME, &deadline) != 0) {
            pthread_mutex_unlock(&queue->mutex);
            return -1;
        }

        deadline.tv_sec += 1;

        int ret = pthread_cond_timedwait(
            &queue->not_empty,
            &queue->mutex,
            &deadline
        );

        if (ret == ETIMEDOUT && queue->count == 0) {
            pthread_mutex_unlock(&queue->mutex);
            return -1;
        }

        if (ret != 0 && ret != ETIMEDOUT) {
            pthread_mutex_unlock(&queue->mutex);
            return -1;
        }
    }

    if (queue->count == 0 && queue->closed) {
        pthread_mutex_unlock(&queue->mutex);
        return 0;
    }

    *message = queue->messages[queue->tail];

    queue->tail = (queue->tail + 1) % SMS_LOG_QUEUE_SIZE;
    queue->count--;
    queue->total_dequeued++;

    pthread_mutex_unlock(&queue->mutex);

    return 1;
}

/*
 * Close queue and wake Logger Thread.
 */
void log_queue_close(LogQueue *queue)
{
    if (queue == NULL)
        return;

    pthread_mutex_lock(&queue->mutex);

    queue->closed = 1;

    pthread_cond_broadcast(&queue->not_empty);

    pthread_mutex_unlock(&queue->mutex);
}

/*
 * Destroy queue after all threads finish.
 */
void log_queue_destroy(LogQueue *queue)
{
    if (queue == NULL)
        return;

    pthread_cond_destroy(&queue->not_empty);
    pthread_mutex_destroy(&queue->mutex);
}

/*
 * Rotate log file:
 *
 * sms.log -> sms.log.1
 * Create a new sms.log.
 */
static FILE *rotate_log(FILE *fp,
                        const char *path,
                        LoggerThreadContext *ctx)
{
    char backup_path[512];

    if (fflush(fp) != 0)
        ctx->write_errors++;

    if (fclose(fp) != 0)
        ctx->write_errors++;

    snprintf(backup_path, sizeof(backup_path),
             "%s.1", path);

    remove(backup_path);

    if (rename(path, backup_path) != 0) {
        perror("[LOGGER] rename");
        ctx->write_errors++;
    } else {
        ctx->log_rotations++;
    }

    FILE *new_fp = fopen(path, "a");

    if (new_fp == NULL) {
        perror("[LOGGER] fopen after rotation");
        ctx->write_errors++;
    }

    return new_fp;
}

/*
 * Logger Thread.
 */
void *logger_thread_func(void *arg)
{
    LoggerThreadContext *ctx = arg;

    if (ctx == NULL || ctx->queue == NULL)
        return NULL;

    const char *path = ctx->log_path;

    if (path == NULL)
        path = SMS_LOG_PATH;

    FILE *fp = fopen(path, "a");

    if (fp == NULL) {
        perror("[LOGGER] fopen");
        ctx->write_errors++;
        return NULL;
    }

    printf("[LOGGER] Thread started, path=%s\n", path);

    /*
     * Use full buffering.
     * Flush is performed periodically.
     */
    setvbuf(fp, NULL, _IOFBF, 8192);

    struct timespec last_flush;
    clock_gettime(CLOCK_MONOTONIC, &last_flush);

    LogMessage message;

    while (1) {
        int ret = log_queue_pop(ctx->queue, &message);

        if (ret == 0)
            break;

        if (ret == 1) {
            size_t len = strlen(message.text);

            /*
             * Check file size before writing.
             * ftell includes the current stream position.
             */
            long position = ftell(fp);

            if (position >= 0 &&
                (unsigned long)position + len + 1 >
                SMS_LOG_MAX_SIZE) {

                fp = rotate_log(fp, path, ctx);

                if (fp == NULL)
                    break;

                setvbuf(fp, NULL, _IOFBF, 8192);
            }

            if (fprintf(fp, "%s\n", message.text) < 0) {
                perror("[LOGGER] fprintf");
                ctx->write_errors++;
            } else {
                ctx->messages_written++;
            }
        }

        /*
         * Flush approximately every 5 seconds.
         */
        struct timespec now;

        if (clock_gettime(CLOCK_MONOTONIC, &now) == 0) {
            if (now.tv_sec - last_flush.tv_sec >=
                SMS_LOG_FLUSH_INTERVAL) {

                if (fflush(fp) != 0) {
                    perror("[LOGGER] fflush");
                    ctx->write_errors++;
                }

                last_flush = now;
            }
        }
    }

    if (fp != NULL) {
        if (fflush(fp) != 0)
            ctx->write_errors++;

        if (fclose(fp) != 0)
            ctx->write_errors++;
    }

    printf("[LOGGER] Thread stopped\n");

    return NULL;
}
