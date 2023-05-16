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
 * Project:     HEIA-FR / CSEL1 Laboratory 2
 * Author:      Luca Srdjenovic & Yerly Louka
 * Date:        05.05.2023
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define NUM_BLOCKS 50
#define BLOCK_SIZE (1 << 20)  // 1 Mebibyte

int main()
{
    void* blocks[NUM_BLOCKS];
    int goodBlocks = 0;

    // Allocation of memory
    for (int i = 0; i < NUM_BLOCKS; i++) {
        printf("Allocating block %d\n", i);
        blocks[i] = malloc(BLOCK_SIZE);

        // Check if the allocation was successful
        if (blocks[i] == NULL) {
            printf("Allocation of block %d failed\n", i);
            break;
        }

        memset(blocks[i], 0, BLOCK_SIZE);
        goodBlocks++;
        usleep(100000);
    }

    // Freeing of memory
    printf("Freeing memory\n");
    for (int i = 0; i < goodBlocks; i++) {
        free(blocks[i]);
    }

    printf("Done\n");

    return 0;
}
