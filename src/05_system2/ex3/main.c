#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define DURATION 10 // Duration in seconds for each thread to consume resources
#define NUM_THREADS 3

void *thread_function(void *arg) {
    int sum = 0;
    time_t start_time = time(NULL);

    while (time(NULL) - start_time < DURATION) {
        for (int i = 0; i < 10000000; i++) {
            // Intense computation example: calculating the sum of numbers
            sum += i;
        }
    }

    return NULL;
}

int main() {
    pthread_t threads[NUM_THREADS];

    for (int i = 0; i < NUM_THREADS; i++) {
        if (pthread_create(&threads[i], NULL, thread_function, NULL) != 0) {
            printf("Failed to create thread %d\n", i);
            exit(EXIT_FAILURE);
        }
    }
    printf("Threads created\n");

    printf("Waiting for threads to finish in %d seconds\n", DURATION);

    // Wait for each thread to finish consuming resources
    for (int i = 0; i < NUM_THREADS; i++) {
        if (pthread_join(threads[i], NULL) != 0) {
            printf("Failed to join thread %d\n", i);
            exit(EXIT_FAILURE);
        }
    }

    printf("Done\n");

    return 0;
}
