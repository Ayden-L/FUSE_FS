/*
 *  Copyright (C) 2023 CS416 Rutgers CS
 *	Tiny File System
 *	File:	rufs.c
 *
 */

#define FUSE_USE_VERSION 26


#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <libgen.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "block.h"
#include "rufs.h"


// MAX_DIRENTS --> max number of dirents a directory can hold
#define MAX_DIRENTS ((BLOCK_SIZE / sizeof(dirent_t)) * NUM_DIRECT_PTRS)  // 16 denotes the amount of direct pointers in an inode
#define MAX_DIRENTS_IN_BLOCK ((BLOCK_SIZE / sizeof(dirent_t)))

char diskfile_path[PATH_MAX];

// Declare your in-memory data structures here

atomic_flag init = ATOMIC_FLAG_INIT;
static superblock_t superblock;
static char i_bitmap[I_BITMAP_SIZE];
static char d_bitmap[D_BITMAP_SIZE];
/* will hold a block (or blocks), depending on size allocated, that will need to be read or written to memory */
char *buff_mem;

int inodes_in_use = 0;  // book-keeping variable (may not be needed)
int superblock_index;
int i_bitmap_index;
int d_bitmap_index;
int inode_table_index;
int data_block_start;
int inodes_in_block;
int root_inode;

int num_of_components(char *path_interest, char **parts_of_path) {
    char path[1024];
    strcpy(path, path_interest);

    if (strcmp(path, "/") == 0) {
        // 0 indicates that the path is simply the root directory

        return 0;
    }

    // char* parts[BLOCK_SIZE];
    char *token;
    char *delim = "/";
    int count = 0;

    token = strtok(path, delim);

    while (token != NULL) {
        parts_of_path[count] = strdup(token);

        token = strtok(NULL, delim);
        count++;
    }

    if (count == 0) {
        // return -1 to indicate the path passed in was invalid or something went wrong

        return -1;
    } else {
        return count;
    }
}

/*
 * Get available inode number from bitmap
    returns -1 to indicate failure, else returns the inode position found that was available
 */
int get_avail_ino() {
    // Step 1: Read inode bitmap from disk
    memset(buff_mem, 0, BUFF_MEM_SIZE);
    int read_ret_stat = bio_read(i_bitmap_index, buff_mem);
    if (read_ret_stat < 0) {
        return -1;
    }

    // Step 2: Traverse inode bitmap to find an available slot
    for (int i = 0; i < I_BITMAP_SIZE; i++) {
        /* NOTE: buff_mem here only holds the i_bitmap, and the loop will only go as far as
                the length of this i_bitmap, so it's okay to pass buff_mem here*/

        if (get_bitmap(buff_mem, i) == 0) {
            // if a free bit is found, set it to allocated

            // Step 3: Update inode bitmap and write to disk
            set_bitmap(buff_mem, i);

            int write_ret_stat = bio_write(i_bitmap_index, buff_mem);
            if (write_ret_stat < 0) {
                return -1;
            }
            memset(buff_mem, 0, BUFF_MEM_SIZE);  // clear buffer once done with it

            return i;
        }
    }

    // if we get to here, return EXIT_FAILURE to indicate no free inode was found

    return -1;
}

/*
 * Get available data block number from bitmap
 */
int get_avail_blkno() {
    // Step 1: Read data block bitmap from disk
    memset(buff_mem, 0, BUFF_MEM_SIZE);
    int read_ret_stat = bio_read(d_bitmap_index, buff_mem);
    if (read_ret_stat < 0) {
        return -1;
    }

    // Step 2: Traverse data block bitmap to find an available slot
    for (int i = 0; i < D_BITMAP_SIZE; i++) {
        if (get_bitmap(buff_mem, i) == 0) {
            // if a free bit is found, set to allocated
            set_bitmap(buff_mem, i);

            // Step 3: Update data block bitmap and write to disk
            int write_ret_stat = bio_write(d_bitmap_index, buff_mem);
            if (write_ret_stat < 0) {
                return -1;
            }

            memset(buff_mem, 0, BUFF_MEM_SIZE);  // clear buffer once done with it

            return i + data_block_start;
            // return i;
        }
    }

    // if we get to here, return EXIT_FAILURE to indicate no free data block was found
    return -1;
}

/*
 * inode operations:
 * given an inode number, return that inode from disk
 */
int readi(uint16_t ino, struct inode *inode) {
    // Step 1: Get the inode's on-disk block number

    // inode table starts at block 3, add this to the target block
    int inode_block_num = inode_table_index + (ino / inodes_in_block);

    // Step 2: Get offset of the inode in the inode on-disk block
    int offset_in_block = (ino % inodes_in_block);

    // Step 3: Read the block from disk and then copy into inode structure

    // read the block to the buffer
    memset(buff_mem, 0, BUFF_MEM_SIZE);

    int read_ret_stat = bio_read(inode_block_num, buff_mem);

    if (read_ret_stat < 0) {
        return EXIT_FAILURE;
    }

    inode_t temp;
    memcpy(&temp, buff_mem + (sizeof(inode_t) * offset_in_block), sizeof(inode_t));

    *inode = temp;

    return EXIT_SUCCESS;
}

