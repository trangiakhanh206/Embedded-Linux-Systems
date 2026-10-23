#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <stdatomic.h>
#include <pthread.h>
#include <mqueue.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sched.h>
#include <time.h>
#include <math.h>

#include "sms_common.h"
#include "shared_buffer.h"
#include "sensor_thread.h"
#include "processor_thread.h"
#include "alert_thread.h"
#include "logger_thread.h"

#define SMS_THREAD_STACK_SIZE (256U * 1024U)
#define SMS_ALARM_QUEUE_NAME "/sms_alarm_queue"

#define SMS_IOCTL_MAGIC 'S'
#define SMS_SET_INTERVAL \
    _IOW(SMS_IOCTL_MAGIC, 1, unsigned int)

/* Shared application resources */
static SharedBuffer shared_buffer;
static LogQueue log_queue;

static atomic_int running = ATOMIC_VAR_INIT(1);
static volatile sig_atomic_t stop_requested = 0;

/* Signal handler: only set a flag */
static void handle_signal(int signum)
{
    (void)signum;
    stop_requested = 1;
}

/* Display command-line usage */
static void print_usage(const char *program)
{
    printf("Usage: %s [OPTIONS]\n", program);
    printf("  -i <ms>   Sensor interval (default: 500)\n");
    printf("  -t <C>    Temperature threshold (default: 40.0)\n");
    printf("  -h <%%>    Humidity threshold (default: 80.0)\n");
    printf("  -d <sec>  Duration (0 = infinite, default: 60)\n");
    printf("  -v        Verbose mode\n");
    printf("  -r <mode> Scheduling mode: fifo or other (default: fifo)\n");
}

/* Read a positive or zero integer */
static int parse_uint(const char *text, unsigned int *value)
{
    char *end = NULL;
    unsigned long result;

    errno = 0;
    result = strtoul(text, &end, 10);

    if (errno != 0 || text[0] == '\0' ||
        text[0] == '-' || *end != '\0' ||
        result > UINT32_MAX) {
        return -1;
    }

    *value = (unsigned int)result;
    return 0;
}

/* Read a floating-point parameter */
static int parse_float(const char *text, float *value)
{
    char *end = NULL;
    float result;

    errno = 0;
    result = strtof(text, &end);

    if (errno != 0 || text[0] == '\0' ||
        *end != '\0' || !isfinite(result)) {
        return -1;
    }

    *value = result;
    return 0;
}

/* Configure driver's read interval */
static int configure_sensor(unsigned int interval_ms)
{
#ifdef SMS_HOST_SIMULATION

    printf("[APP] HOST SIMULATION MODE enabled\n");
    printf("[APP] Simulated sensor interval: %u ms\n",
           interval_ms);

    return 0;

#else

    int fd = open(SMS_DEVICE_PATH, O_RDONLY);

    if (fd < 0) {
        perror("[APP] open sensor");
        return -1;
    }

    if (ioctl(fd, SMS_SET_INTERVAL, &interval_ms) < 0) {
        perror("[APP] ioctl SMS_SET_INTERVAL");
        close(fd);
        return -1;
    }

    close(fd);

    printf("[APP] Sensor interval: %u ms\n", interval_ms);

    return 0;

#endif
}

/*
 * Lock current and future memory pages to reduce
 * paging-related latency in real-time execution.
 *
 * Return:
 * 1 = memory locking succeeded
 * 0 = memory locking failed
 */
static int configure_memory_lock(void)
{
    if (mlockall(MCL_CURRENT | MCL_FUTURE) == 0) {
        printf("[RT] mlockall: SUCCESS\n");
        return 1;
    }

    fprintf(stderr,
            "[RT] mlockall failed: %s\n",
            strerror(errno));

    fprintf(stderr,
            "[RT] Continuing without memory locking\n");

    return 0;
}

/* Attempt real-time scheduling */
/* Configure and verify Sensor Thread scheduling policy */
static int configure_realtime(pthread_t thread, int rt_mode_fifo)
{
    struct sched_param param = {0};
    struct sched_param actual_param = {0};

    int policy = rt_mode_fifo ? SCHED_FIFO : SCHED_OTHER;
    int actual_policy = -1;

    param.sched_priority = rt_mode_fifo ? 50 : 0;

    int ret = pthread_setschedparam(thread, policy, &param);

    if (ret != 0) {
        fprintf(stderr,
                "[RT] pthread_setschedparam failed: %s\n",
                strerror(ret));
    }

    ret = pthread_getschedparam(
        thread,
        &actual_policy,
        &actual_param
    );

    if (ret != 0) {
        fprintf(stderr,
                "[RT] pthread_getschedparam failed: %s\n",
                strerror(ret));
        return -1;
    }

    const char *actual_name =
        actual_policy == SCHED_FIFO ? "SCHED_FIFO" :
        actual_policy == SCHED_OTHER ? "SCHED_OTHER" :
        actual_policy == SCHED_RR ? "SCHED_RR" :
        "UNKNOWN";

    printf("[RT] Sensor actual policy: %s, priority=%d\n",
           actual_name,
           actual_param.sched_priority);

    if (actual_policy != policy ||
        actual_param.sched_priority != param.sched_priority) {

        fprintf(stderr,
                "[RT] ERROR: Requested scheduling policy not active\n");

        return -1;
    }

    printf("[RT] Scheduling policy verified successfully\n");

    return 0;
}
/*
 * Task 4: Export recorded sensor wake-up jitter.
 * Called only after the Sensor Thread has stopped.
 */
