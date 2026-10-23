#ifndef SMS_COMMON_H
#define SMS_COMMON_H

#include <stdint.h>

#define SMS_BUFFER_SIZE 64

#define SMS_DEFAULT_INTERVAL_MS 500
#define SMS_DEFAULT_TEMP_THRESHOLD 40.0f
#define SMS_DEFAULT_HUMID_THRESHOLD 80.0f
#define SMS_DEFAULT_DURATION 60

#define SMS_DEVICE_PATH "/dev/sms_sensor"
#define SMS_LOG_PATH "/tmp/sms.log"

typedef struct {
    uint64_t timestamp_ms;
    float temperature;
    float humidity;
} SensorData;

typedef enum {
    ALERT_TEMP_HIGH,
    ALERT_HUMID_HIGH,
    ALERT_TEMP_SPIKE
} AlertType;

typedef struct {
    uint64_t timestamp_ms;
    AlertType type;
    float value;
    float threshold;
} AlertMessage;

#endif
