#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <mqueue.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>

#include "sms_common.h"
#include "shared_buffer.h"
#include "processor_thread.h"
#include "alert_thread.h"

#define TEST_QUEUE_NAME "/sms_test_alarm"

int main(void)
{
    SharedBuffer buffer;
    pthread_t processor_tid;
    pthread_t alert_tid;

    struct mq_attr attr;
    mqd_t mq;

    /* Create shared buffer */
    if (shared_buffer_init(&buffer, SMS_BUFFER_SIZE) != 0) {
        fprintf(stderr, "Failed to initialize buffer\n");
        return 1;
    }

    /* Configure POSIX Message Queue */
    memset(&attr, 0, sizeof(attr));

    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = sizeof(AlertMessage);

    mq_unlink(TEST_QUEUE_NAME);

    mq = mq_open(TEST_QUEUE_NAME,
                 O_CREAT | O_EXCL | O_RDWR,
                 0600,
                 &attr);

    if (mq == (mqd_t)-1) {
        perror("mq_open");
        shared_buffer_destroy(&buffer);
        return 1;
    }

    ProcessorThreadContext processor_ctx = {
        .buffer = &buffer,
        .alarm_queue = mq,
        .temp_threshold = 40.0f,
        .humid_threshold = 80.0f
    };

    AlertThreadContext alert_ctx = {
        .alarm_queue = mq
    };

    /* Start Alert Thread */
    if (pthread_create(&alert_tid, NULL,
                       alert_thread_func, &alert_ctx) != 0) {
        fprintf(stderr, "Failed to create Alert Thread\n");
        mq_close(mq);
        mq_unlink(TEST_QUEUE_NAME);
        shared_buffer_destroy(&buffer);
        return 1;
    }

    /* Start Processor Thread */
    if (pthread_create(&processor_tid, NULL,
                       processor_thread_func,
                       &processor_ctx) != 0) {
        fprintf(stderr, "Failed to create Processor Thread\n");

        AlertMessage stop = {
            .timestamp_ms = UINT64_MAX
        };

        mq_send(mq, (const char *)&stop, sizeof(stop), 0);

        pthread_join(alert_tid, NULL);

        mq_close(mq);
        mq_unlink(TEST_QUEUE_NAME);
        shared_buffer_destroy(&buffer);
        return 1;
    }

    printf("[TEST] Sending 3 sensor samples...\n");

    SensorData samples[] = {
        {1000, 25.0f, 50.0f},
        {1500, 43.0f, 85.0f},
        {2000, 30.0f, 60.0f}
    };

    for (int i = 0; i < 3; i++) {
        if (!shared_buffer_push(&buffer, &samples[i])) {
            fprintf(stderr, "Failed to push sample %d\n", i);
        }
    }

    /* Signal processor that no more samples will arrive */
    shared_buffer_close(&buffer);

    /* Wait until processor finishes sending alerts */
    pthread_join(processor_tid, NULL);

    /* Send shutdown message to Alert Thread */
    AlertMessage stop = {
        .timestamp_ms = UINT64_MAX
    };

    if (mq_send(mq, (const char *)&stop,
                sizeof(stop), 0) == -1) {
        perror("mq_send shutdown");
    }

    pthread_join(alert_tid, NULL);

    printf("\n=== PROCESSOR & ALERT TEST RESULTS ===\n");

    printf("Samples processed : %llu\n",
           (unsigned long long)processor_ctx.samples_processed);

    printf("Alerts generated  : %llu\n",
           (unsigned long long)processor_ctx.alerts_generated);

    printf("Alerts received   : %llu\n",
           (unsigned long long)alert_ctx.alerts_received);

    printf("TEMP_HIGH count   : %llu\n",
           (unsigned long long)alert_ctx.temp_high_count);

    printf("HUMID_HIGH count  : %llu\n",
           (unsigned long long)alert_ctx.humid_high_count);

    printf("TEMP_SPIKE count  : %llu\n",
           (unsigned long long)alert_ctx.temp_spike_count);

    int passed =
        processor_ctx.samples_processed == 3 &&
        processor_ctx.alerts_generated == 3 &&
        alert_ctx.alerts_received == 3 &&
        alert_ctx.temp_high_count == 1 &&
        alert_ctx.humid_high_count == 1 &&
        alert_ctx.temp_spike_count == 1;

    printf("\nTEST RESULT: %s\n",
           passed ? "PASS" : "FAIL");

    mq_close(mq);
    mq_unlink(TEST_QUEUE_NAME);

    shared_buffer_destroy(&buffer);

    return passed ? 0 : 1;
}