int writei(uint16_t ino, struct inode *inode) {
    // Step 1: Get the block number where this inode resides on disk

    int inode_block_num = inode_table_index + (ino / inodes_in_block);

    // Step 2: Get the offset in the block where this inode resides on disk
    int offset_in_block = (ino % inodes_in_block);

    // Step 3: Write inode to disk

    // read the target block into buff_mem
    memset(buff_mem, 0, BUFF_MEM_SIZE);
    int read_ret_stat = bio_read(inode_block_num, buff_mem);
    if (read_ret_stat < 0) {
        return EXIT_FAILURE;
    }
    // offset into buff_mem (which holds the target block), then save the inode there
    memcpy((buff_mem + (offset_in_block * sizeof(inode_t))), inode, sizeof(inode_t));  // double check this
    for (int i = 0; i <= ino; i++) {
        inode_t *temp = buff_mem + i * sizeof(inode_t);
    }
    int write_ret_stat = bio_write(inode_block_num, buff_mem);
    if (write_ret_stat < 0) {
        return EXIT_FAILURE;
    }
    memset(buff_mem, 0, BUFF_MEM_SIZE);

    bio_read(inode_block_num, buff_mem);
    for (int i = 0; i <= ino; i++) {
        inode_t *temp = buff_mem + i * sizeof(inode_t);
    }
    memset(buff_mem, 0, BUFF_MEM_SIZE);

    return EXIT_SUCCESS;
}

/*
 * 	directory operations:
        given the ino of the current directory,
        check to see if a desired file or sub-directory exists,
                if so, then save to struct dirent *dirent
 */
int dir_find(uint16_t ino, const char *fname, size_t name_len, struct dirent *dirent) {
    // Step 1: Call readi() to get the inode using ino (inode number of current directory)

    inode_t temp_inode;
    int ret_stat = readi(ino, &temp_inode);
    if (ret_stat == EXIT_FAILURE) {
        return EXIT_FAILURE;
    }

    for (int i = 0; i < 16; i++) {
    }

    // Step 2: Get data block of current directory from inode
    for (int i = 0; i < 16; i++) {
        // traverse through the direct_ptr array (NOTE: there are only 16 direct blocks in an inode)
        if (temp_inode.direct_ptr[i] >= data_block_start) {  // NOTE: valid data blocks are >= 67

            // if the index points to a valid (data) block, read that block to buff_mem:
            memset(buff_mem, 0, BUFF_MEM_SIZE);

            // read the data block into buff mem
            int read_ret_stat = bio_read(temp_inode.direct_ptr[i], buff_mem);

            if (read_ret_stat < 0) {
                return EXIT_FAILURE;
            }

            dirent_t temp_dirent;
            /*
                    will need to check how this conditional performs
                    ==> since (BLOCK_SIZE/sizeof(dirent_t)) ==> (4096 / 214) = 19.14

                    it might read past the given block accidentally
            */

            // switched to this way instead:
            int max_dirents_in_block = (BLOCK_SIZE / sizeof(dirent_t));  // max num of dirents in a block (that fit evenly)
            int last = (max_dirents_in_block * sizeof(dirent_t));        // once j == last, it would have read all the dirents of the block
            int j = 0;
            while (j <= last) {
                // check each directory entry (dirent) for the valid (data) block:
                memset(&temp_dirent, 0, sizeof(dirent_t));
                memcpy(&temp_dirent, buff_mem + j, sizeof(dirent_t));

                if (temp_dirent.len == name_len) {
                    if (strcmp(fname, temp_dirent.name) == 0) {
                        // if the name matches, then copy directory entry to dirent structure

                        *dirent = temp_dirent;
                        return EXIT_SUCCESS;
                    }
                }

                // increment to the next dirent
                j += sizeof(dirent_t);
            }
        }
    }

    // return EXIT_FAILURE to indicate no matching exists
    return EXIT_FAILURE;
}

