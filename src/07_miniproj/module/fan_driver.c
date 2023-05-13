/**
 * Copyright 2023 University of Applied Sciences Western Switzerland / Fribourg
 *
 * Licensed under GPL
 *
 * Project:     HEIA-FR / CSEL1 Mini-project
 * Author:      Louka Yerly
 * Date:        10.05.2023
 */
#include <linux/delay.h>      /* needed for delay fonctions */
#include <linux/gpio.h>       /* needed for led */
#include <linux/jiffies.h>    /* needed for timer */
#include <linux/kernel.h>     /* needed for debugging */
#include <linux/kthread.h>    /* needed for kernel thread management */
#include <linux/miscdevice.h> /* needed for miscdevice */
#include <linux/module.h>     /* needed by all modules */
#include <linux/mutex.h>      /* needed for the concurent access*/
#include <linux/slab.h>       /* needed for dynamic allocation */
#include <linux/thermal.h>    /* needed for temperature */
#include <linux/timer.h>      /* needed for fan speed */
#include <linux/wait.h>       /* needed for waitqueues handling */

#define PRINT_ON_ERROR(status, fmt, ...)          \
    do {                                          \
        if (status < 0) pr_err(fmt, __VA_ARGS__); \
    } while (0)

#define MIN(a, b) (a < b ? a : b)

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

#define ATTRIBUTE_FREQ freq
#define ATTRIBUTE_MODE mode

#define THERMAL_NAME "cpu-thermal"

#define THREAD_RUN_FLAG 0
#define THREAD_SLEEP_SEC 10

#define LED_STATUS_NR 10
#define LED_STATUS_LABEL "LED STATUS"

typedef enum {
    FAN_MODE_MANUAL,
    FAN_MODE_AUTO,
    FAN_MODE_ERROR,
} fan_driver_mode_t;

static const char* const fan_driver_mode_str[] = {[FAN_MODE_MANUAL] = "manual",
                                                  [FAN_MODE_AUTO]   = "auto",
                                                  [FAN_MODE_ERROR]  = "error"};

typedef struct {
    struct device* dev;

    atomic_t frequency;
    char mode_str[50];
    struct mutex mode_mutex;

    unsigned gpio_nr;
    bool led_state;

    struct thermal_zone_device* thermal_zone;

    struct task_struct* fan_thread;
    atomic_t running_flag;
    wait_queue_head_t queue;

    struct timer_list fan_timer;
} fan_driver_data_t;


/// @brief set the frequency of the the timer
static void fan_timer_frequency_set(fan_driver_data_t* fan_data, int frequency)
{
    // re-arm the timer with the new value
    int period = 1 * HZ / (frequency * 2);  // multipy freq by two to have half
                                            // ON and half OFF during one period
    mod_timer(&fan_data->fan_timer, jiffies + period);

    // notify to poll the frequency change
    if(frequency != atomic_read(&fan_data->frequency)) {
        sysfs_notify(&fan_data->dev->kobj, NULL, TOSTRING(ATTRIBUTE_FREQ));
    }
    
    // change the actual value
    atomic_set(&fan_data->frequency, frequency);
}

static void fan_timer_callback(struct timer_list* list)
{
    fan_driver_data_t* fan_data =
        (fan_driver_data_t*)container_of(list, fan_driver_data_t, fan_timer);
    // re-arm the timer
    fan_timer_frequency_set(fan_data, atomic_read(&fan_data->frequency));

    // toggle the led
    fan_data->led_state = !fan_data->led_state;
    gpio_set_value(fan_data->gpio_nr, fan_data->led_state ? 1 : 0);
}

/// @brief get the mode from a buffer
/// @warning do not use with fan_data buffer (not tread-safe)
///          use \fn fan_driver_mode_str_get() instead
static fan_driver_mode_t fan_mode_from_str(const char* mode_str)
{
    int i = 0;

    // search the corresponding mode
    for (i = 0; i < ARRAY_SIZE(fan_driver_mode_str); i++) {
        if (strncmp(mode_str,
                    fan_driver_mode_str[i],
                    MIN(strlen(mode_str), strlen(fan_driver_mode_str[i]))) ==
            0) {
            return i != FAN_MODE_ERROR ? i : FAN_MODE_ERROR;
        }
    }
    return FAN_MODE_ERROR;
}

/// @brief get the mode inside the fan_data
static fan_driver_mode_t fan_driver_mode_str_get(fan_driver_data_t* fan_data)
{
    fan_driver_mode_t mode = FAN_MODE_ERROR;
    mutex_lock(&fan_data->mode_mutex);
    {
        mode = fan_mode_from_str(fan_data->mode_str);
    }
    mutex_unlock(&fan_data->mode_mutex);
    return mode;
}

