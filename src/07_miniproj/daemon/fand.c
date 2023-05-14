
#define _XOPEN_SOURCE 600
#define _DEFAULT_SOURCE
#define _GNU_SOURCE  // necessary for sigabbrev_np()

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <mqueue.h>
#include <pwd.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>

#include "../common.h"
#include "oled.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

#define MIN(x, y) (x < y ? x : y)

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

// TODO: enlever tout les perror...

//-----------------------------------------------------------------------------
struct ctrl {
    int fd;                         // fd of the /value
    struct epoll_event event;       // epoll event
    void (*process)(struct ctrl*);  // pointer to the function
};
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// TEMPERATURE
//----------------------------------------------------------------------------
#define TEMPERATURE_PATH "/sys/class/thermal"

typedef enum {
    TEMPERATURE_CPU,

} temperature_t;

struct temperature_ctrl {
    struct ctrl ctrl;
    char* thermal_zone;
    temperature_t temperature;
};

static void temperature_process(struct ctrl* ctrl)
{
    struct temperature_ctrl* temp = (struct temperature_ctrl*)ctrl;

    char buf[100] = {0};
    ssize_t n     = pread(temp->ctrl.fd, buf, sizeof(buf) - 1, 0);
    if (n == -1 && errno != 0) {
        syslog(LOG_ERR,
               "Error while reading the thermal_zone \"%s\"",
               temp->thermal_zone);
        return;
    }

    int temperature = atoi(buf);
    memset(buf, 0, sizeof(buf));
    sprintf(buf, "%.2f", temperature / 1000.f);
    oled_set_temperature(buf);
}

static struct temperature_ctrl temperatures[] = {
    [TEMPERATURE_CPU] = {
        .ctrl =
            {
                .fd      = -1,
                .event   = {.events   = EPOLLIN,
                            .data.ptr = &temperatures[TEMPERATURE_CPU].ctrl},
                .process = temperature_process,
            },
        .thermal_zone = "thermal_zone0",
        .temperature  = TEMPERATURE_CPU,

    }};

static void temperatures_open(int efd)
{
    for (unsigned int i = 0; i < ARRAY_SIZE(temperatures); i++) {
        struct temperature_ctrl* temp = &temperatures[i];

        // open fd
        char buf[100] = {0};
        snprintf(buf,
                 sizeof(buf) - 1,
                 "%s/%s/temp",
                 TEMPERATURE_PATH,
                 temp->thermal_zone);
        temp->ctrl.fd = open(buf, O_RDONLY);
        if (temp->ctrl.fd == -1) {
            char msg[100] = {0};
            snprintf(msg,
                     sizeof(msg) - 1,
                     "ERROR: cannot open %s",
                     temp->thermal_zone);
            syslog(LOG_ERR, msg);
            exit(EXIT_FAILURE);
        }

        // epoll configuration
        epoll_ctl(efd, EPOLL_CTL_ADD, temp->ctrl.fd, &temp->ctrl.event);
    }
}

static void temperatures_close()
{
    for (unsigned int i = 0; i < ARRAY_SIZE(temperatures); i++) {
        struct temperature_ctrl* temp = &temperatures[i];

        close(temp->ctrl.fd);
    }
}

//-----------------------------------------------------------------------------
// FAN DRIVER
//-----------------------------------------------------------------------------
#define FAN_DRIVER_FREQ "/sys/class/misc/fan_driver/freq"
#define FAN_DRIVER_MODE "/sys/class/misc/fan_driver/mode"

typedef enum {
    FAN_DRIVER_ATTRIBUTE_FREQ,
    FAN_DRIVER_ATTRIBUTE_MODE,
} fan_driver_attribute_t;

typedef enum {
    FAN_DRIVER_MODE_AUTO,
    FAN_DRIVER_MODE_MANUAL,
} fan_driver_mode_t;

struct fan_driver_ctrl {
    struct ctrl ctrl;
    char* attribute_path;
    fan_driver_attribute_t attribute;
};

static const char* fan_driver_mode[] = {
    [FAN_DRIVER_MODE_AUTO]   = "auto",
    [FAN_DRIVER_MODE_MANUAL] = "manual",
};