int dir_add(struct inode dir_inode, uint16_t f_ino, const char *fname, size_t name_len) {
    /*
            1. check to see if the dirent we want to add already exists
            2a. if it does not, try to add the dirent to the directory's current data blocks
            2b. if there is no room in the directory's current data blocks, allocate another data block
            and put the new dirent there (if possible --> because all direct pointers may be in use, in this case throw error?)
    */

    // test printing data blocks of the directory inode:

    for (int i = 0; i < 16; i++) {
    }

    // Step 1: Read dir_inode's data block and check each directory entry of dir_inode to see if target already exists
    for (int i = 0; i < 16; i++) {
        // if the direct pointer points to a valid data block check its dirents
        if (dir_inode.direct_ptr[i] >= data_block_start) {
            // read the data block pointed to by direct_ptr[i] into buff_mem
            memset(buff_mem, 0, BUFF_MEM_SIZE);
            int read_ret_stat = bio_read(dir_inode.direct_ptr[i], buff_mem);
            if (read_ret_stat < 0) {
                return EXIT_FAILURE;
            }

            dirent_t temp_dirent;
            int max_dirents_in_block = (BLOCK_SIZE / sizeof(dirent_t));  // max num of dirents in a block (that fit evenly)
            int last = (max_dirents_in_block * sizeof(dirent_t));        // once j == last, it would have read all the dirents of the block
            int j = 0;

            // traverse the dirents in this data block
            while (j != last) {
                // check each directory entry (dirent) for the valid (data) block:
                memset(&temp_dirent, 0, sizeof(dirent_t));
                memcpy(&temp_dirent, buff_mem + j, sizeof(dirent_t));

                // Step 2: Check if fname (directory name) is already used in other entries
                // if the dirent we'd like to add already exists return EXIT_FAILURE
                if (strcmp(temp_dirent.name, fname) == 0 && temp_dirent.ino == f_ino && temp_dirent.len == name_len) {
                    return EXIT_FAILURE;
                }

                // increment to the next dirent
                j += sizeof(dirent_t);
            }
        }
    }

    // at this point, we know the dirent we'd like to add does not exist in the current directory, so -->

    /*
            - Step 3: Add directory entry in dir_inode's data block and write to disk
                    - traverse the data blocks
                    - find one that has enough space to hold another dirent
                    -
    */

    dirent_t res_dirent = {
        .ino = f_ino,
        .valid = 1,
        .len = name_len};
    strcpy(res_dirent.name, fname);

    // if the directory's data blocks are full of dirents and we cant add another --> throw error
    if (dir_inode.size != MAX_DIRENTS) {
        int dir_inode_block_num = inode_table_index + (dir_inode.ino / inodes_in_block);
        int offset_in_block = (dir_inode.ino % inodes_in_block);

        // traverse the data blocks of the dir_inode/current directory
        for (int i = 0; i < 16; i++) {
            // if the data block is valid
            if (dir_inode.direct_ptr[i] >= data_block_start) {
                // read the data block pointed to by direct_ptr[i] into buff_mem
                memset(buff_mem, 0, BUFF_MEM_SIZE);
                int read_ret_stat = bio_read(dir_inode.direct_ptr[i], buff_mem);
                if (read_ret_stat < 0) {
                    // free(dir_inode_block);
                    return EXIT_FAILURE;
                }

                dirent_t temp_dirent;
                int max_dirents_in_block = (BLOCK_SIZE / sizeof(dirent_t));  // max num of dirents in a block (that fit evenly)
                int last = (max_dirents_in_block * sizeof(dirent_t));        // once j == last, it would have read all the dirents of the block
                int j = 0;

                // traverse the dirent positions in this data block and check if there is an opening
                while (j != last) {
                    // check each directory entry (dirent) for the block:
                    memset(&temp_dirent, 0, sizeof(dirent_t));
                    memcpy(&temp_dirent, buff_mem + j, sizeof(dirent_t));
                    // temp_dirent = (buff_mem + j);

                    // if the dirent we are reading is empty we have found somewhere to put the new dirent
                    if (temp_dirent.ino == 0) {
                        // if (temp_dirent == NULL) {

                        // if there is an open spot here within the data block, place the dirent here:
                        memcpy(buff_mem + j, &res_dirent, sizeof(dirent_t));

                        // write dirent to disk (write data block back to memory)
                        int write_ret_stat = bio_write(dir_inode.direct_ptr[i], buff_mem);
                        if (write_ret_stat < 0) {
                            // free(dir_inode_block);
                            return EXIT_FAILURE;
                        }

                        // update directory inode
                        dir_inode.size += sizeof(dirent_t);
                        dir_inode.link += 1;  // i think so
                        dir_inode.vstat.st_atime = time(NULL);
                        dir_inode.vstat.st_nlink += 1;

                        memset(buff_mem, 0, BUFF_MEM_SIZE);
                        int read_ret_stat = bio_read(dir_inode_block_num, buff_mem);
                        if (read_ret_stat < 0) {
                            return EXIT_FAILURE;
                        }

                        // save the updated current directory inode into the directory inode block
                        memcpy(buff_mem + (offset_in_block * sizeof(dirent_t)), &dir_inode, sizeof(dirent_t));

                        // write the updated dir_inode back to disk (I believe)
                        write_ret_stat = bio_write(dir_inode_block_num, buff_mem);
                        if (write_ret_stat < 0) {
                            // free(dir_inode_block);
                            return EXIT_FAILURE;
                        }

                        /////////////////////////////////////////////////////
                        /* TESTING CODE BELOW (to see if data block holds correct data) --> delete later*/
                        memset(buff_mem, 0, BUFF_MEM_SIZE);
                        if (bio_read(dir_inode.direct_ptr[i], buff_mem) < 0) {
                            // free(dir_inode_block);
                            return EXIT_FAILURE;
                        }
                        dirent_t my_dirent;
                        memset(&my_dirent, 0, sizeof(dirent_t));
                        memcpy(&my_dirent, buff_mem, sizeof(dirent_t));

                        memset(buff_mem, 0, BUFF_MEM_SIZE);

                        writei(dir_inode.ino, &dir_inode);

                        return EXIT_SUCCESS;
                    }

                    // increment to the next dirent
                    j += sizeof(dirent_t);
                }
            }
        }

        /*
                if we get to here after the above, then there wasn't any free spots within the already allocated
                data blocks of the directory, so try to add another datablock to the directory
        */

        // traverse the direct pointers of the dir_inode
        for (int i = 0; i < 16; i++) {
            // if there is an opening to put another data block, allocate, update, and save
            if (dir_inode.direct_ptr[i] == 0) {
                // find the next available block
                int avail_d_block = get_avail_blkno();
                if (avail_d_block == -1) {
                    perror("********** dir_add() Couldn't find open data block");
                    // free(dir_inode_block);
                    return EXIT_FAILURE;
                }

                dir_inode.direct_ptr[i] = avail_d_block;

                // read the new data block from disk
                memset(buff_mem, 0, BUFF_MEM_SIZE);
                int read_ret_stat = bio_read(dir_inode.direct_ptr[i], buff_mem);
                if (read_ret_stat < 0) {
                    // free(dir_inode_block);
                    return EXIT_FAILURE;
                }

                // put the dirent into the new data block
                memcpy(buff_mem, &res_dirent, sizeof(dirent_t));

                // write the new and updated data block back to disk
                int write_ret_stat = bio_write(dir_inode.direct_ptr[i], buff_mem);

                dirent_t *test_dirent = (dirent_t *)buff_mem;

                if (write_ret_stat < 0) {
                    // free(dir_inode_block);
                    return EXIT_FAILURE;
                }

                // update the current directory inode
                dir_inode.size += sizeof(dirent_t);
                dir_inode.link += 1;  // i think so
                dir_inode.vstat.st_atime = time(NULL);
                dir_inode.vstat.st_nlink += 1;

                memset(buff_mem, 0, BUFF_MEM_SIZE);
                read_ret_stat = bio_read(dir_inode_block_num, buff_mem);
                if (read_ret_stat < 0) {
                    return EXIT_FAILURE;
                }

                // save the updated current directory inode into the directory inode block
                memcpy(buff_mem + (offset_in_block * sizeof(dirent_t)), &dir_inode, sizeof(dirent_t));

                for (int k = 0; k <= dir_inode.ino; k++) {
                    dirent_t *temp = buff_mem + k * sizeof(dirent_t);
                }

                // write the updated dir_inode block back to disk
                write_ret_stat = bio_write(dir_inode_block_num, buff_mem);
                if (write_ret_stat < 0) {
                    // free(dir_inode_block);
                    return EXIT_FAILURE;
                }
                // update the inode in the inode directory that stores all the inodes
                writei(dir_inode.ino, &dir_inode);

                // free(dir_inode_block);
                return EXIT_SUCCESS;
            }
        }

    } else {
        perror("Directory is full, can't add another directory entry");
        return EXIT_FAILURE;
    }
}


