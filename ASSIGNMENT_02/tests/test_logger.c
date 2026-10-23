#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#include "logger_thread.h"

#define TEST_LOG_PATH "/tmp/sms_logger_test.log"
#define TEST_MESSAGES 10000

static LogQueue log_queue;
static LoggerThreadContext logger_ctx;

int main(void)
{
    pthread_t logger_tid;

    /* Remove old test files */
    remove(TEST_LOG_PATH);
    remove(TEST_LOG_PATH ".1");

    if (log_queue_init(&log_queue) != 0) {
        fprintf(stderr, "[TEST] Failed to initialize log queue\n");
        return 1;
    }

    logger_ctx.queue = &log_queue;
    logger_ctx.log_path = TEST_LOG_PATH;

    if (pthread_create(&logger_tid, NULL,
                       logger_thread_func, &logger_ctx) != 0) {
        fprintf(stderr, "[TEST] Failed to start Logger Thread\n");
        log_queue_destroy(&log_queue);
        return 1;
    }

    printf("[TEST] Sending %d log messages...\n", TEST_MESSAGES);

    for (int i = 0; i < TEST_MESSAGES; i++) {
        char message[256];

        snprintf(message, sizeof(message),
                 "[DATA] Sample=%05d "
                 "Temp=35.5 Humid=75.5 "
                 "Test payload: "
                 "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                 "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                 "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
                 i);

        /*
         * Retry if queue is temporarily full.
         */
        while (!log_queue_push(&log_queue, message)) {
            usleep(1000);
        }
    }

    /* Signal that no more messages will be sent */
    log_queue_close(&log_queue);

    pthread_join(logger_tid, NULL);

    printf("\n=== LOGGER TEST RESULTS ===\n");

    printf("Messages written : %llu\n",
           (unsigned long long)logger_ctx.messages_written);

    printf("Log rotations    : %llu\n",
           (unsigned long long)logger_ctx.log_rotations);

    printf("Write errors     : %llu\n",
           (unsigned long long)logger_ctx.write_errors);

    printf("Queue enqueued   : %llu\n",
           (unsigned long long)log_queue.total_enqueued);

    printf("Queue dequeued   : %llu\n",
           (unsigned long long)log_queue.total_dequeued);

    printf("Queue dropped    : %llu\n",
           (unsigned long long)log_queue.dropped_messages);

    int passed =
        logger_ctx.messages_written == TEST_MESSAGES &&
        logger_ctx.write_errors == 0 &&
        log_queue.total_enqueued == TEST_MESSAGES &&
        log_queue.total_dequeued == TEST_MESSAGES;

    printf("\nTEST RESULT: %s\n", passed ? "PASS" : "FAIL");

    log_queue_destroy(&log_queue);

    return passed ? 0 : 1;
}
