#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>

#include "shared_buffer.h"

#define TEST_SAMPLES 100

static SharedBuffer buffer;

/* Producer Thread */
static void *producer_thread(void *arg)
{
    (void)arg;

    for (int i = 0; i < TEST_SAMPLES; i++) {
        SensorData sample = {
            .timestamp_ms = (uint64_t)i,
            .temperature = 25.0f + i * 0.1f,
            .humidity = 60.0f
        };

        while (!shared_buffer_push(&buffer, &sample)) {
            usleep(1000);
        }

        printf("[PRODUCER] Sample %d inserted\n", i);
    }

    shared_buffer_close(&buffer);

    return NULL;
}

/* Consumer Thread */
static void *consumer_thread(void *arg)
{
    (void)arg;

    SensorData sample;

    while (shared_buffer_pop(&buffer, &sample)) {
        printf("[CONSUMER] Timestamp=%llu Temp=%.1f Humid=%.1f\n",
               (unsigned long long)sample.timestamp_ms,
               sample.temperature,
               sample.humidity);

        usleep(2000);
    }

    return NULL;
}

int main(void)
{
    pthread_t producer;
    pthread_t consumer;

    BufferStats stats;

    if (shared_buffer_init(&buffer, SMS_BUFFER_SIZE) != 0) {
        fprintf(stderr, "Failed to initialize shared buffer\n");
        return 1;
    }

    if (pthread_create(&consumer, NULL, consumer_thread, NULL) != 0) {
        fprintf(stderr, "Failed to create consumer thread\n");
        shared_buffer_destroy(&buffer);
        return 1;
    }

    if (pthread_create(&producer, NULL, producer_thread, NULL) != 0) {
        fprintf(stderr, "Failed to create producer thread\n");
        shared_buffer_close(&buffer);
        pthread_join(consumer, NULL);
        shared_buffer_destroy(&buffer);
        return 1;
    }

    pthread_join(producer, NULL);
    pthread_join(consumer, NULL);

    shared_buffer_get_stats(&buffer, &stats);

    printf("\n=== SHARED BUFFER TEST RESULTS ===\n");
    printf("Current size   : %zu\n", stats.current_size);
    printf("Total produced : %llu\n",
           (unsigned long long)stats.total_produced);
    printf("Total consumed : %llu\n",
           (unsigned long long)stats.total_consumed);
    printf("Overflow count : %llu\n",
           (unsigned long long)stats.overflow_count);

    shared_buffer_destroy(&buffer);

    return 0;
}
