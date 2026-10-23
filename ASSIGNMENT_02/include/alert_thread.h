#ifndef ALERT_THREAD_H
#define ALERT_THREAD_H

#include <stdint.h>
#include <mqueue.h>

#include "sms_common.h"
#include "logger_thread.h"

typedef struct {
    mqd_t alarm_queue;

    LogQueue *log_queue;

    uint64_t alerts_received;

    uint64_t temp_high_count;
    uint64_t humid_high_count;
    uint64_t temp_spike_count;

} AlertThreadContext;

void *alert_thread_func(void *arg);

#endif
