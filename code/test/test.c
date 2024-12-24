#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "../rufs.h"
#include "../block.h"

static superblock_t superblock;
static char i_bitmap[I_BITMAP_SIZE];
static char d_bitmap[D_BITMAP_SIZE];

int main(int argc, char* argv[]) {

    memset(&superblock, 0, sizeof(superblock_t));
    superblock.magic_num = MAGIC_NUM;
    superblock.max_inum = MAX_INUM;
    superblock.max_dnum = MAX_DNUM;

    printf("MAX_INUM is: %d\n",superblock.max_inum);

    memset(i_bitmap, 0, I_BITMAP_SIZE);
    // uint8_t get_val = get_bitmap(i_bitmap, 0);
    if (get_bitmap(i_bitmap, 0) == 0) {
        set_bitmap(i_bitmap, 0);
    } else {
        perror("Failed to allocate bitmap for root directory.");
        return EXIT_FAILURE;
    }

    printf("bitmap byte 1: %d\n", i_bitmap[0]);

    // get_val = get_bitmap(i_bitmap, 1);

    if (get_bitmap(i_bitmap, 1) == 0) {
        set_bitmap(i_bitmap, 1);
    } else {
        perror("Failed to allocate bitmap for root directory.");
        return EXIT_FAILURE;
    }
    
    printf("bitmap byte 1 (after second set): %d\n", i_bitmap[0]);

    // get_val = get_bitmap(i_bitmap, 2);

    if (get_bitmap(i_bitmap, 2) == 0) {
        set_bitmap(i_bitmap, 2);
    } else {
        perror("Failed to allocate bitmap for root directory.");
        return EXIT_FAILURE;
    }
    
    printf("bitmap byte 1 (after third set): %d\n", i_bitmap[0]);


    return 0;
}