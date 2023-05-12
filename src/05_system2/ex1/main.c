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
 * Project:     HEIA-FR / CSEL1 Laboratory
 * Author:      Louka Yerly
 * Date:        05.05.2023
 */

#define _XOPEN_SOURCE 600
#define _DEFAULT_SOURCE
#define _GNU_SOURCE  // necessary for sigabbrev_np() and sched

#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#define TOKEN ";)"
#define EXIT_MESSAGE "exit"

#if 1
#define SOCK_TYPE SOCK_STREAM
#else
#define SOCK_TYPE SOCK_DGRAM
#endif

static void signal_handler(int s)
{
    printf("receive SIG%s\n", sigabbrev_np(s));
}

static void signal_setup()
{
    static bool installed_ = false;
    if (!installed_) {
        //  The signals SIGKILL and SIGSTOP cannot be caught, blocked, or
        //  ignored.

        struct sigaction act = {
            .sa_handler = signal_handler,
        };

        sigaction(SIGHUP, &act, NULL);     // 1
        sigaction(SIGINT, &act, NULL);     // 2
        sigaction(SIGQUIT, &act, NULL);    // 3
        sigaction(SIGILL, &act, NULL);     // 4
        sigaction(SIGTRAP, &act, NULL);    // 5
        sigaction(SIGABRT, &act, NULL);    // 6
        sigaction(SIGIOT, &act, NULL);     // 6
        sigaction(SIGBUS, &act, NULL);     // 7
        sigaction(SIGFPE, &act, NULL);     // 8
        sigaction(SIGKILL, &act, NULL);    // 9 - Will not be redefine
        sigaction(SIGUSR1, &act, NULL);    // 10
        sigaction(SIGSEGV, &act, NULL);    // 11
        sigaction(SIGUSR2, &act, NULL);    // 12
        sigaction(SIGPIPE, &act, NULL);    // 13
        sigaction(SIGALRM, &act, NULL);    // 14
        sigaction(SIGTERM, &act, NULL);    // 15
        sigaction(SIGSTKFLT, &act, NULL);  // 16
        sigaction(SIGCHLD, &act, NULL);    // 17
        sigaction(SIGCLD, &act, NULL);     // 17
        sigaction(SIGCONT, &act, NULL);    // 18
        sigaction(SIGSTOP, &act, NULL);    // 19 - Will not be redefine
        sigaction(SIGTSTP, &act, NULL);    // 20
        sigaction(SIGTTIN, &act, NULL);    // 21
        sigaction(SIGTTOU, &act, NULL);    // 22
        sigaction(SIGURG, &act, NULL);     // 23
        sigaction(SIGXCPU, &act, NULL);    // 24
        sigaction(SIGXFSZ, &act, NULL);    // 25
        sigaction(SIGVTALRM, &act, NULL);  // 26
        sigaction(SIGPROF, &act, NULL);    // 27
        sigaction(SIGWINCH, &act, NULL);   // 28
        sigaction(SIGPOLL, &act, NULL);    // 29
        sigaction(SIGIO, &act, NULL);      // 29
        sigaction(SIGPWR, &act, NULL);     // 30
        sigaction(SIGSYS, &act, NULL);     // 31

        installed_ = true;
    }
}

bool is_exit_message(const char* message)
{
    return strcmp(message, EXIT_MESSAGE) == 0;
}

int main()
{
    signal_setup();

    int fd[2];

    int sock_type = SOCK_TYPE;
    int err       = socketpair(AF_UNIX, sock_type, 0, fd);
    if (err == -1) {
        perror("Error while opening socket");
        exit(EXIT_FAILURE);
    }

    pid_t pid = fork();

    if (pid == 0) {
        // child
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(0, &set);
        int ret = sched_setaffinity(0, sizeof(set), &set);
        if (ret == -1) {
            perror("Cannot set cpu affinity");
        }

        close(fd[1]);
        int fd_child = fd[0];

        char* msg[] = {"hello", "from", "child"};

        for (unsigned int i = 0; i < ARRAY_SIZE(msg); i++) {
            write(fd_child, msg[i], strlen(msg[i]));

            // write a token for the STREAM type to separate messages (without
            // the \0)
            if (sock_type == SOCK_STREAM)
                write(fd_child, TOKEN, sizeof(TOKEN) - 1);
        }

        // wait a little bit to test the catch signal
        const int waiting_time_sec = 20;
        int remaining              = waiting_time_sec;
        while (remaining > 0) {
            remaining -= waiting_time_sec - sleep(remaining);
        }

        // exit application
        write(fd_child, EXIT_MESSAGE, sizeof(EXIT_MESSAGE));

        close(fd_child);

    } else if (pid > 0) {
        // parent
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(1, &set);
        int ret = sched_setaffinity(0, sizeof(set), &set);
        if (ret == -1) {
            perror("Cannot set cpu affinity");
        }

        close(fd[0]);
        int fd_parent = fd[1];

        bool running = true;
        while (running) {
            char buff[200] = {0};
            ssize_t len    = read(fd_parent, buff, sizeof(buff) - 1);

            if (len <= 0) {
                perror("Cannot receive data from the socket");
                continue;
            }

            if (sock_type == SOCK_STREAM) {
                // use the token to separate messages
                char* message = strtok(buff, TOKEN);
                while (message != NULL) {
                    printf("Parent - Receive msg: %s\n", message);

                    if (is_exit_message(message)) {
                        running = false;
                        break;
                    }
                    message = strtok(NULL, TOKEN);
                }
            } else if (sock_type == SOCK_DGRAM) {
                printf("Parent - Receive msg: %s\n", buff);
                if (is_exit_message(buff)) {
                    running = false;
                }
            } else {
                fprintf(stderr, "SOCK_TYPE not implemented\n");
            }
        }

        close(fd_parent);
        int status;
        wait(&status);

    } else {
        // error
        if (errno != 0) {
            perror("Error while forking");
            exit(EXIT_FAILURE);
        }
        exit(EXIT_FAILURE);
    }

    return EXIT_SUCCESS;
}