static int export_jitter_csv(const SensorThreadContext *ctx)
{
    if (ctx == NULL)
        return -1;

    const char *path = "logs/task4/jitter_samples.csv";

    FILE *fp = fopen(path, "w");

    if (fp == NULL) {
        perror("[RT] jitter CSV fopen");
        return -1;
    }

    if (fprintf(fp, "sample,jitter_us\n") < 0) {
        fclose(fp);
        return -1;
    }

    for (uint64_t i = 0; i < ctx->jitter_count; i++) {
        if (fprintf(fp, "%llu,%.3f\n",
                    (unsigned long long)(i + 1),
                    ctx->jitter_samples_us[i]) < 0) {
            fclose(fp);
            return -1;
        }
    }

    if (fclose(fp) != 0) {
        perror("[RT] jitter CSV fclose");
        return -1;
    }

    printf("[RT] Jitter CSV saved: %s\n", path);
    printf("[RT] Jitter samples: %llu\n",
           (unsigned long long)ctx->jitter_count);
    printf("[RT] Jitter overflow: %llu\n",
           (unsigned long long)ctx->jitter_overflow);

    return 0;
}
/* Monotonic time in seconds */
static double monotonic_seconds(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return -1.0;

    return (double)ts.tv_sec +
           (double)ts.tv_nsec / 1000000000.0;
}

