/**
 * Copyright 2023 University of Applied Sciences Western Switzerland / Fribourg
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Project: HEIA-FR / HES-SO MSE - MA-CSEL1 Laboratory
 *
 * Abstract: System programming -  file system
 *
 * Purpose: NanoPi status led control system
 *
 * Author:  Luca Srdjenovic & Louka Yerly
 * Date:    28.04.2023
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

//-----------------------------------------------------------------------------
struct ctrl {
    int fd;                         // fd of the /value
    struct epoll_event event;       // epoll event
    void (*process)(struct ctrl*);  // pointer to the function
};

// GPIO CONFIG
//-----------------------------------------------------------------------------
#define GPIO_EXPORT "/sys/class/gpio/export"
#define GPIO_UNEXPORT "/sys/class/gpio/unexport"
#define GPIO_GPIO "/sys/class/gpio/gpio"

static inline const char* gpio_name(const char* nr, const char* attr)
{
    static char buf[100];
    memset(buf, 0, sizeof(buf));
    strncpy(buf, GPIO_GPIO, sizeof(buf) - 1);
    strncat(buf, nr, sizeof(buf) - strlen(buf) - 1);
    strncat(buf, attr, sizeof(buf) - strlen(buf) - 1);
    buf[sizeof(buf) - 1] = 0;

    return buf;
}

static void cfg_gpio_export(const char* nr)
{
    int fd = -1;

    // unexport pin out of sysfs (reinitialization)
    if (access(gpio_name(nr, ""), F_OK) != -1) {
        fd = open(GPIO_UNEXPORT, O_WRONLY);
        write(fd, nr, strlen(nr));
        close(fd);
    }

    // export pin to sysfs
    fd = open(GPIO_EXPORT, O_WRONLY);
    if (fd == -1) {
        char msg[100] = "";
        snprintf(msg, sizeof(msg) - 1, "ERROR : can't export gpio -> %s", nr);
        perror(msg);
        exit(EXIT_FAILURE);
    }
    write(fd, nr, strlen(nr));
    close(fd);
}

static int cfg_gpio_in(const char* nr, const char* edge)
{
    cfg_gpio_export(nr);

    // config pin
    int fd = open(gpio_name(nr, "/direction"), O_WRONLY);
    write(fd, "in", sizeof("in"));
    close(fd);

    fd = open(gpio_name(nr, "/edge"), O_WRONLY);
    write(fd, edge, strlen(edge));
    close(fd);

    fd = open(gpio_name(nr, "/value"), O_RDWR);

    // ack pending event
    char dummy[10];
    pread(fd, dummy, sizeof(dummy), 0);

    return fd;
}

static int cfg_gpio_out(const char* nr, bool val)
{
    cfg_gpio_export(nr);

    // config pin
    int fd = open(gpio_name(nr, "/direction"), O_WRONLY);
    write(fd, "out", sizeof("out"));
    close(fd);

    int fdo = open(gpio_name(nr, "/value"), O_RDWR);
    pwrite(fdo, val ? "1" : "0", 1, 0);

    return fdo;
}

// LED
//-----------------------------------------------------------------------------
#define LED_GPIO_NR "10"

static void set_led(int led_fd, bool mode)
{
    pwrite(led_fd, mode ? "1" : "0", 2, 0);
}

static void toggle_led(int led_fd)
{
    char buf[100] = {0};
    pread(led_fd, buf, sizeof(buf), 0);
    set_led(led_fd, !(buf[0] == '1'));
}

// TIMER
//-----------------------------------------------------------------------------
enum timer_type {
    TIMER_LED_BLINK                 = 0,
    TIMER_PERIOD_LED_BUTTON_PRESSED = 1,
};

struct timer_ctrl {
    struct ctrl ctrl;
    int64_t default_period_ms;
    int64_t actual_period_ms;
    const enum timer_type type;
    void* arg;
};

static void timer_led_slowdown();
static void timer_led_speedup();

static void timer_process(struct ctrl* ctrl)
{
    struct timer_ctrl* timer = (struct timer_ctrl*)ctrl;
    uint64_t temp;
    int ret = read(timer->ctrl.fd, &temp, sizeof(temp));
    if (ret == -1 && errno != 0) {
        perror("Error in timer_process");
    }

    bool speed_up = false;
    if (timer->type == TIMER_PERIOD_LED_BUTTON_PRESSED) {
        speed_up = (bool)timer->arg;
    }

    switch (timer->type) {
        case TIMER_LED_BLINK:
            // double casting cause pointer is 64 bits and int is 32 bits
            toggle_led((int)(int64_t)timer->arg);
            break;
        case TIMER_PERIOD_LED_BUTTON_PRESSED:
            if (speed_up) {
                timer_led_speedup();
            } else {
                timer_led_slowdown();
            }
            break;
        default:
            break;
    }
}
// multiple of 2 to divide and mulitply and always have the same value
#define TIMER_LED_BLINK_DEFAULT_PERIOD 2048
#define TIMER_PERIOD_LED_BUTTON_PRESSED_DEFAULT_PERIOD 1000
static struct timer_ctrl timers[] = {
    [0] =
        {
            .ctrl =
                {
                    .fd = -1,
                    .event =
                        {
                            .events   = EPOLLIN,
                            .data.ptr = &timers[0].ctrl,
                        },
                    .process = timer_process,
                },
            .actual_period_ms  = 0,
            .default_period_ms = TIMER_LED_BLINK_DEFAULT_PERIOD,
            .type              = TIMER_LED_BLINK,
            .arg               = NULL,
        },
    [1] = {
        .ctrl =
            {
                .fd = -1,
                .event =
                    {
                        .events   = EPOLLIN,
                        .data.ptr = &timers[1].ctrl,
                    },
                .process = timer_process,
            },
        .actual_period_ms  = 0,
        .default_period_ms = TIMER_PERIOD_LED_BUTTON_PRESSED_DEFAULT_PERIOD,
        .type              = TIMER_PERIOD_LED_BUTTON_PRESSED,
        .arg               = (void*)false,
    }};

static void timer_set_period(enum timer_type timer_index, int64_t period_ms)
{
    timers[timer_index].actual_period_ms = period_ms;

    struct itimerspec timer_spec = {
        .it_interval =
            {
                .tv_sec  = period_ms / 1000,
                .tv_nsec = (period_ms % 1000) * 1000000,
            },
        .it_value =
            {
                .tv_sec  = period_ms / 1000,
                .tv_nsec = (period_ms % 1000) * 1000000,
            },
    };

    int ret = timerfd_settime(timers[timer_index].ctrl.fd, 0, &timer_spec, 0);
    if (ret < 0) {
        perror("Cannot set timer period");
    }

    if (timer_index == TIMER_LED_BLINK)
        printf("LED period %li ms\n", period_ms);
}

static void timers_setup(int efd, int64_t* periods_ms, void** args)
{
    for (unsigned int i = 0; i < ARRAY_SIZE(timers); i++) {
        timers[i].arg = args[i];

        timers[i].ctrl.fd = timerfd_create(CLOCK_MONOTONIC, 0);
        if (timers[i].ctrl.fd == -1) {
            perror("timerfd == -1 --> error");
            exit(EXIT_FAILURE);
        }

        timers[i].default_period_ms = periods_ms[i];
        timer_set_period(i, periods_ms[i]);

        epoll_ctl(efd, EPOLL_CTL_ADD, timers[i].ctrl.fd, &timers[i].ctrl.event);
    }
}

static void timers_close(int efd)
{
    for (unsigned int i = 0; i < ARRAY_SIZE(timers); i++) {
        epoll_ctl(efd, EPOLL_CTL_DEL, timers[i].ctrl.fd, &timers[i].ctrl.event);
        close(timers[i].ctrl.fd);
    }
}

static void timer_led_speedup()
{
    int64_t period_ms = timers[TIMER_LED_BLINK].actual_period_ms / 2;
    period_ms         = period_ms > 0 ? period_ms : 1;
    if (period_ms == 1) printf("Minimal frequency\n");
    timer_set_period(TIMER_LED_BLINK, period_ms);
}
static void timer_led_slowdown()
{
    timer_set_period(TIMER_LED_BLINK,
                     timers[TIMER_LED_BLINK].actual_period_ms * 2);
}
static void timer_led_reset()
{
    timer_set_period(TIMER_LED_BLINK,
                     timers[TIMER_LED_BLINK].default_period_ms);
}

// BUTTON
//-----------------------------------------------------------------------------
enum button_type {
    BUTTON_LEFT,
    BUTTON_CENTER,
    BUTTON_RIGHT,
};

enum button_state {
    BUTTON_PRESSED  = 0,
    BUTTON_RELEASED = 1,
};

struct button_ctrl {
    struct ctrl ctrl;
    const char* gpio_nr;
    const char* edge;
    const enum button_type type;
};

void button_process(struct ctrl* ctrl)
{
    struct button_ctrl* btn = (struct button_ctrl*)ctrl;
    char buf[10];
    ssize_t n = pread(btn->ctrl.fd, buf, sizeof(buf), 0);
    if (n == -1 && errno != 0) {
        perror("Error while reading button state");
    }

    enum button_state state = buf[0] == '0' ? BUTTON_RELEASED : BUTTON_PRESSED;

    switch (btn->type) {
        case BUTTON_LEFT:
            if (state == BUTTON_PRESSED) {
                timer_led_speedup();
                timer_set_period(
                    TIMER_PERIOD_LED_BUTTON_PRESSED,
                    TIMER_PERIOD_LED_BUTTON_PRESSED_DEFAULT_PERIOD);
                timers[TIMER_PERIOD_LED_BUTTON_PRESSED].arg = (void*)true;
            } else {
                timer_set_period(TIMER_PERIOD_LED_BUTTON_PRESSED, 0);
            }
            break;
        case BUTTON_CENTER:
            timer_led_reset();
            break;
        case BUTTON_RIGHT:

            if (state == BUTTON_PRESSED) {
                timer_led_slowdown();
                timer_set_period(
                    TIMER_PERIOD_LED_BUTTON_PRESSED,
                    TIMER_PERIOD_LED_BUTTON_PRESSED_DEFAULT_PERIOD);
                timers[TIMER_PERIOD_LED_BUTTON_PRESSED].arg = (void*)false;
            } else {
                timer_set_period(TIMER_PERIOD_LED_BUTTON_PRESSED, 0);
            }
            break;
        default:
            break;
    }
}

struct button_ctrl buttons[] = {
    [0] =
        {
            .gpio_nr = "0",
            .edge    = "both",
            .type    = BUTTON_LEFT,
            .ctrl =
                {
                    .fd      = -1,
                    .event   = {.events   = EPOLLERR | EPOLLET,
                                .data.ptr = &buttons[0].ctrl},
                    .process = button_process,
                },
        },
    [1] =
        {
            .gpio_nr = "2",
            .edge    = "falling",
            .type    = BUTTON_CENTER,
            .ctrl =
                {
                    .fd      = -1,
                    .event   = {.events   = EPOLLERR | EPOLLET,
                                .data.ptr = &buttons[1].ctrl},
                    .process = button_process,
                },
        },
    [2] =
        {
            .gpio_nr = "3",
            .edge    = "both",
            .type    = BUTTON_RIGHT,
            .ctrl =
                {
                    .fd      = -1,
                    .event   = {.events   = EPOLLERR | EPOLLET,
                                .data.ptr = &buttons[2].ctrl},
                    .process = button_process,
                },
        },
};

static void buttons_setup(int efd)
{
    for (unsigned i = 0; i < ARRAY_SIZE(buttons); i++) {
        struct button_ctrl* btn = &buttons[i];

        // gpio pins configuration
        btn->ctrl.fd = cfg_gpio_in(btn->gpio_nr, btn->edge);

        // epoll configuration
        epoll_ctl(efd, EPOLL_CTL_ADD, btn->ctrl.fd, &btn->ctrl.event);
    }
}

static void buttons_close(int efd)
{
    for (unsigned i = 0; i < ARRAY_SIZE(buttons); i++) {
        struct button_ctrl* btn = &buttons[i];
        epoll_ctl(efd, EPOLL_CTL_DEL, btn->ctrl.fd, &btn->ctrl.event);
        close(btn->ctrl.fd);
    }
}

// SIGNAL
//-----------------------------------------------------------------------------
static bool signal_running = false;

static void signal_int_handler(int s)
{
    (void)s;
    signal_running = false;
}

static void signal_setup()
{
    static bool installed_ = false;
    if (!installed_) {
        signal(SIGINT, signal_int_handler);
        signal(SIGILL, signal_int_handler);
        signal(SIGABRT, signal_int_handler);
        signal(SIGFPE, signal_int_handler);
        signal(SIGSEGV, signal_int_handler);
        signal(SIGTERM, signal_int_handler);
        installed_     = true;
        signal_running = true;
    }
}
static inline bool signal_is_running() { return signal_running; }
//-----------------------------------------------------------------------------

int main(int argc, char* argv[])
{
    // catch signal
    signal_setup();

    // create event poll
    int efd = epoll_create1(0);
    if (efd == -1) {
        perror("ERROR while create epoll");
        exit(EXIT_FAILURE);
    }

    // open led
    int led_fd = cfg_gpio_out(LED_GPIO_NR, false);

    // config button
    buttons_setup(efd);

    // config timer
    int64_t period = TIMER_LED_BLINK_DEFAULT_PERIOD;
    if (argc >= 2) period = atoi(argv[1]);
    int64_t periods[] = {period, 0};
    // double casting cause pointer is 64 bits and int is 32 bits
    void* args[] = {(void*)(int64_t)led_fd, (void*)NULL};
    timers_setup(efd, periods, args);

    //  monitor events
    while (signal_is_running()) {
        struct epoll_event events[2];
        int ret = epoll_wait(efd, events, ARRAY_SIZE(events), -1);
        if (ret == -1) {
            perror("Error in epoll_wait");
        } else if (ret == 0) {
            perror("Error, nothing receive in epoll_wait");
        } else {
            for (int i = 0; i < ret; i++) {
                struct ctrl* ctrl = events[i].data.ptr;
                ctrl->process(ctrl);
            }
        }
    }

    buttons_close(efd);
    timers_close(efd);
    close(led_fd);
    close(efd);

    return EXIT_SUCCESS;
}