static void fan_driver_process(struct ctrl* ctrl)
{
    struct fan_driver_ctrl* fan_drv = (struct fan_driver_ctrl*)ctrl;

    char buf[100] = {0};
    ssize_t n     = pread(fan_drv->ctrl.fd, buf, sizeof(buf) - 1, 0);
    if (n == -1 && errno != 0) {
        syslog(LOG_ERR, "Error while reading the fan_driver state");
        return;
    }

    if (fan_drv->attribute == FAN_DRIVER_ATTRIBUTE_FREQ) {
        syslog(LOG_INFO, "freq: %s", buf);
        oled_set_freq(buf);

    } else if (fan_drv->attribute == FAN_DRIVER_ATTRIBUTE_MODE) {
        syslog(LOG_INFO, "Mode: %s", buf);
        oled_set_mode(buf);

    } else {
        syslog(LOG_ERR,
               "Error: fan_driver attribute not supported (%i)",
               fan_drv->attribute);
    }
}

static struct fan_driver_ctrl fan_driver_attributes[] = {
    [FAN_DRIVER_ATTRIBUTE_FREQ] =
        {
            .ctrl =
                {
                    .fd = -1,
                    .event =
                        {.events = EPOLLERR | EPOLLET | EPOLLPRI,
                         .data.ptr =
                             &fan_driver_attributes[FAN_DRIVER_ATTRIBUTE_FREQ]
                                  .ctrl},
                    .process = fan_driver_process,
                },
            .attribute_path = FAN_DRIVER_FREQ,
            .attribute      = FAN_DRIVER_ATTRIBUTE_FREQ,
        },

    [FAN_DRIVER_ATTRIBUTE_MODE] =
        {
            .ctrl =
                {
                    .fd = -1,
                    .event =
                        {.events = EPOLLPRI,
                         .data.ptr =
                             &fan_driver_attributes[FAN_DRIVER_ATTRIBUTE_MODE]
                                  .ctrl},
                    .process = fan_driver_process,
                },
            .attribute_path = FAN_DRIVER_MODE,
            .attribute      = FAN_DRIVER_ATTRIBUTE_MODE,
        },
};

static void fan_driver_set_mode(fan_driver_mode_t mode)
{
    pwrite(fan_driver_attributes[FAN_DRIVER_ATTRIBUTE_MODE].ctrl.fd,
           fan_driver_mode[mode],
           strlen(fan_driver_mode[mode]),
           0);
}

static fan_driver_mode_t fan_driver_get_mode_from_str(const char* mode)
{
    if (strncmp(mode,
                fan_driver_mode[FAN_DRIVER_MODE_MANUAL],
                MIN(strlen(mode),
                    strlen(fan_driver_mode[FAN_DRIVER_MODE_MANUAL]))) == 0) {
        return FAN_DRIVER_MODE_MANUAL;
    } else if (strncmp(mode,
                       fan_driver_mode[FAN_DRIVER_MODE_AUTO],
                       MIN(strlen(mode),
                           strlen(fan_driver_mode[FAN_DRIVER_MODE_AUTO]))) ==
               0) {
        return FAN_DRIVER_MODE_AUTO;
    } else {
        syslog(LOG_ERR, "Error: unrecognized mode %s", mode);
        return FAN_DRIVER_MODE_AUTO;
    }
}
static void fan_driver_switch_mode()
{
    char buf[100] = {0};
    pread(fan_driver_attributes[FAN_DRIVER_ATTRIBUTE_MODE].ctrl.fd,
          buf,
          sizeof(buf) - 1,
          0);
    fan_driver_mode_t mode = fan_driver_get_mode_from_str(buf);
    fan_driver_set_mode(mode == FAN_DRIVER_MODE_AUTO ? FAN_DRIVER_MODE_MANUAL
                                                     : FAN_DRIVER_MODE_AUTO);
}

static int fan_driver_get_freq()
{
    char buf[100];
    pread(fan_driver_attributes[FAN_DRIVER_ATTRIBUTE_FREQ].ctrl.fd,
          buf,
          sizeof(buf) - 1,
          0);
    int freq = atoi(buf);
    return freq;
}