int dir_remove(struct inode dir_inode, const char *fname, size_t name_len) {
    // Step 1: Read dir_inode's data block and checks each directory entry of dir_inode

    // Step 2: Check if fname exist

    // Step 3: If exist, then remove it from dir_inode's data block and write to disk

    return 0;
}

/*
 * namei operation
 */
int get_node_by_path(const char *path, uint16_t ino, struct inode *inode) {
    // Step 1: Resolve the path name, walk through path, and finally, find its inode.
    // Note: You could either implement it in a iterative way or recursive way

    char **parts_of_path[1024];

    // split the input path into components and store in 'parts_of_path'
    // save the number of components in 'termination_count'
    // --> this indicates how many times we need to run the loop until we reach the terminal point
    int termination_count = num_of_components(path, parts_of_path);

    inode_t temp_inode;
    inode_t res_inode;
    dirent_t temp_dirent;

    if (termination_count == -1) {
        // indicate something went wrong
        return EXIT_FAILURE;
    } else if (termination_count == 0) {
        // the path must be the root directory '/'

        /* from the rufs_mkfs() we know root dir has inode of 0, since we made it that way*/
        /* so then read the root inode and return*/

        int ret_stat = readi(root_inode, &temp_inode);
        if (ret_stat == EXIT_FAILURE) {
            return EXIT_FAILURE;
        }

        *inode = temp_inode;

        return EXIT_SUCCESS;
    }

    // else, if the path needs traversing... traverse it:
    int inode_for_search = ino;  // should contain root inode at first go
    int ret_stat;
    for (int i = 0; i < termination_count; i++) {
        // search to see if each part of the path exists

        ret_stat = dir_find(inode_for_search, parts_of_path[i], strlen(parts_of_path[i]), &temp_dirent);
        if (ret_stat == EXIT_FAILURE) {
            return EXIT_FAILURE;
        }

        // update inode for search as you work your way through the path
        inode_for_search = temp_dirent.ino;
    }

    /*
            once the termination point is reached temp_dirent should contain its inode,
            so read its inode into temp_inode then return it
    */
    int read_ret_stat = readi(temp_dirent.ino, &temp_inode);
    if (read_ret_stat == EXIT_FAILURE) {
        return EXIT_FAILURE;
    }

    *inode = temp_inode;

    return EXIT_SUCCESS;
}

/*
 * Make file system
 */
