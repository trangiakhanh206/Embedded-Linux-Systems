#ifndef PROCESSOR_THREAD_H
#define PROCESSOR_THREAD_H

#include <stdint.h>
#include <mqueue.h>

#include "shared_buffer.h"
#include "sms_common.h"
#include "logger_thread.h"

#define SMS_WINDOW_SIZE 10

typedef struct {
    SharedBuffer *buffer;

    mqd_t alarm_queue;

    LogQueue *log_queue;

    float temp_threshold;
    float humid_threshold;

    uint64_t samples_processed;
    uint64_t alerts_generated;

    float avg_temperature;
    float avg_humidity;

} ProcessorThreadContext;

void *processor_thread_func(void *arg);

#endif
