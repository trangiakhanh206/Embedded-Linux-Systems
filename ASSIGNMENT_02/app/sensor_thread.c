#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <stdatomic.h>

#include "sensor_thread.h"
#include "sms_common.h"

/* Read one CSV sample from sensor device */
static int read_sensor(SensorData *sample)
{
#ifdef SMS_HOST_SIMULATION

    /*
     * Generate simulated sensor data for Valgrind testing.
     * No kernel driver is required on Ubuntu Host.
     */
    static uint64_t sample_counter = 0;

    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        perror("[SENSOR] clock_gettime");
        return -1;
    }

    sample->timestamp_ms =
        (uint64_t)now.tv_sec * 1000ULL +
        (uint64_t)now.tv_nsec / 1000000ULL;

    /*
     * Temperature range: 15.0 - 45.0 C
     * Humidity range: 30.0 - 90.0 %
     */
    sample->temperature =
        15.0f + (float)((sample_counter * 17U) % 301U) / 10.0f;

    sample->humidity =
        30.0f + (float)((sample_counter * 23U) % 601U) / 10.0f;

    sample_counter++;

    return 1;

#else

    char buffer[128];

    int fd = open(SMS_DEVICE_PATH, O_RDONLY);

    if (fd < 0) {
        perror("[SENSOR] open");
        return -1;
    }

    ssize_t n;
int saved_errno = 0;

/*
 * Retry read() if interrupted by a signal.
 */
do {
    n = read(fd, buffer, sizeof(buffer) - 1);
} while (n < 0 && errno == EINTR);

/*
 * Preserve errno before closing the file descriptor.
 */
if (n < 0)
    saved_errno = errno;

if (close(fd) < 0) {
    perror("[SENSOR] close");
    return -1;
}

if (n < 0) {
    if (saved_errno == EAGAIN)
        return 0;

    errno = saved_errno;
    perror("[SENSOR] read");
    return -1;
}

    if (n == 0)
        return 0;

    buffer[n] = '\0';

    unsigned long long timestamp;
    float temperature;
    float humidity;

    if (sscanf(buffer, "%llu,%f,%f",
               &timestamp,
               &temperature,
               &humidity) != 3) {
        fprintf(stderr,
                "[SENSOR] Invalid CSV: %s\n",
                buffer);
        return -1;
    }

    sample->timestamp_ms = (uint64_t)timestamp;
    sample->temperature = temperature;
    sample->humidity = humidity;

    return 1;

#endif
}
/* Sensor Thread */
/* Sensor Thread using absolute-time periodic scheduling */
void *sensor_thread_func(void *arg)
{
    SensorThreadContext *ctx = (SensorThreadContext *)arg;

    if (ctx == NULL || ctx->buffer == NULL ||
        ctx->running == NULL || ctx->interval_ms == 0)
        return NULL;

    printf("[SENSOR] Thread started, interval=%u ms\n",
           ctx->interval_ms);

    struct timespec next_wakeup;

    if (clock_gettime(CLOCK_MONOTONIC, &next_wakeup) != 0) {
        perror("[SENSOR] clock_gettime");
        ctx->read_errors++;
        return NULL;
    }

    const long long interval_ns =
        (long long)ctx->interval_ms * 1000000LL;

    while (atomic_load(ctx->running)) {

        SensorData sample;

        int ret = read_sensor(&sample);

        if (ret == 1) {
            ctx->samples_read++;

            if (!shared_buffer_push(ctx->buffer, &sample)) {
                ctx->dropped_samples++;

                fprintf(stderr,
                        "[SENSOR] Warning: shared buffer full\n");
            }

        } else if (ret < 0) {
            ctx->read_errors++;
        }

        /*
         * Calculate the next absolute wake-up time.
         */
        next_wakeup.tv_sec +=
            interval_ns / 1000000000LL;

        next_wakeup.tv_nsec +=
            interval_ns % 1000000000LL;

        if (next_wakeup.tv_nsec >= 1000000000L) {
            next_wakeup.tv_sec++;
            next_wakeup.tv_nsec -= 1000000000L;
        }

        /*
         * Sleep until the absolute deadline.
         * EINTR is handled by retrying the same deadline.
         */
        int sleep_ret;

        do {
            sleep_ret = clock_nanosleep(
                CLOCK_MONOTONIC,
                TIMER_ABSTIME,
                &next_wakeup,
                NULL
            );
        } while (sleep_ret == EINTR &&
                 atomic_load(ctx->running));

         if (sleep_ret == EINTR) {
            /* Stop requested while sleeping: no jitter sample */
            break;
        }

        if (sleep_ret != 0) {
            fprintf(stderr,
                    "[SENSOR] clock_nanosleep failed: %s\n",
                    strerror(sleep_ret));
            ctx->read_errors++;
            break;
        }

        /*
         * Task 4: Measure sensor wake-up latency.
         * Jitter = actual wake-up time - expected wake-up time.
         */
        struct timespec actual_wakeup;

        if (clock_gettime(CLOCK_MONOTONIC, &actual_wakeup) != 0) {
            perror("[SENSOR] jitter clock_gettime");
            break;
        }

        long long jitter_ns =
            ((long long)actual_wakeup.tv_sec -
             (long long)next_wakeup.tv_sec) * 1000000000LL
            +
            ((long long)actual_wakeup.tv_nsec -
             (long long)next_wakeup.tv_nsec);

        double jitter_us = (double)jitter_ns / 1000.0;

        /* Store jitter without printing in the real-time loop */
        if (ctx->jitter_count < SMS_MAX_JITTER_SAMPLES) {
            ctx->jitter_samples_us[ctx->jitter_count] = jitter_us;
            ctx->jitter_count++;
        } else {
            ctx->jitter_overflow++;
        }
    }

    printf("[SENSOR] Thread stopped\n");

    return NULL;
}
