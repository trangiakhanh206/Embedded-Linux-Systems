#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/random.h>
#include <linux/ktime.h>
#include <linux/math64.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/ioctl.h>

#define DEVICE_NAME "sms_sensor"
#define PROC_NAME   "sms_stats"

#define SMS_IOCTL_MAGIC 'S'
#define SMS_SET_INTERVAL _IOW(SMS_IOCTL_MAGIC, 1, unsigned int)

static DEFINE_MUTEX(sensor_lock);

static unsigned long read_count;
static int last_temp_x10;
static int last_humid_x10;

static unsigned int read_interval_ms = 100;
static u64 last_read_ms;
static u64 start_time_ms;

static struct proc_dir_entry *proc_entry;

/* Generate simulated sensor data */
static int sensor_read(struct file *file,
                       char __user *buf,
                       size_t count,
                       loff_t *ppos)
{
    char data[128];
    int len;
    int temp;
    int humid;
    u64 now;
    unsigned int interval;

    if (*ppos > 0)
        return 0;

    mutex_lock(&sensor_lock);

    now = div_u64(ktime_get_boottime_ns(), 1000000);
    interval = read_interval_ms;

    if (last_read_ms && now - last_read_ms < interval) {
        mutex_unlock(&sensor_lock);
        return -EAGAIN;
    }

    temp = 150 + (get_random_u32() % 301);
    humid = 300 + (get_random_u32() % 601);

    last_temp_x10 = temp;
    last_humid_x10 = humid;

    read_count++;
    last_read_ms = now;

    len = scnprintf(data, sizeof(data),
                    "%llu,%d.%d,%d.%d\n",
                    (unsigned long long)now,
                    temp / 10, temp % 10,
                    humid / 10, humid % 10);

    mutex_unlock(&sensor_lock);

    return simple_read_from_buffer(buf, count, ppos, data, len);
}

/* Configure minimum sensor read interval */
static long sensor_ioctl(struct file *file,
                         unsigned int cmd,
                         unsigned long arg)
{
    unsigned int interval;

    if (cmd != SMS_SET_INTERVAL)
        return -ENOTTY;

    if (copy_from_user(&interval,
                       (unsigned int __user *)arg,
                       sizeof(interval)))
        return -EFAULT;

    if (interval < 10 || interval > 60000)
        return -EINVAL;

    mutex_lock(&sensor_lock);
    read_interval_ms = interval;
    mutex_unlock(&sensor_lock);

    return 0;
}

/* Display statistics through procfs */
static int stats_show(struct seq_file *m, void *v)
{
    u64 uptime;

    mutex_lock(&sensor_lock);

    uptime = div_u64(
    div_u64(ktime_get_boottime_ns(), 1000000)
    - start_time_ms,
    1000
);

    seq_printf(m,
        "SMS Sensor Driver Statistics\n"
        "============================\n"
        "Read count       : %lu\n"
        "Last temperature : %d.%d C\n"
        "Last humidity    : %d.%d %%\n"
        "Driver uptime    : %llu seconds\n",
        read_count,
        last_temp_x10 / 10,
        last_temp_x10 % 10,
        last_humid_x10 / 10,
        last_humid_x10 % 10,
        (unsigned long long)uptime);

    mutex_unlock(&sensor_lock);

    return 0;
}

static int stats_open(struct inode *inode, struct file *file)
{
    return single_open(file, stats_show, NULL);
}

static const struct proc_ops stats_ops = {
    .proc_open    = stats_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};

static const struct file_operations sensor_fops = {
    .owner          = THIS_MODULE,
    .read           = sensor_read,
    .unlocked_ioctl = sensor_ioctl,
    .llseek         = no_llseek,
};

static struct miscdevice sensor_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = DEVICE_NAME,
    .fops  = &sensor_fops,
    .mode  = 0666,
};

static int __init sensor_init(void)
{
    int ret;

    start_time_ms = div_u64(ktime_get_boottime_ns(), 1000000);

    ret = misc_register(&sensor_device);

    if (ret) {
        pr_err("SMS: Failed to register sensor device\n");
        return ret;
    }

    proc_entry = proc_create(PROC_NAME, 0444,
                             NULL, &stats_ops);

    if (!proc_entry) {
        misc_deregister(&sensor_device);
        return -ENOMEM;
    }

    pr_info("SMS: Sensor driver loaded successfully\n");

    return 0;
}

static void __exit sensor_exit(void)
{
    proc_remove(proc_entry);
    misc_deregister(&sensor_device);

    pr_info("SMS: Sensor driver unloaded\n");
}

module_init(sensor_init);
module_exit(sensor_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Embedded Linux Assignment 02");
MODULE_DESCRIPTION("SMS Virtual Temperature and Humidity Sensor");
MODULE_VERSION("1.0");