static void fan_driver_set_freq(uint8_t freq)
{
    char buf[100] = {0};
    snprintf(buf, sizeof(buf) - 1, "%u", freq);
    pwrite(fan_driver_attributes[FAN_DRIVER_ATTRIBUTE_FREQ].ctrl.fd,
           buf,
           strlen(buf),
           0);
}

static void fan_driver_speedup_freq()
{
    int freq = fan_driver_get_freq();
    fan_driver_set_freq(freq * 2);
}

static void fan_driver_slowdown_freq()
{
    int freq = fan_driver_get_freq();
    freq /= 2;
    fan_driver_set_freq(freq <= 0 ? 1 : freq);
}

static void fan_driver_setup(int efd)
{
    for (unsigned int i = 0; i < ARRAY_SIZE(fan_driver_attributes); i++) {
        struct fan_driver_ctrl* fan = &fan_driver_attributes[i];

        // open fd
        fan->ctrl.fd = open(fan->attribute_path, O_RDWR);
        if (fan->ctrl.fd == -1) {
            char msg[100] = {0};
            snprintf(msg,
                     sizeof(msg) - 1,
                     "ERROR: cannot open %s",
                     fan->attribute_path);
            syslog(LOG_ERR, msg);
            exit(EXIT_FAILURE);
        }

        // epoll configuration
        epoll_ctl(efd, EPOLL_CTL_ADD, fan->ctrl.fd, &fan->ctrl.event);
    }
}

static void fan_driver_close()
{
    for (unsigned int i = 0; i < ARRAY_SIZE(fan_driver_attributes); i++) {
        struct fan_driver_ctrl* fan = &fan_driver_attributes[i];

        close(fan->ctrl.fd);
    }
}

//-----------------------------------------------------------------------------
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
        snprintf(msg, sizeof(msg) - 1, "ERROR : can't export gpio %s", nr);
        syslog(LOG_ERR, msg);
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

//-----------------------------------------------------------------------------
// LED
//-----------------------------------------------------------------------------
// led power is on the GPIOL10
#define LED_POWER_GPIO_NR (32 * ('L' - 'A') + 10)

typedef enum {
    LED_POWER,
} led_t;

static int leds_fd[] = {
    [LED_POWER] = -1,
};

static const int leds_nr[] = {
    [LED_POWER] = LED_POWER_GPIO_NR,
};

static void set_led(led_t led, bool mode)
{
    pwrite(leds_fd[led], mode ? "1" : "0", 2, 0);
}

static void toggle_led(led_t led)
{
    char buf[100] = {0};
    pread(leds_fd[led], buf, sizeof(buf), 0);
    set_led(led, !(buf[0] == '1'));
}

static void leds_setup()
{
    for (unsigned int i = 0; i < ARRAY_SIZE(leds_fd); i++) {
        char gpio_nr[100] = {0};
        snprintf(gpio_nr, sizeof(gpio_nr) - 1, "%i", leds_nr[i]);
        leds_fd[i] = cfg_gpio_out(gpio_nr, false);
    }
}

static void leds_close()
{
    for (unsigned int i = 0; i < ARRAY_SIZE(leds_fd); i++) {
        close(leds_fd[i]);
    }
}

