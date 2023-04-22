#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char* argv[])
{
    if (argc <= 1) return 0;

    /* open memory file descriptor */
    int fdw = open(argv[1], O_RDWR);
    fd_set read_fds;
    FD_ZERO(&read_fds);

    int counter  = 0;
    int status   = 0;
    int nbErrors = 0;

    printf("Waiting for button press... \n");
    while (1) {
        FD_SET(fdw, &read_fds);
        status = select(fdw + 1, &read_fds, NULL, NULL, NULL);
        if (status == -1) {
            nbErrors++;
            printf("Error: %d times \n", nbErrors);
        } else if (FD_ISSET(fdw, &read_fds)) {
            printf("Button pressed %d times \n", ++counter);

        } else {
            printf("No data.\n");
        }
    }

    return 0;
}
