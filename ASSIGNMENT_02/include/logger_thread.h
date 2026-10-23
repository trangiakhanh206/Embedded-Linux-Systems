#ifndef LOGGER_THREAD_H
#define LOGGER_THREAD_H

#include <pthread.h>
#include <stdint.h>
#include <stddef.h>

#define SMS_LOG_QUEUE_SIZE 256
#define SMS_LOG_MESSAGE_SIZE 256

#define SMS_LOG_MAX_SIZE (1024 * 1024)
#define SMS_LOG_FLUSH_INTERVAL 5

typedef struct {
    char text[SMS_LOG_MESSAGE_SIZE];
} LogMessage;

/*
 * Thread-safe queue used by Processor Thread
 * and Alert Thread to send log messages.
 */
typedef struct {
    LogMessage messages[SMS_LOG_QUEUE_SIZE];

    size_t head;
    size_t tail;
    size_t count;

    uint64_t total_enqueued;
    uint64_t total_dequeued;
    uint64_t dropped_messages;

    int closed;

    pthread_mutex_t mutex;
    pthread_cond_t not_empty;

} LogQueue;

typedef struct {
    LogQueue *queue;

    const char *log_path;

    uint64_t messages_written;
    uint64_t log_rotations;
    uint64_t write_errors;

} LoggerThreadContext;

/* Initialize log queue */
int log_queue_init(LogQueue *queue);

/* Add one log message */
int log_queue_push(LogQueue *queue, const char *message);

/* Receive one log message */
int log_queue_pop(LogQueue *queue, LogMessage *message);

/* Close queue and wake waiting threads */
void log_queue_close(LogQueue *queue);

/* Release synchronization objects */
void log_queue_destroy(LogQueue *queue);

/* Logger Thread entry point */
void *logger_thread_func(void *arg);

#endif