//-----------------------------------------------------------------------------
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
        syslog(LOG_ERR, "Error while reading button state");
        return;
    }

    enum button_state state = buf[0] == '0' ? BUTTON_RELEASED : BUTTON_PRESSED;

    if (btn->type == BUTTON_LEFT) {
        toggle_led(LED_POWER);

        if (state == BUTTON_PRESSED) {
            fan_driver_speedup_freq();
        }

    } else if (btn->type == BUTTON_CENTER) {
        toggle_led(LED_POWER);

        if (state == BUTTON_PRESSED) {
            fan_driver_slowdown_freq();
        }

    } else if (btn->type == BUTTON_RIGHT) {
        fan_driver_switch_mode();
    } else {
        syslog(LOG_ERR, "Error: button not supported (%i)", btn->type);
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
                    .fd    = -1,
                    .event = {.events = EPOLLET, .data.ptr = &buttons[0].ctrl},
                    .process = button_process,
                },
        },
    [1] =
        {
            .gpio_nr = "2",
            .edge    = "both",
            .type    = BUTTON_CENTER,
            .ctrl =
                {
                    .fd    = -1,
                    .event = {.events = EPOLLET, .data.ptr = &buttons[1].ctrl},
                    .process = button_process,
                },
        },
    [2] =
        {
            .gpio_nr = "3",
            .edge    = "falling",
            .type    = BUTTON_RIGHT,
            .ctrl =
                {
                    .fd    = -1,
                    .event = {.events = EPOLLET, .data.ptr = &buttons[2].ctrl},
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

//-----------------------------------------------------------------------------
// COMMUNICATION
//-----------------------------------------------------------------------------
struct mq_ctrl {
    struct ctrl ctrl;
    char* name;
};

static void mq_process(struct ctrl* ctrl)
{
    struct mq_ctrl* mq = (struct mq_ctrl*)ctrl;

    char buf[MQ_MAX_SIZE + 1] = {0};
    ssize_t n                 = mq_receive(mq->ctrl.fd, buf, MQ_MAX_SIZE, NULL);
    if (n == -1 && errno != 0) {
        syslog(LOG_ERR, "Error while reading message queue");
        return;
    }

    struct fan_msg* msg = (struct fan_msg*)buf;

    if (msg->msg_type == FAN_MSG_MODE) {
        fan_driver_mode_t mode = fan_driver_get_mode_from_str(msg->data);
        fan_driver_set_mode(mode);
    } else if (msg->msg_type == FAN_MSG_FREQUENCY) {
        int freq = atoi(msg->data);
        if (freq <= 0) {
            syslog(LOG_ERR, "Frequency not valid (%i)", freq);
        } else {
            fan_driver_set_freq(freq);
        }
    } else {
        printf("Not recognized\n");
    }
}

static struct mq_ctrl mq = {
    .ctrl =
        {
            .fd      = -1,
            .event   = {.events = EPOLLIN, .data.ptr = &mq.ctrl},
            .process = mq_process,
        },
    .name = MQ_NAME,
};

static void communication_create()
{
    struct mq_attr ma = {
        .mq_flags   = 0,
        .mq_maxmsg  = MQ_MAX_MSG,
        .mq_msgsize = MQ_MAX_SIZE,
        .mq_curmsgs = 0,
        .__pad      = {0},
    };

    mqd_t mqd = mq_open(mq.name, O_CREAT, 0666, &ma);
    if (mqd == -1) {
        syslog(LOG_ERR, "Error while opening message queue (%s)", mq.name);
        exit(EXIT_FAILURE);
    }
    mq_close(mqd);
}

static void communication_open(int efd)
{
    communication_create();
    mq.ctrl.fd = mq_open(mq.name, O_RDONLY | O_NONBLOCK);
    if (mq.ctrl.fd == -1) {
        char msg[100] = {0};
        snprintf(msg, sizeof(msg) - 1, "ERROR: cannot open %s", mq.name);
        syslog(LOG_ERR, msg);
        exit(EXIT_FAILURE);
    }
    epoll_ctl(efd, EPOLL_CTL_ADD, mq.ctrl.fd, &mq.ctrl.event);
}

static void communication_close()
{
    mq_close(mq.ctrl.fd);
    mq_unlink(mq.name);
}

//-----------------------------------------------------------------------------
// SIGNAL
//-----------------------------------------------------------------------------
static volatile bool signal_running = true;

static void catch_signal(int signal)
{
    syslog(LOG_INFO, "Catched SIG%s\n", sigabbrev_np(signal));
    if (signal == SIGTSTP || signal == SIGTERM) signal_running = false;
}

static inline bool signal_is_running() { return signal_running; }

//-----------------------------------------------------------------------------
// DAEMON
//-----------------------------------------------------------------------------

static void fork_process()
{
    pid_t pid = fork();
    switch (pid) {
        case 0:
            break;  // child process has been created
        case -1:
            syslog(LOG_ERR, "ERROR while forking");
            exit(1);
            break;
        default:
            exit(0);  // exit parent process with success
    }
}

static void create_daemon()
{
    // 1. fork off the parent process
    fork_process();

    // 2. create new session
    if (setsid() == -1) {
        syslog(LOG_ERR, "ERROR while creating new session");
        exit(1);
    }

    // 3. fork again to get rid of session leading process
    fork_process();

    // 4. capture all required signals
    struct sigaction act = {
        .sa_handler = catch_signal,
    };
    sigaction(SIGHUP, &act, NULL);   //  1 - hangup
    sigaction(SIGINT, &act, NULL);   //  2 - terminal interrupt
    sigaction(SIGQUIT, &act, NULL);  //  3 - terminal quit
    sigaction(SIGABRT, &act, NULL);  //  6 - abort
    sigaction(SIGTERM, &act, NULL);  // 15 - termination
    sigaction(SIGTSTP, &act, NULL);  // 19 - terminal stop signal

    signal_running = true;

    // 5. update file mode creation mask
    umask(0027);

    // 6. change working directory to appropriate place
    if (chdir("/opt") == -1) {
        syslog(LOG_ERR, "ERROR while changing to working directory");
        exit(1);
    }

    // 7. close all open file descriptors
    for (int fd = sysconf(_SC_OPEN_MAX); fd >= 0; fd--) {
        close(fd);
    }

    // 8. redirect stdin, stdout and stderr to /dev/null
    if (open("/dev/null", O_RDWR) != STDIN_FILENO) {
        syslog(LOG_ERR, "ERROR while opening '/dev/null' for stdin");
        exit(1);
    }
    if (dup2(STDIN_FILENO, STDOUT_FILENO) != STDOUT_FILENO) {
        syslog(LOG_ERR, "ERROR while opening '/dev/null' for stdout");
        exit(1);
    }
    if (dup2(STDIN_FILENO, STDERR_FILENO) != STDERR_FILENO) {
        syslog(LOG_ERR, "ERROR while opening '/dev/null' for stderr");
        exit(1);
    }

    // 9. option: open syslog for message logging
    openlog(NULL, LOG_NDELAY | LOG_PID, LOG_DAEMON);
    syslog(LOG_INFO, "Daemon has started...");

    // 10. option: get effective user and group id for appropriate's one
    struct passwd* pwd = getpwnam("root");
    if (pwd == 0) {
        syslog(LOG_ERR, "ERROR while reading daemon password file entry");
        exit(1);
    }

    // 11. option: change root directory
    if (chroot("/") == -1) {
        syslog(LOG_ERR, "ERROR while changing to new root directory");
        exit(1);
    }

    // 12. option: change effective user and group id for appropriate's one
    if (setegid(pwd->pw_gid) == -1) {
        syslog(LOG_ERR, "ERROR while setting new effective group id");
        exit(1);
    }
    if (seteuid(pwd->pw_uid) == -1) {
        syslog(LOG_ERR, "ERROR while setting new effective user id");
        exit(1);
    }
}

//-----------------------------------------------------------------------------
// MAIN
//-----------------------------------------------------------------------------

int main()
{
    // run the application as a daemon
    create_daemon();

    // create event poll
    int efd = epoll_create1(0);

    // open the fan driver
    fan_driver_setup(efd);

    // open the thermal zone
    temperatures_open(efd);

    // setup the leds
    leds_setup();

    // activate the button
    buttons_setup(efd);

    // activate the screen
    oled_init();
    oled_display_all();

    // open communication channel
    communication_open(efd);

    //  monitor events
    while (signal_is_running()) {
        struct epoll_event events[10];
        int ret = epoll_wait(efd, events, ARRAY_SIZE(events), -1);
        if (ret == -1) {
            syslog(LOG_ERR, "Error in epoll_wait");
        } else if (ret == 0) {
            syslog(LOG_ERR, "Error, nothing receive in epoll_wait");
        } else {
            for (int i = 0; i < ret; i++) {
                struct ctrl* ctrl = events[i].data.ptr;
                ctrl->process(ctrl);
            }
        }
    }

    communication_close();
    oled_close();
    buttons_close(efd);
    leds_close();
    temperatures_close();
    fan_driver_set_mode(FAN_DRIVER_MODE_AUTO);
    fan_driver_close();
    close(efd);

    syslog(LOG_INFO, "daemon stopped\n");
    closelog();

    return 0;
}