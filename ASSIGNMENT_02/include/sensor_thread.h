#ifndef SENSOR_THREAD_H
#define SENSOR_THREAD_H

#include <pthread.h>
#include <stdint.h>
#include <stdatomic.h>
#include "shared_buffer.h"

/* Maximum number of recorded jitter samples */
#define SMS_MAX_JITTER_SAMPLES 2000U

typedef struct {
    SharedBuffer *buffer;

    unsigned int interval_ms;

    atomic_int *running;

    uint64_t samples_read;
    uint64_t read_errors;
    uint64_t dropped_samples;

    /* Task 4: Real-time jitter measurements */
    double jitter_samples_us[SMS_MAX_JITTER_SAMPLES];

    uint64_t jitter_count;
    uint64_t jitter_overflow;

} SensorThreadContext;

/* Sensor Thread entry point */
void *sensor_thread_func(void *arg);

#endif