int main(int argc, char *argv[])
{
int memory_locked = 0;

    pthread_attr_t worker_attr;
    int worker_attr_ready = 0;
    int rt_mode_fifo = 1;
    unsigned int interval_ms = SMS_DEFAULT_INTERVAL_MS;
    unsigned int duration = SMS_DEFAULT_DURATION;

    float temp_threshold = SMS_DEFAULT_TEMP_THRESHOLD;
    float humid_threshold = SMS_DEFAULT_HUMID_THRESHOLD;

    int verbose = 0;
    int opt;
    int ret;
    int exit_status = EXIT_FAILURE;

    int buffer_ready = 0;
    int log_ready = 0;

    int logger_started = 0;
    int alert_started = 0;
    int processor_started = 0;
    int sensor_started = 0;

    pthread_t sensor_tid;
    pthread_t processor_tid;
    pthread_t alert_tid;
    pthread_t logger_tid;

    mqd_t alarm_mq = (mqd_t)-1;

    struct mq_attr attr;
    struct sigaction sa;

    SensorThreadContext sensor_ctx = {0};
    ProcessorThreadContext processor_ctx = {0};
    AlertThreadContext alert_ctx = {0};
    LoggerThreadContext logger_ctx = {0};

    BufferStats buffer_stats = {0};

    /* Parse command-line options */
    while ((opt = getopt(argc, argv, "i:t:h:d:vr:")) != -1) {
        switch (opt) {
        case 'i':
            if (parse_uint(optarg, &interval_ms) != 0 ||
                interval_ms < 10 || interval_ms > 60000) {
                fprintf(stderr, "Invalid interval\n");
                return EXIT_FAILURE;
            }
            break;

        case 't':
            if (parse_float(optarg, &temp_threshold) != 0) {
                fprintf(stderr, "Invalid temperature threshold\n");
                return EXIT_FAILURE;
            }
            break;

        case 'h':
            if (parse_float(optarg, &humid_threshold) != 0) {
                fprintf(stderr, "Invalid humidity threshold\n");
                return EXIT_FAILURE;
            }
            break;

        case 'd':
            if (parse_uint(optarg, &duration) != 0 ||
                duration > 86400) {
                fprintf(stderr, "Invalid duration\n");
                return EXIT_FAILURE;
            }
            break;

        case 'v':
            verbose = 1;
            break;
         case 'r':
            if (strcmp(optarg, "fifo") == 0) {
                rt_mode_fifo = 1;
            } else if (strcmp(optarg, "other") == 0) {
                rt_mode_fifo = 0;
            } else {
                fprintf(stderr,
                        "Invalid scheduling mode: %s\n",
                        optarg);
                return EXIT_FAILURE;
            }
            break;
        default:
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (optind != argc) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    printf("\n=== SENSOR MONITORING SYSTEM ===\n");
    printf("Interval    : %u ms\n", interval_ms);
    printf("Temp limit  : %.1f C\n", temp_threshold);
    printf("Humid limit : %.1f %%\n", humid_threshold);
    printf("Duration    : %u seconds\n", duration);
    printf("RT Mode     : %s\n",
        rt_mode_fifo ? "SCHED_FIFO" : "SCHED_OTHER");
    if (verbose)
        printf("[APP] Verbose option enabled\n");

    /* Install signal handlers */
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGINT, &sa, NULL) != 0 ||
        sigaction(SIGTERM, &sa, NULL) != 0) {
        perror("[APP] sigaction");
        return EXIT_FAILURE;
    }

    /* Initialize shared circular buffer */
    if (shared_buffer_init(
            &shared_buffer, SMS_BUFFER_SIZE) != 0) {
        fprintf(stderr, "[APP] Shared buffer init failed\n");
        goto cleanup;
    }

    buffer_ready = 1;

    /* Initialize log queue */
    if (log_queue_init(&log_queue) != 0) {
        fprintf(stderr, "[APP] Log queue init failed\n");
        goto cleanup;
    }

    log_ready = 1;

    /* Create POSIX alarm message queue */
    memset(&attr, 0, sizeof(attr));

    attr.mq_maxmsg = 10;
    attr.mq_msgsize = sizeof(AlertMessage);

    /* Remove a leftover queue from an earlier run */
    mq_unlink(SMS_ALARM_QUEUE_NAME);

    alarm_mq = mq_open(
        SMS_ALARM_QUEUE_NAME,
        O_CREAT | O_EXCL | O_RDWR,
        0600,
        &attr
    );

    if (alarm_mq == (mqd_t)-1) {
        perror("[APP] mq_open");
        goto cleanup;
    }

    /* Configure sensor driver */
    if (configure_sensor(interval_ms) != 0)
        goto cleanup;

    /* Initialize thread contexts */
    sensor_ctx.buffer = &shared_buffer;
    sensor_ctx.interval_ms = interval_ms;
    sensor_ctx.running = &running;

    processor_ctx.buffer = &shared_buffer;
    processor_ctx.alarm_queue = alarm_mq;
    processor_ctx.log_queue = &log_queue;
    processor_ctx.temp_threshold = temp_threshold;
    processor_ctx.humid_threshold = humid_threshold;

    alert_ctx.alarm_queue = alarm_mq;
    alert_ctx.log_queue = &log_queue;

    logger_ctx.queue = &log_queue;
    logger_ctx.log_path = SMS_LOG_PATH;

ret = pthread_attr_init(&worker_attr);

if (ret != 0) {
    fprintf(stderr,
            "[RT] pthread_attr_init: %s\n",
            strerror(ret));
    goto cleanup;
}

worker_attr_ready = 1;

ret = pthread_attr_setstacksize(
    &worker_attr,
    SMS_THREAD_STACK_SIZE
);

if (ret != 0) {
    fprintf(stderr,
            "[RT] pthread_attr_setstacksize: %s\n",
            strerror(ret));
    goto cleanup;
}

printf("[RT] Worker thread stack size: %u KB\n",
       (unsigned int)(SMS_THREAD_STACK_SIZE / 1024U));

   /*
 * Attempt to lock memory before starting worker threads.
 */
    memory_locked = configure_memory_lock(); 
/* Start Logger Thread */
    ret = pthread_create(
        &logger_tid, &worker_attr,
        logger_thread_func, &logger_ctx
    );

    if (ret != 0) {
        fprintf(stderr, "[APP] Logger: %s\n", strerror(ret));
        goto cleanup;
    }

    logger_started = 1;

    /* Start Alert Thread */
    ret = pthread_create(
        &alert_tid, &worker_attr,
        alert_thread_func, &alert_ctx
    );

    if (ret != 0) {
        fprintf(stderr, "[APP] Alert: %s\n", strerror(ret));
        goto shutdown;
    }

    alert_started = 1;

    /* Start Processor Thread */
    ret = pthread_create(
        &processor_tid, &worker_attr,
        processor_thread_func, &processor_ctx
    );

    if (ret != 0) {
        fprintf(stderr, "[APP] Processor: %s\n", strerror(ret));
        goto shutdown;
    }

    processor_started = 1;

    /* Start Sensor Thread */
    ret = pthread_create(
        &sensor_tid, &worker_attr,
        sensor_thread_func, &sensor_ctx
    );

    if (ret != 0) {
        fprintf(stderr, "[APP] Sensor: %s\n", strerror(ret));
        goto shutdown;
    }

    sensor_started = 1;

    if (configure_realtime(sensor_tid, rt_mode_fifo) != 0) {
    fprintf(stderr,
            "[RT] WARNING: Scheduling mode verification failed\n");
}

    printf("[APP] All four threads started\n");

    /* Run until timeout or SIGINT/SIGTERM */
    double start_time = monotonic_seconds();

    while (!stop_requested) {
        double elapsed = monotonic_seconds() - start_time;

        if (duration != 0 && elapsed >= (double)duration)
            break;

        struct timespec pause_time = {
            .tv_sec = 0,
            .tv_nsec = 100000000L
        };

        nanosleep(&pause_time, NULL);
    }

    exit_status = EXIT_SUCCESS;

shutdown:
    printf("\n[APP] Shutting down...\n");

    /* Step 1: stop Sensor Thread */
    atomic_store(&running, 0);

    if (sensor_started) {
    pthread_join(sensor_tid, NULL);

    if (export_jitter_csv(&sensor_ctx) != 0) {
        fprintf(stderr,
                "[RT] Failed to export jitter CSV\n");
    }
}

    /* Step 2: finish processing buffered samples */
    if (buffer_ready)
        shared_buffer_close(&shared_buffer);

    if (processor_started)
        pthread_join(processor_tid, NULL);

    /* Step 3: finish pending alerts */
    if (alert_started) {
        AlertMessage stop_message = {
            .timestamp_ms = UINT64_MAX
        };

        if (mq_send(
                alarm_mq,
                (const char *)&stop_message,
                sizeof(stop_message),
                0
            ) == -1) {
            perror("[APP] mq_send shutdown");
            exit_status = EXIT_FAILURE;
        }

        pthread_join(alert_tid, NULL);
    }

    /* Step 4: flush remaining log messages */
    if (log_ready)
        log_queue_close(&log_queue);

    if (logger_started)
        pthread_join(logger_tid, NULL);

    /* Read statistics after threads have stopped */
    if (buffer_ready)
        shared_buffer_get_stats(
            &shared_buffer, &buffer_stats);

    printf("\n=== SMS Final Statistics ===\n");

    printf("Samples read      : %llu\n",
           (unsigned long long)sensor_ctx.samples_read);

    printf("Read errors       : %llu\n",
           (unsigned long long)sensor_ctx.read_errors);

    printf("Dropped samples   : %llu\n",
           (unsigned long long)sensor_ctx.dropped_samples);

    printf("Samples produced  : %llu\n",
           (unsigned long long)buffer_stats.total_produced);

    printf("Samples consumed  : %llu\n",
           (unsigned long long)buffer_stats.total_consumed);

    printf("Buffer overflows  : %llu\n",
           (unsigned long long)buffer_stats.overflow_count);

    printf("Samples processed : %llu\n",
           (unsigned long long)processor_ctx.samples_processed);

    printf("Alerts generated  : %llu\n",
           (unsigned long long)processor_ctx.alerts_generated);

    printf("Alerts received   : %llu\n",
           (unsigned long long)alert_ctx.alerts_received);

    printf("TEMP_HIGH         : %llu\n",
           (unsigned long long)alert_ctx.temp_high_count);

    printf("HUMID_HIGH        : %llu\n",
           (unsigned long long)alert_ctx.humid_high_count);

    printf("TEMP_SPIKE        : %llu\n",
           (unsigned long long)alert_ctx.temp_spike_count);

    printf("Log messages      : %llu\n",
           (unsigned long long)logger_ctx.messages_written);

    printf("Log rotations     : %llu\n",
           (unsigned long long)logger_ctx.log_rotations);

    printf("Log write errors  : %llu\n",
           (unsigned long long)logger_ctx.write_errors);

    printf("Log queue dropped : %llu\n",
           (unsigned long long)log_queue.dropped_messages);

cleanup:
    if (alarm_mq != (mqd_t)-1) {
        mq_close(alarm_mq);
        mq_unlink(SMS_ALARM_QUEUE_NAME);
    }

    if (log_ready)
        log_queue_destroy(&log_queue);

    if (buffer_ready)
        shared_buffer_destroy(&shared_buffer);

    /* Release worker thread attributes */
    if (worker_attr_ready) {
        ret = pthread_attr_destroy(&worker_attr);

        if (ret != 0) {
            fprintf(stderr,
                    "[RT] pthread_attr_destroy: %s\n",
                    strerror(ret));
        }
    }

    /* Unlock memory */
    if (memory_locked) {
        if (munlockall() != 0) {
            perror("[RT] munlockall");
        }
    }

    printf("[APP] Shutdown complete\n");

    return exit_status;
}