int rufs_mkfs() {
    if (atomic_flag_test_and_set(&init) == 0) {
        // create a buffer memory to hold intermediate values between read and writes
        buff_mem = (char *)malloc(BUFF_MEM_SIZE);
        if (!buff_mem) {
            perror("Failed to allocate memory for buff_mem");
            return EXIT_FAILURE;
        }
        memset(buff_mem, 0, BUFF_MEM_SIZE);

        // Call dev_init() to initialize (Create) Diskfile
        dev_init(diskfile_path);

        // set up local variables
        superblock_index = 0;
        i_bitmap_index = 1;
        d_bitmap_index = 2;
        /*
                calculate how many inodes fit in 1 block
                (w/ 4KB blocks and the inode struct given to us, there should be 16 inodes in 1 block)
        */
        inodes_in_block = (BLOCK_SIZE / sizeof(inode_t));
        /*
                calculate how many blocks are needed for the inode table
                (w/ 16 inodes per block, and a total of 1024 inodes
                ==> 1024/16 = 64 block for inodes)
        */
        int inode_table_size = (MAX_INUM / inodes_in_block);  // = 64 blocks
        inode_table_index = 3;
        data_block_start = (inode_table_size + inode_table_index);  // also serves as end of inode table

        /*
                read the first block of disk and store into buff_mem
                (this is where the superblock will go)
        */

        // write superblock information
        memset(&superblock, 0, sizeof(superblock_t));
        superblock.magic_num = MAGIC_NUM;
        superblock.max_inum = MAX_INUM;
        superblock.max_dnum = MAX_DNUM;
        superblock.i_bitmap_blk = i_bitmap_index;
        superblock.d_bitmap_blk = d_bitmap_index;
        superblock.i_start_blk = inode_table_index;
        superblock.d_start_blk = data_block_start;

        // copy superblock to buffer, then write to disk
        memcpy(buff_mem, &superblock, sizeof(superblock_t));
        int write_ret_stat = bio_write(superblock_index, buff_mem);
        if (write_ret_stat < 0) {
            return EXIT_FAILURE;
        }
        memset(buff_mem, 0, BUFF_MEM_SIZE);  // clear the buffer

        // initialize i_bitmap
        memset(i_bitmap, 0, I_BITMAP_SIZE);
        // copy i_bitmap to the buffer then write buffer to disk
        memcpy(buff_mem, i_bitmap, I_BITMAP_SIZE);
        write_ret_stat = bio_write(i_bitmap_index, buff_mem);
        if (write_ret_stat < 0) {
            return EXIT_FAILURE;
        }
        memset(buff_mem, 0, BUFF_MEM_SIZE);

        // initialize d_bitmap
        memset(d_bitmap, 0, D_BITMAP_SIZE);

        // copy d_bitmap to the buffer then write buffer to disk
        memcpy(buff_mem, d_bitmap, D_BITMAP_SIZE);
        write_ret_stat = bio_write(d_bitmap_index, buff_mem);
        if (write_ret_stat < 0) {
            return EXIT_FAILURE;
        }
        memset(buff_mem, 0, BUFF_MEM_SIZE);

        // update bitmap information for root directory

        root_inode = get_avail_ino();

        // create inode for root directory
        inode_t local_root_inode = {
            .ino = root_inode,         // inode # for root directory is 0
            .valid = 1,                // not sure what to set this to yet, so I set to 1 for now to indicate its in use
            .size = sizeof(dirent_t),  // keep track of the size of the root dir
            .type = __S_IFDIR | 0755,  // set file type and permissions
            .link = 2,                 // . and .. are the links for the root
            .direct_ptr = {0},         // nothing for now, then update once dirent is created and linked
            .indirect_ptr = {0},
            .vstat = {
                .st_mtime = time(NULL),  // clock the make time of the root dir
                .st_atime = time(NULL),  // clock the access time of the root dir
                .st_nlink = 2}};

        local_root_inode.direct_ptr[0] = get_avail_blkno();

        // copy this inode into the buffer
        memset(buff_mem, 0, BUFF_MEM_SIZE);
        memcpy(buff_mem, &local_root_inode, sizeof(inode_t));
        // write buffer to disk
        write_ret_stat = bio_write(inode_table_index, buff_mem);
        if (write_ret_stat < 0) {
            return EXIT_FAILURE;
        }
        memset(buff_mem, 0, BUFF_MEM_SIZE);

        // create dirent for root directory
        dirent_t root_dirent_1 = {
            .ino = 0,
            .valid = 1,
            .name = "/",
            .len = 1};

        dirent_t root_dirent_2 = {
            .ino = 0,
            .valid = 1,
            .name = ".",
            .len = 1};

        dirent_t root_dirent_3 = {
            .ino = 0,
            .valid = 1,
            .name = "..",
            .len = 2};

        // copy the dirent created to the buffer:
        memcpy(buff_mem, &root_dirent_1, sizeof(dirent_t));
        memcpy(buff_mem + sizeof(dirent_t), &root_dirent_2, sizeof(dirent_t));
        memcpy(buff_mem + (2 * sizeof(dirent_t)), &root_dirent_3, sizeof(dirent_t));

        // write buffer to disk at block 67 (which is data block 0)
        write_ret_stat = bio_write(local_root_inode.direct_ptr[0], buff_mem);
        if (write_ret_stat < 0) {
            return EXIT_FAILURE;
        }
        memset(buff_mem, 0, BUFF_MEM_SIZE);
    }

    return 0;
}

/*
 * FUSE file operations
 */
static void *rufs_init(struct fuse_conn_info *conn) {
    // Step 1a: If disk file is not found, call mkfs
    int disk = dev_open(diskfile_path);
    if (disk == -1) {
        rufs_mkfs();
    } else {
        // Step 1b: If disk file is found, just initialize in-memory data structures
        // and read superblock from disk

        buff_mem = (char *)malloc(BUFF_MEM_SIZE);
        if (!buff_mem) {
            perror("Failed to allocate memory for buff_mem");
            return EXIT_FAILURE;
        }

        memset(buff_mem, 0, BUFF_MEM_SIZE);
        // Read the superblock and get from memory

        superblock_index = 0;
        i_bitmap_index = 1;
        d_bitmap_index = 2;

        if (bio_read(superblock_index, buff_mem) < 0) {
            return EXIT_FAILURE;
        }
        // Now we've read the superblock into memory

        superblock_t *casted_superblock = (superblock_t *)buff_mem;
        i_bitmap_index = casted_superblock->i_bitmap_blk;
        d_bitmap_index = casted_superblock->d_bitmap_blk;

        inode_table_index = casted_superblock->i_start_blk;
        data_block_start = casted_superblock->d_start_blk;

        inodes_in_block = (BLOCK_SIZE / sizeof(inode_t));
        int inode_table_size = (MAX_INUM / inodes_in_block);  // = 64 blocks
    }

    return NULL;
}

static void rufs_destroy(void *userdata) {
    // Step 1: De-allocate in-memory data structures
    free(buff_mem);

    // Step 2: Close diskfile
    dev_close();
}