/// @brief set the mode inside the fan_data
static void fan_driver_mode_str_set(fan_driver_data_t* fan_data,
                                    fan_driver_mode_t mode)
{
    mutex_lock(&fan_data->mode_mutex);
    {
        memset(fan_data->mode_str, 0, sizeof(fan_data->mode_str));
        strncpy(
            fan_data->mode_str,
            fan_driver_mode_str[mode],
            MIN(sizeof(fan_data->mode_str), strlen(fan_driver_mode_str[mode])));
    }
    mutex_unlock(&fan_data->mode_mutex);
}

/// @brief change to mode_new and apply the configuration
static void fan_driver_mode_change(fan_driver_data_t* fan_data,
                                   fan_driver_mode_t mode_new)
{
    // set the mode inside the fan_data
    fan_driver_mode_str_set(fan_data, mode_new);

    // change the driver mode
    if (mode_new == FAN_MODE_AUTO) {
        // activate the thread (timer will be set by thread)
        atomic_set(&fan_data->running_flag, THREAD_RUN_FLAG);
        wake_up_interruptible(&fan_data->queue);

    } else if (mode_new == FAN_MODE_MANUAL) {
        // stop the thread
        atomic_set(&fan_data->running_flag, !THREAD_RUN_FLAG);
        // apply the confiuguration to the timer
        fan_timer_frequency_set(fan_data, atomic_read(&fan_data->frequency));

    } else {
        pr_err("Invalid mode for fan_driver (%i)", mode_new);
    }

    sysfs_notify(&fan_data->dev->kobj, NULL, TOSTRING(ATTRIBUTE_MODE));
}

ssize_t sysfs_show(struct device* dev, struct device_attribute* attr, char* buf)
{
    ssize_t ret                 = -1;
    int frequency               = 0;
    fan_driver_data_t* fan_data = (fan_driver_data_t*)dev->driver_data;

    if (strncmp(TOSTRING(ATTRIBUTE_FREQ),
                attr->attr.name,
                sizeof(TOSTRING(ATTRIBUTE_FREQ))) == 0) {
        frequency = atomic_read(&fan_data->frequency);
        sprintf(buf, "%u\n", frequency);
        ret = strlen(buf);

    } else if (strncmp(TOSTRING(ATTRIBUTE_MODE),
                       attr->attr.name,
                       sizeof(TOSTRING(ATTRIBUTE_MODE))) == 0) {
        sprintf(buf, "%s\n", fan_data->mode_str);
        ret = strlen(buf);
    }

    return ret;
}
ssize_t sysfs_store(struct device* dev,
                    struct device_attribute* attr,
                    const char* buf,
                    size_t count)
{
    ssize_t ret                 = -1;
    int frequency               = 0;
    fan_driver_data_t* fan_data = (fan_driver_data_t*)dev->driver_data;
    fan_driver_mode_t mode      = FAN_MODE_ERROR;

    if (strncmp(TOSTRING(ATTRIBUTE_FREQ),
                attr->attr.name,
                MIN(sizeof(TOSTRING(ATTRIBUTE_FREQ)),
                    strlen(attr->attr.name))) == 0) {
        mode = fan_driver_mode_str_get(fan_data);

        if (mode == FAN_MODE_MANUAL) {
            frequency = (int)simple_strtol(buf, 0, 10);
            fan_timer_frequency_set(fan_data, frequency);
            pr_info("Changed frequency to: %i Hz\n", frequency);
            ret = count;

        } else {
            pr_err(
                "Cannot set frequency while fan_driver is not in \"%s\" mode\n",
                fan_driver_mode_str[FAN_MODE_MANUAL]);
            ret = -1;
        }

    } else if (strncmp(TOSTRING(ATTRIBUTE_MODE),
                       attr->attr.name,
                       MIN(sizeof(TOSTRING(ATTRIBUTE_MODE)),
                           strlen(attr->attr.name))) == 0) {
        mode = fan_mode_from_str(buf);
        if (mode != FAN_MODE_ERROR) {
            fan_driver_mode_change(fan_data, mode);
            pr_info("Changed to mode: %s\n", fan_driver_mode_str[mode]);
            ret = count;

        } else {
            pr_err("Invalid mode for fan_driver (%s)\n", buf);
            ret = -1;
        }
    } else {
        pr_err("No corresponding attribute (%s)\n", attr->attr.name);
    }
    return ret;
}
DEVICE_ATTR(freq, 0664, sysfs_show, sysfs_store);
DEVICE_ATTR(mode, 0664, sysfs_show, sysfs_store);

static struct miscdevice misc_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = "fan_driver",
    .mode  = 0,
};

static uint32_t cpu_temperature_to_fan_frequency(int temperature)
{
    if (temperature < 35) return 2;
    if (temperature < 40) return 5;
    if (temperature < 45) return 10;
    return 20;
}

