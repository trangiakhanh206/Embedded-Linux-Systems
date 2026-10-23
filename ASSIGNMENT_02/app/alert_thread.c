#include <stdio.h>
#include <errno.h>
#include <mqueue.h>
#include <string.h>

#include "alert_thread.h"

/*
 * Convert alert type to readable text.
 */
static const char *alert_type_name(AlertType type)
{
    switch (type) {
        case ALERT_TEMP_HIGH:
            return "TEMP_HIGH";

        case ALERT_HUMID_HIGH:
            return "HUMID_HIGH";

        case ALERT_TEMP_SPIKE:
            return "TEMP_SPIKE";

        default:
            return "UNKNOWN";
    }
}

/*
 * Alert Thread receives alarms from POSIX message queue.
 *
 * A shutdown message is identified by timestamp_ms == UINT64_MAX.
 */
void *alert_thread_func(void *arg)
{
    AlertThreadContext *ctx = (AlertThreadContext *)arg;

    if (ctx == NULL)
        return NULL;

    printf("[ALERT] Thread started\n");

    while (1) {
        AlertMessage alert;

        ssize_t n = mq_receive(
            ctx->alarm_queue,
            (char *)&alert,
            sizeof(alert),
            NULL
        );

        if (n < 0) {
            if (errno == EINTR)
                continue;

            perror("[ALERT] mq_receive");
            break;
        }

        if ((size_t)n != sizeof(AlertMessage)) {
            fprintf(stderr,
                    "[ALERT] Invalid message size\n");
            continue;
        }

        /*
         * Shutdown signal.
         */
        if (alert.timestamp_ms == UINT64_MAX) {
            printf("[ALERT] Shutdown message received\n");
            break;
        }

        ctx->alerts_received++;

        switch (alert.type) {
            case ALERT_TEMP_HIGH:
                ctx->temp_high_count++;
                break;

            case ALERT_HUMID_HIGH:
                ctx->humid_high_count++;
                break;

            case ALERT_TEMP_SPIKE:
                ctx->temp_spike_count++;
                break;

            default:
                break;
        }

        printf("[ALERT][%llu] TYPE=%s VALUE=%.1f THRESHOLD=%.1f\n",
               (unsigned long long)alert.timestamp_ms,
               alert_type_name(alert.type),
               alert.value,
               alert.threshold);
  /*
 * Forward alert message to Logger Thread.
 */
if (ctx->log_queue != NULL) {
    char log_message[256];

    snprintf(log_message, sizeof(log_message),
             "[ALERT] Time=%llu TYPE=%s "
             "VALUE=%.1f THRESHOLD=%.1f",
             (unsigned long long)alert.timestamp_ms,
             alert_type_name(alert.type),
             alert.value,
             alert.threshold);

    if (!log_queue_push(ctx->log_queue, log_message)) {
        fprintf(stderr,
                "[ALERT] Warning: log queue full or closed\n");
    }
} 
 }

    printf("[ALERT] Thread stopped\n");

    return NULL;
}