static int rufs_getattr(const char *path, struct stat *stbuf) {
    // Step 1: call get_node_by_path() to get inode from path
    inode_t path_node;
    int ret = get_node_by_path(path, root_inode, &path_node);  // Adjust root_inode as needed

    if (ret == EXIT_FAILURE) {
        // If the inode is not found, return an appropriate error

        return -ENOENT;  // No such file or directory
    }

    // Step 2: fill attribute of file into stbuf from inode
    stbuf->st_uid = getuid();                    // user ID of owner
    stbuf->st_gid = getgid();                    // group ID of owner
    stbuf->st_nlink = path_node.link;            // number of links
    stbuf->st_size = path_node.size;             // size of the file
    stbuf->st_ctime = path_node.vstat.st_ctime;  // creation time
    stbuf->st_atime = path_node.vstat.st_atime;  // last access time
    stbuf->st_mtime = path_node.vstat.st_mtime;  // last modification time
    stbuf->st_mode = path_node.type;             // type of file

    return 0;  // Success
}

static int rufs_opendir(const char *path, struct fuse_file_info *fi) {
    // Step 1: Call get_node_by_path() to get inode from path

    // For now we can assume that the path will always be from root? So ino will be 0
    inode_t *path_inode = (inode_t *)malloc(sizeof(inode_t));

    int result = get_node_by_path(path, root_inode, path_inode);

    if (result == EXIT_FAILURE) {
        free(path_inode);
        return -1;
    }

    // Step 2: If not find, return -1
    free(path_inode);

    return 0;
}

static int rufs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
    // Step 1: Call get_node_by_path() to get inode from path

    inode_t dir_inode;

    int get_node_result = get_node_by_path(path, root_inode, &dir_inode);

    if (get_node_result == EXIT_FAILURE) {
        return EXIT_FAILURE;
    }

    // Instantiate buffer
    dirent_t all_dirents[dir_inode.link];
    dirent_t current_dirent;

    for (int i = 0; i < NUM_DIRECT_PTRS; i++) {
        if (dir_inode.direct_ptr[i] >= data_block_start) {  // Check if valid

            memset(buff_mem, 0, BUFF_MEM_SIZE);
            if (bio_read(dir_inode.direct_ptr[i], buff_mem) < 0) {
                return EXIT_FAILURE;
            }

            int last = MAX_DIRENTS_IN_BLOCK * sizeof(dirent_t);

            // Step 2: Read directory entries from its data blocks, and copy them to filler
            for (int j = 0; j <= last; j += sizeof(dirent_t)) {
                // do something more sophisticated but for now just directly copying the data
                memcpy(&all_dirents[j % sizeof(dirent_t)], buff_mem + j, sizeof(dirent_t));

                dirent_t temp_dirent;
                memset(&temp_dirent, 0, sizeof(dirent_t));
                memcpy(&temp_dirent, buff_mem + j, sizeof(dirent_t));

                if (temp_dirent.len == 0) {
                    return EXIT_SUCCESS;
                }

                // offset += offset;
                // filler(buffer, all_dirents[j % sizeof(dirent_t)].name, NULL, offset + sizeof(dirent_t));
                filler(buffer, all_dirents[j % sizeof(dirent_t)].name, NULL, offset);  // maybe
            }
        }
    }

    return 0;
}

static int rufs_mkdir(const char *path, mode_t mode) {
    // Step 1: Use dirname() and basename() to separate parent directory path and target directory name

    char *dirname_copy[strlen(path)];
    char *basename_copy[strlen(path)];

    strcpy(dirname_copy, path);
    strcpy(basename_copy, path);

    // Step 1: Use dirname() and basename() to separate parent directory path and target file name
    char *dir_name = dirname(dirname_copy);
    char *path_name = basename(basename_copy);

    // Step 2: Call get_node_by_path() to get inode of parent directory

    inode_t parent_dir_node;
    if (get_node_by_path(dir_name, root_inode, &parent_dir_node) == EXIT_FAILURE) {
        return EXIT_FAILURE;
    }

    // Step 3: Call get_avail_ino() to get an available inode number

    int new_ino_num = get_avail_ino();

    if (new_ino_num == -1) {
        return EXIT_FAILURE;
    }

    inode_t new_inode = {
        .ino = new_ino_num,
        .valid = 1,
        .size = 0,
        .type = __S_IFDIR,
        .link = 2,
        .direct_ptr = {0},
        .indirect_ptr = {0},
        .vstat = {
            .st_atime = time(NULL),
            .st_ctime = time(NULL),
            .st_mtime = time(NULL),
            .st_gid = getegid(),
            .st_uid = getuid(),
            .st_ino = new_ino_num,
            .st_mode = __S_IFDIR,
            .st_nlink = 2,
        }};

    // Step 4: Call dir_add() to add directory entry of target directory to parent directory
    // Step 5: Update inode for target directory
    int result = dir_add(parent_dir_node, new_ino_num, path_name, strlen(path_name));

    if (result == EXIT_SUCCESS) {
        // Step 6: Call writei() to write inode to disk
        if (writei(new_ino_num, &new_inode) == EXIT_SUCCESS) {
            return EXIT_SUCCESS;
        } else {
            return EXIT_FAILURE;
        }
    } else {
        return EXIT_FAILURE;
    }

    return 0;
}

static int rufs_rmdir(const char *path) {
    // Step 1: Use dirname() and basename() to separate parent directory path and target directory name

    char *dir_name = dirname(path);
    char *path_name = basename(path);

    // Step 2: Call get_node_by_path() to get inode of target directory

    inode_t parent_dir_node;

    get_node_by_path(dir_name, root_inode, &parent_dir_node);
    // Step 3: Clear data block bitmap of target directory

    // Step 4: Clear inode bitmap and its data block

    // Step 5: Call get_node_by_path() to get inode of parent directory

    // Step 6: Call dir_remove() to remove directory entry of target directory in its parent directory

    return 0;
}

static int rufs_releasedir(const char *path, struct fuse_file_info *fi) {
    
    return 0;
}

