
#include <getopt.h>
#include <mqueue.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common.h"

#define MIN(a, b) (a < b ? a : b)

static mqd_t communication_open()
{
    mqd_t mqd = mq_open(MQ_NAME, O_WRONLY);
    if (mqd == -1) {
        perror("Cannot open mqueue " MQ_NAME);
        exit(EXIT_FAILURE);
    }
    return mqd;
}

static void communication_close(mqd_t mqd) {
    mq_close(mqd);
}

static void communication_send_mode(mqd_t mqd, char* mode)
{
    struct fan_msg msg = {0};

    msg.msg_type = FAN_MSG_MODE;
    memcpy(msg.data, mode, MIN(sizeof(msg.data) - 1, strlen(mode)));

    int ret = mq_send(mqd, (char*)&msg, sizeof(struct fan_msg), 0);
    if (ret < 0) {
        perror("Cannot send mode to mqueue " MQ_NAME);
        exit(EXIT_FAILURE);
    }
}

static void communication_send_frequency(mqd_t mqd, char* frequency)
{
    struct fan_msg msg = {0};

    msg.msg_type = FAN_MSG_FREQUENCY;
    memcpy(msg.data, frequency, MIN(sizeof(msg.data) - 1, strlen(frequency)));

    int ret = mq_send(mqd, (char*)&msg, sizeof(struct fan_msg), 0);
    if (ret < 0) {
        perror("Cannot send frequency to mqueue " MQ_NAME);
        exit(EXIT_FAILURE);
    }
}

static int check_mode(char* mode)
{
    int ret = -1;
    if (strncmp(mode, "manual", MIN(strlen(mode), sizeof("manual") - 1)) == 0) {
        ret = strlen(mode) == strlen("manual") ? 0 : -1;
    } else if (strncmp(mode, "auto", MIN(strlen(mode), sizeof("auto") - 1)) == 0) {
        ret = strlen(mode) == strlen("auto") ? 0 : -1;
    }

    // not a valid mode
    if(ret == -1) {
        fprintf(stderr, "Invalid mode (%s)\n", mode);
    }
    
    return ret;
}

static int check_freq(char* freq)
{
    int f = atoi(freq);
    if (f > 0 && f <= 50) {
        return 0;
    }
    fprintf(stderr, "Invalid frequency (%s)\n", freq);
    return -1;
}

static void print_usage()
{
    char* usage =
        "Usage: fan_ctrl [OPTION]...\n"
        "   --mode=[manual | auto]     set the mode of the fan_driver\n"
        "   --freq=value               set the frequency of the fan. The\n"
        "                              fan_driver must be in \"manual\" mode\n"
        "                              The value must be in range [1;50]\n";
        
    fprintf(stderr, usage);
}

int main(int argc, char** argv)
{
    int c;
    bool errflg         = false;
    bool noopt          = true;
    int option_index    = 0;
    char frequency[100] = {0};
    char mode[100]      = {0};

    while (true) {
        static struct option long_options[] = {
            {"freq", required_argument, 0, 0},
            {"mode", required_argument, 0, 0},
            {0, 0, 0, 0}};

        c = getopt_long(argc, argv, "", long_options, &option_index);
        if (c == -1) break;

        switch (c) {
            case 0:
                if (option_index == 0 && optarg != NULL) {
                    strncpy(frequency,
                            optarg,
                            MIN(sizeof(frequency) - 1, strlen(optarg)));
                    noopt = false;
                } else if (option_index == 1 && optarg != NULL) {
                    strncpy(
                        mode, optarg, MIN(sizeof(mode) - 1, strlen(optarg)));
                    noopt = false;
                }
                break;

            case '?':
                errflg = true;
                break;
        }
    }
    if (errflg || noopt) {
        print_usage();
        exit(EXIT_FAILURE);
    }

    mqd_t mqd = -1;
    if (mode[0] != 0 || frequency[0] != 0) {
        mqd = communication_open();
    }
    if (mode[0] != 0) {
        int status = check_mode(mode);
        if (status == 0) communication_send_mode(mqd, mode);
    }
    if (frequency[0] != 0) {
        int status = check_freq(frequency);
        if (status == 0) communication_send_frequency(mqd, frequency);
    }
    if(mqd >= 0) {
        communication_close(mqd);
    }
}