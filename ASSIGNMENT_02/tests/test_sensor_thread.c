#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>

#include "sensor_thread.h"
#include "shared_buffer.h"
#include "sms_common.h"

static SharedBuffer buffer;
static atomic_int running = 1;

/* Consumer thread for testing */
static void *consumer_thread(void *arg)
{
    (void)arg;

    SensorData sample;

    while (shared_buffer_pop(&buffer, &sample)) {
        printf("[CONSUMER] Time=%llu Temp=%.1f C Humid=%.1f %%\n",
               (unsigned long long)sample.timestamp_ms,
               sample.temperature,
               sample.humidity);
    }

    return NULL;
}

int main(void)
{
    pthread_t sensor_tid;
    pthread_t consumer_tid;

    SensorThreadContext sensor_ctx = {
        .buffer = &buffer,
        .interval_ms = 500,
        .running = &running,
        .samples_read = 0,
        .read_errors = 0,
        .dropped_samples = 0
    };

    if (shared_buffer_init(&buffer, SMS_BUFFER_SIZE) != 0) {
        fprintf(stderr, "Failed to initialize buffer\n");
        return 1;
    }

    if (pthread_create(&consumer_tid, NULL,
                       consumer_thread, NULL) != 0) {
        fprintf(stderr, "Failed to create consumer thread\n");
        shared_buffer_destroy(&buffer);
        return 1;
    }

    if (pthread_create(&sensor_tid, NULL,
                       sensor_thread_func, &sensor_ctx) != 0) {
        fprintf(stderr, "Failed to create sensor thread\n");

        shared_buffer_close(&buffer);
        pthread_join(consumer_tid, NULL);
        shared_buffer_destroy(&buffer);

        return 1;
    }

    printf("[TEST] Sensor Thread running for 10 seconds...\n");

    sleep(10);

    atomic_store(&running, 0);

    pthread_join(sensor_tid, NULL);

    shared_buffer_close(&buffer);
    pthread_join(consumer_tid, NULL);

    BufferStats stats;
    shared_buffer_get_stats(&buffer, &stats);

    printf("\n=== SENSOR THREAD TEST RESULTS ===\n");
    printf("Samples read     : %llu\n",
           (unsigned long long)sensor_ctx.samples_read);
    printf("Read errors      : %llu\n",
           (unsigned long long)sensor_ctx.read_errors);
    printf("Dropped samples  : %llu\n",
           (unsigned long long)sensor_ctx.dropped_samples);
    printf("Total produced   : %llu\n",
           (unsigned long long)stats.total_produced);
    printf("Total consumed   : %llu\n",
           (unsigned long long)stats.total_consumed);
    printf("Buffer overflows : %llu\n",
           (unsigned long long)stats.overflow_count);

    shared_buffer_destroy(&buffer);

    return 0;
}