static int rufs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    char *dirname_copy[strlen(path)];
    char *basename_copy[strlen(path)];

    strcpy(dirname_copy, path);
    strcpy(basename_copy, path);

    // Use dirname() and basename() to separate parent directory path and target file name
    char *dir_name = dirname(dirname_copy);
    char *path_name = basename(basename_copy);

    // Call get_node_by_path() to get inode of parent directory
    inode_t parent_dir_node;
    int get_path_stat = get_node_by_path(dir_name, root_inode, &parent_dir_node);  // get inode of parent directory with directory name (dir_name)
    if (get_path_stat == EXIT_FAILURE) {
        return EXIT_FAILURE;
    }
    // Call get_avail_ino() to get an available inode number
    int new_ino_num = get_avail_ino();

    inode_t new_inode = {
        .ino = new_ino_num,
        .valid = 1,
        .size = 0,
        .type = __S_IFREG,
        .link = 1,
        .direct_ptr = {0},
        .indirect_ptr = {0},
        .vstat = {
            .st_atime = time(NULL),
            .st_ctime = time(NULL),
            .st_mtime = time(NULL),
            .st_gid = getegid(),
            .st_uid = getuid(),
            .st_ino = new_ino_num,
            .st_mode = __S_IFREG,
            .st_nlink = 1,
        }};

    // Call dir_add() to add directory entry of target file to parent directory

    int result = dir_add(parent_dir_node, new_ino_num, path_name, strlen(path_name));
    if (result == EXIT_SUCCESS) {
        // if the directory entry was added successfully, write to disk the new inode
        if (writei(new_ino_num, &new_inode) == EXIT_SUCCESS) {
            return EXIT_SUCCESS;
        } else
            return EXIT_FAILURE;
    } else
        return EXIT_FAILURE;

    // Update inode for target file

    // Call writei() to write inode to disk

    return 0;
}

static int rufs_open(const char *path, struct fuse_file_info *fi) {
    // Call get_node_by_path() to get inode from path

    inode_t target_ino;

    if (get_node_by_path(path, root_inode, &target_ino) < 0) {  // Did not find ino

        return -1;
    }

    // If not find, return -1
    return -1;
}

static int rufs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
    // Step 1: You could call get_node_by_path() to get inode from path

    inode_t target_ino;

    int get_path_result = get_node_by_path(path, root_inode, &target_ino);

    if (get_path_result == EXIT_FAILURE) {
        return -EXIT_FAILURE;
    }

    // Step 2: Based on size and offset, read its data blocks from disk
    if (size < 0) {
        return -EXIT_FAILURE;
    }

    for (int i = 0; i < NUM_DIRECT_PTRS; i++) {
    }

    // NOTE: this variable will always be one (debug with print statements to see how it is executed)
    int num_blocks_to_read = ceil(size / BLOCK_SIZE);
    if (num_blocks_to_read == 0) {
        num_blocks_to_read = 1;
    }

    /*
        if offset = 0, then read starting from first data block (data block 0)
        if offset > 5000, then read starting from data block 1 (specifically, offset amount in data block 1) of target inode
    */
    int block_where_offset_is = (offset / BLOCK_SIZE);

    for (int i = 0; i < NUM_DIRECT_PTRS; i++) {
        // if we're at the direct pointer that points to the data block where offset starts:
        if (i == block_where_offset_is) {
            // read this data block

            if (target_ino.direct_ptr[i] >= data_block_start) {
                memset(buff_mem, 0, BUFF_MEM_SIZE);
                if (bio_read(target_ino.direct_ptr[i], buff_mem) < 0) {
                    return -EXIT_FAILURE;
                }

                // now buff_mem holds the data block where offset is located
                // copy size bytes from the offset of the data block to the buffer

                // Step 3: copy the correct amount of data from offset to buffer
                offset %= BLOCK_SIZE;
                memcpy(buffer, buff_mem + offset, ((size % BLOCK_SIZE == 0) ? BLOCK_SIZE : size % BLOCK_SIZE));

                /* NOTE: need to account for the scenario where you'll need to read more than 1 data block */

                return size;

            } else {
            }
        }
    }

    // Upon success, return size
    return -1;
}

