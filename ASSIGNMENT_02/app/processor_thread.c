#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <mqueue.h>
#include "logger_thread.h"
#include "processor_thread.h"

#define WINDOW_SIZE 10

/*
 * Send an alert through POSIX message queue.
 */
static void send_alert(ProcessorThreadContext *ctx,
                       uint64_t timestamp,
                       AlertType type,
                       float value,
                       float threshold)
{
    AlertMessage alert = {
        .timestamp_ms = timestamp,
        .type = type,
        .value = value,
        .threshold = threshold
    };

    if (mq_send(ctx->alarm_queue,
                (const char *)&alert,
                sizeof(alert),
                0) == -1) {
        perror("[PROCESSOR] mq_send");
        return;
    }

    ctx->alerts_generated++;
}

/*
 * Processor Thread
 */
void *processor_thread_func(void *arg)
{
    ProcessorThreadContext *ctx = arg;

    if (ctx == NULL || ctx->buffer == NULL)
        return NULL;

    float temp_window[WINDOW_SIZE] = {0};
    float humid_window[WINDOW_SIZE] = {0};

    int window_count = 0;
    int window_index = 0;

    float temp_sum = 0.0f;
    float humid_sum = 0.0f;

    float previous_temp = 0.0f;
    int has_previous = 0;

    SensorData sample;

    printf("[PROCESSOR] Thread started\n");

    while (shared_buffer_pop(ctx->buffer, &sample)) {

        /*
         * Update rolling average.
         */
        if (window_count == WINDOW_SIZE) {
            temp_sum -= temp_window[window_index];
            humid_sum -= humid_window[window_index];
        } else {
            window_count++;
        }

        temp_window[window_index] = sample.temperature;
        humid_window[window_index] = sample.humidity;

        temp_sum += sample.temperature;
        humid_sum += sample.humidity;

        window_index = (window_index + 1) % WINDOW_SIZE;

        ctx->avg_temperature = temp_sum / window_count;
        ctx->avg_humidity = humid_sum / window_count;

        ctx->samples_processed++;
        /*
 * Send processed sensor data to Logger Thread.
 */
if (ctx->log_queue != NULL) {
    char log_message[256];

    snprintf(log_message, sizeof(log_message),
             "[DATA] Time=%llu "
             "Temp=%.1f Humid=%.1f "
             "AvgTemp=%.1f AvgHumid=%.1f",
             (unsigned long long)sample.timestamp_ms,
             sample.temperature,
             sample.humidity,
             ctx->avg_temperature,
             ctx->avg_humidity);

    if (!log_queue_push(ctx->log_queue, log_message)) {
        fprintf(stderr,
                "[PROCESSOR] Warning: log queue full or closed\n");
    }
}
        /*
         * Detect high temperature.
         */
        if (sample.temperature > ctx->temp_threshold) {
            send_alert(ctx,
                       sample.timestamp_ms,
                       ALERT_TEMP_HIGH,
                       sample.temperature,
                       ctx->temp_threshold);
        }

        /*
         * Detect high humidity.
         */
        if (sample.humidity > ctx->humid_threshold) {
            send_alert(ctx,
                       sample.timestamp_ms,
                       ALERT_HUMID_HIGH,
                       sample.humidity,
                       ctx->humid_threshold);
        }

        /*
         * Detect temperature spike.
         */
        if (has_previous &&
            sample.temperature - previous_temp > 5.0f) {

            send_alert(ctx,
                       sample.timestamp_ms,
                       ALERT_TEMP_SPIKE,
                       sample.temperature - previous_temp,
                       5.0f);
        }

        previous_temp = sample.temperature;
        has_previous = 1;

        printf("[PROCESSOR] Time=%llu "
               "Temp=%.1f AvgTemp=%.1f "
               "Humid=%.1f AvgHumid=%.1f\n",
               (unsigned long long)sample.timestamp_ms,
               sample.temperature,
               ctx->avg_temperature,
               sample.humidity,
               ctx->avg_humidity);
    }

    printf("[PROCESSOR] Thread stopped\n");

    return NULL;
}
