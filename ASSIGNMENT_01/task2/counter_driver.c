#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/atomic.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/mutex.h>
#include <linux/string.h>

#define DRIVER_NAME "counter_driver"
#define PROC_NAME   "counter_info"

static atomic_t counter = ATOMIC_INIT(0);
static atomic_t read_count = ATOMIC_INIT(0);
static atomic_t write_count = ATOMIC_INIT(0);
static DEFINE_MUTEX(counter_mutex);

/*
 * READ:
 * Return current counter followed by newline.
 * Increment counter after a successful read.
 */
static ssize_t counter_read(struct file *file,
                            char __user *buf,
                            size_t count,
                            loff_t *ppos)
{
    char kbuf[32];
    int len;
    int value;

    if (*ppos > 0)
        return 0;

    if (count == 0)
        return 0;

    mutex_lock(&counter_mutex);

    value = atomic_read(&counter);
    len = scnprintf(kbuf, sizeof(kbuf), "%d\n", value);

    if (count < len) {
        mutex_unlock(&counter_mutex);
        return -EINVAL;
    }

    if (copy_to_user(buf, kbuf, len)) {
        mutex_unlock(&counter_mutex);
        return -EFAULT;
    }

    atomic_inc(&counter);
    atomic_inc(&read_count);
    *ppos += len;

    mutex_unlock(&counter_mutex);

    return len;
}

/*
 * WRITE:
 * Parse an ASCII integer.
 * 0 resets the counter; positive values set it.
 * Negative or invalid values return -EINVAL.
 */
static ssize_t counter_write(struct file *file,
                             const char __user *buf,
                             size_t count,
                             loff_t *ppos)
{
    char kbuf[32];
    int value;
    int ret;

    if (count == 0)
        return 0;

    if (count >= sizeof(kbuf))
        return -EINVAL;

    if (copy_from_user(kbuf, buf, count))
        return -EFAULT;

    kbuf[count] = '\0';

    ret = kstrtoint(kbuf, 10, &value);
    if (ret)
        return -EINVAL;

    if (value < 0)
        return -EINVAL;

    mutex_lock(&counter_mutex);

    atomic_set(&counter, value);
    atomic_inc(&write_count);

    mutex_unlock(&counter_mutex);

    pr_info("%s: counter set to %d\n", DRIVER_NAME, value);

    return count;
}

/* procfs statistics */
static int counter_proc_show(struct seq_file *m, void *v)
{
    seq_printf(m, "Counter value : %d\n", atomic_read(&counter));
    seq_printf(m, "Total reads   : %d\n", atomic_read(&read_count));
    seq_printf(m, "Total writes  : %d\n", atomic_read(&write_count));

    return 0;
}

static int counter_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, counter_proc_show, NULL);
}

static const struct proc_ops counter_proc_ops = {
    .proc_open    = counter_proc_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};

/* Device operations */
static const struct file_operations counter_fops = {
    .owner  = THIS_MODULE,
    .read   = counter_read,
    .write  = counter_write,
    .llseek = no_llseek,
};

static struct miscdevice counter_miscdev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = "counter",
    .fops  = &counter_fops,
    .mode  = 0666,
};

static struct proc_dir_entry *counter_proc_entry;

static int __init counter_init(void)
{
    int ret;

    ret = misc_register(&counter_miscdev);
    if (ret) {
        pr_err("%s: failed to register misc device: %d\n",
               DRIVER_NAME, ret);
        return ret;
    }

    counter_proc_entry = proc_create(PROC_NAME, 0444, NULL,
                                     &counter_proc_ops);
    if (!counter_proc_entry) {
        misc_deregister(&counter_miscdev);
        pr_err("%s: failed to create /proc/%s\n",
               DRIVER_NAME, PROC_NAME);
        return -ENOMEM;
    }

    pr_info("%s: registered /dev/counter (major=10, minor=%d)\n",
            DRIVER_NAME, counter_miscdev.minor);
    pr_info("%s: created /proc/%s\n", DRIVER_NAME, PROC_NAME);

    return 0;
}

static void __exit counter_exit(void)
{
    proc_remove(counter_proc_entry);
    misc_deregister(&counter_miscdev);

    pr_info("%s: unloaded and cleaned up\n", DRIVER_NAME);
}

module_init(counter_init);
module_exit(counter_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Embedded Linux Lab -Tram Gia Khanh -SE200963 ");
MODULE_DESCRIPTION("Misc device counter driver with atomic operations and procfs statistics");
MODULE_VERSION("1.0");