/*
    size --> size of the data to write
    offset --> where to write the data to in the block
        if offset = 0, write the data at the start of the block
        if offset = 2561, write the data starting at this offset
*/
static int rufs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
    for (int i = 0; i < size; i++) {
    }

    inode_t target_ino;

    // Step 1: You could call get_node_by_path() to get inode from path

    int get_path_result = get_node_by_path(path, root_inode, &target_ino);

    if (get_path_result == EXIT_FAILURE) {
        return -EXIT_FAILURE;
    }

    // Step 2a: Based on size and offset, read its data blocks from disk
    if (size < 0) {
        return -1;
    }

    for (int i = 0; i < NUM_DIRECT_PTRS; i++) {
    }

    // NOTE: size will always be >= 0 and <= 4096 (or 1 block), so num_blocks_copy would always be 1
    int num_blocks_copy = ceil(size / BLOCK_SIZE);
    if (num_blocks_copy == 0) {
        num_blocks_copy = 1;
    }

    int block_where_offset_is = (offset / BLOCK_SIZE);

    for (int i = 0; i < NUM_DIRECT_PTRS; i++) {
        // read block where offset is, since this is where you must start writing
        if (i == block_where_offset_is) {
            // once you get to this location, start reading and writing

            if (target_ino.direct_ptr[i] >= data_block_start) {
                // if this data block is valid, proceed with reading and writing

                // if it IS valid, read this block to buff_mem
                memset(buff_mem, 0, BUFF_MEM_SIZE);
                if (bio_read(target_ino.direct_ptr[i], buff_mem) < 0) {
                    return -EXIT_FAILURE;
                }

                offset %= BLOCK_SIZE;
                // copy the data from the buffer to this data block and position of the offset it belongs
                memcpy(buff_mem + offset, buffer, ((size % BLOCK_SIZE == 0) ? BLOCK_SIZE : size % BLOCK_SIZE));

                // write this data block back to disk
                if (bio_write(target_ino.direct_ptr[i], buff_mem) < 0) {
                    return -EXIT_FAILURE;
                }

                // update target_ino stats to reflect data changes
                target_ino.size += size;
                target_ino.vstat.st_atime = time(NULL);
                target_ino.vstat.st_mtime = time(NULL);
                target_ino.vstat.st_size += size;

                // update the target inode on disk:
                int target_inode_block_num = inode_table_index + (target_ino.ino / inodes_in_block);
                int offset_in_block = (target_ino.ino % inodes_in_block);

                // read the inode block where the target_ino.ino number exists:
                memset(buff_mem, 0, BUFF_MEM_SIZE);
                if (bio_read(target_inode_block_num, buff_mem) < 0) {
                    return -EXIT_FAILURE;
                }

                // copy the updated inode (with the updated stats) to the inode block at it's proper offset
                memcpy(buff_mem + (offset_in_block * sizeof(inode_t)), &target_ino, sizeof(inode_t));

                // write this inode block back to disk
                if (bio_write(target_inode_block_num, buff_mem) < 0) {
                    return -EXIT_FAILURE;
                }

                // write updated target inode back to disk
                if (writei(target_ino.ino, &target_ino) == EXIT_FAILURE) {
                    return -EXIT_FAILURE;
                }

                // return size indicating how much data was written and that operation was successful
                return size;

            } else {
                // if this data block is invalid need to allocate the data block

                // the next data block we want to write to is invalid, so create a new one:

                target_ino.direct_ptr[i] = get_avail_blkno();

                memset(buff_mem, 0, BUFF_MEM_SIZE);
                if (bio_read(target_ino.direct_ptr[i], buff_mem) < 0) {
                    return -EXIT_FAILURE;
                }

                offset %= BLOCK_SIZE;
                memcpy(buff_mem + offset, buffer, ((size % BLOCK_SIZE == 0) ? BLOCK_SIZE : size % BLOCK_SIZE));

                if (bio_write(target_ino.direct_ptr[i], buff_mem) < 0) {
                    return -EXIT_FAILURE;
                }

                // update target_ino stats to reflect data changes
                target_ino.size += size;
                target_ino.vstat.st_atime = time(NULL);
                target_ino.vstat.st_mtime = time(NULL);
                target_ino.vstat.st_size += size;

                // update the target inode on disk:
                int target_inode_block_num = inode_table_index + (target_ino.ino / inodes_in_block);
                int offset_in_block = (target_ino.ino % inodes_in_block);

                // read the inode block where the target_ino.ino number exists:
                memset(buff_mem, 0, BUFF_MEM_SIZE);
                if (bio_read(target_inode_block_num, buff_mem) < 0) {
                    return -EXIT_FAILURE;
                }

                memcpy(buff_mem + (offset_in_block * sizeof(inode_t)), &target_ino, sizeof(inode_t));

                // write this inode block back to disk
                if (bio_write(target_inode_block_num, buff_mem) < 0) {
                    return -EXIT_FAILURE;
                }

                if (writei(target_ino.ino, &target_ino) == EXIT_FAILURE) {
                    return -EXIT_FAILURE;
                }

                return size;
            }
        }
    }

    // Upon reaching this point, return -1 to indicate something went wrong as this functin should have returned earlier

    return -1;
}

static int rufs_unlink(const char *path) {
    // Step 1: Use dirname() and basename() to separate parent directory path and target file name

    // Step 2: Call get_node_by_path() to get inode of target file

    // Step 3: Clear data block bitmap of target file

    // Step 4: Clear inode bitmap and its data block

    // Step 5: Call get_node_by_path() to get inode of parent directory

    // Step 6: Call dir_remove() to remove directory entry of target file in its parent directory

    return 0;
}

static int rufs_truncate(const char *path, off_t size) {
    
    return 0;
}

static int rufs_release(const char *path, struct fuse_file_info *fi) {
    
    return 0;
}

static int rufs_flush(const char *path, struct fuse_file_info *fi) {
    
    return 0;
}

static int rufs_utimens(const char *path, const struct timespec tv[2]) {
    
    return 0;
}

static struct fuse_operations rufs_ope = {
    .init = rufs_init,
    .destroy = rufs_destroy,

    .getattr = rufs_getattr,
    .readdir = rufs_readdir,
    .opendir = rufs_opendir,
    .releasedir = rufs_releasedir,
    .mkdir = rufs_mkdir,
    .rmdir = rufs_rmdir,

    .create = rufs_create,
    .open = rufs_open,
    .read = rufs_read,
    .write = rufs_write,
    .unlink = rufs_unlink,

    .truncate = rufs_truncate,
    .flush = rufs_flush,
    .utimens = rufs_utimens,
    .release = rufs_release};

int main(int argc, char *argv[]) {
    int fuse_stat;

    getcwd(diskfile_path, PATH_MAX);
    strcat(diskfile_path, "/DISKFILE");

    fuse_stat = fuse_main(argc, argv, &rufs_ope, NULL);

    // Add print statement to confirm RUFS active?

    return fuse_stat;
    // exit(EXIT_SUCCESS);
}