static int fan_thread(void* data)
{
    fan_driver_data_t* fan_data = (fan_driver_data_t*)data;
    int status                  = -1;
    int temperature             = 0;
    uint32_t frequency          = 0;

    pr_info("fan_thread is now active...\n");

    while (!kthread_should_stop()) {
        status = wait_event_interruptible(
            fan_data->queue,
            (atomic_read(&fan_data->running_flag) == THREAD_RUN_FLAG) ||
                kthread_should_stop());
        PRINT_ON_ERROR(
            status, "Error while waiting to start thread (%i)", status);

        // read temperature
        status = thermal_zone_get_temp(fan_data->thermal_zone, &temperature);
        PRINT_ON_ERROR(status, "Error while reading temperature (%i)", status);

        if (status >= 0) {
            // round the temperature
            temperature = (temperature + 500) / 1000;

            // set the timer frequency in relation with the temperature
            frequency = cpu_temperature_to_fan_frequency(temperature);
            if (frequency != atomic_read(&fan_data->frequency)) {
                fan_timer_frequency_set(fan_data, frequency);
            }
        }

        // sleep a little bit
        status = wait_event_interruptible_timeout(
            fan_data->queue, kthread_should_stop(), THREAD_SLEEP_SEC * HZ);
        PRINT_ON_ERROR(status, "Error while sleeping (%i)", status);
    }
    pr_info("fan_thread exit...\n");
    return 0;
}

static int __init fan_driver_init(void)
{
    int status                  = 0;
    fan_driver_data_t* fan_data = NULL;

    // -----------------------------------------------------
    // MISC DEVICE
    // -----------------------------------------------------
    if (status == 0) status = misc_register(&misc_device);
    if (status == 0)
        status = device_create_file(misc_device.this_device,
                                    &CONCATENATE(dev_attr_, ATTRIBUTE_FREQ));
    if (status == 0)
        status = device_create_file(misc_device.this_device,
                                    &CONCATENATE(dev_attr_, ATTRIBUTE_MODE));

    // allocate private data
    fan_data = kmalloc(sizeof(*fan_data), GFP_KERNEL);
    BUG_ON(!fan_data);
    
    fan_data->dev = misc_device.this_device;
    
    memset(fan_data->mode_str, 0, sizeof(fan_data->mode_str));
    strncpy(fan_data->mode_str,
            fan_driver_mode_str[FAN_MODE_AUTO],
            MIN(sizeof(fan_data->mode_str),
                strlen(fan_driver_mode_str[FAN_MODE_AUTO])));

    mutex_init(&fan_data->mode_mutex);

    misc_device.this_device->driver_data = fan_data;

    // -----------------------------------------------------
    // TEMPERATURE
    // -----------------------------------------------------
    fan_data->thermal_zone = thermal_zone_get_zone_by_name(THERMAL_NAME);

    // -----------------------------------------------------
    // GPIO
    // -----------------------------------------------------
    fan_data->gpio_nr   = LED_STATUS_NR;
    fan_data->led_state = 0;
    if (status == 0) status = gpio_request(fan_data->gpio_nr, LED_STATUS_LABEL);
    if (status == 0)
        status = gpio_direction_output(fan_data->gpio_nr, fan_data->led_state);

    // -----------------------------------------------------
    // TIMER
    // -----------------------------------------------------
    timer_setup(&fan_data->fan_timer, fan_timer_callback, 0);

    // -----------------------------------------------------
    // THREAD
    // -----------------------------------------------------
    atomic_set(&fan_data->running_flag, THREAD_RUN_FLAG);
    init_waitqueue_head(&fan_data->queue);
    fan_data->fan_thread =
        kthread_run(fan_thread, (void*)fan_data, "fan_driver_thread");

    pr_info("Finish initializing driver...\n");

    return status;
}

static void __exit fan_driver_exit(void)
{
    fan_driver_data_t* fan_data =
        (fan_driver_data_t*)misc_device.this_device->driver_data;

    if (fan_data->fan_thread != NULL) kthread_stop(fan_data->fan_thread);

    del_timer(&fan_data->fan_timer);

    if (fan_data->gpio_nr >= 0) gpio_set_value(fan_data->gpio_nr, 0);
    if (fan_data->gpio_nr >= 0) gpio_free(fan_data->gpio_nr);

    kfree(fan_data);
    fan_data = NULL;

    misc_deregister(&misc_device);

    pr_info("Finish remove driver...\n");
}

module_init(fan_driver_init);
module_exit(fan_driver_exit);

MODULE_AUTHOR("Louka Yerly <louka.yerly@gmail.com>");
MODULE_DESCRIPTION("Fan driver to manage the fan speed");
MODULE_LICENSE("GPL");