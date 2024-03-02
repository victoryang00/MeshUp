
#include <emmintrin.h>
#include <errno.h> // errno support
#include <fcntl.h> // for open()
#include <stdint.h> // standard integer types, e.g., uint32_t
#include <stdio.h> // printf, etc
#include <stdlib.h> // exit() and EXIT_FAILURE
#include <string.h> // strerror() function converts errno to a text string for printing
#include <unistd.h> // sysconf() function, sleep() function
#define __USE_GNU
#include <pthread.h>
// #include "uarch.h"

#define NUM_CHA_COUNTERS 1
#define NUM_TILE_ENABLED 24
#define EPOCHS 1
#define LOOP_NUM 64 * 1024 * 1024
#define MSRFLIENAME "/dev/cpu/0/msr"
#define _mm_clflushopt(addr) asm volatile(".byte 0x66; clflushopt %0" : "+m"(*(volatile char *)addr));

void show_counters();
void attach_core(int);
void make_core_busy(int processor, char *buf);
int max_counter_cha();

uint64_t cha_perfevtsel[NUM_CHA_COUNTERS];
long cha_counts[NUM_TILE_ENABLED][NUM_CHA_COUNTERS][2]; // 28 tiles per socket, 4 counters per tile, 2 times (before
                                                        // and after)
uint64_t counters_changes[NUM_TILE_ENABLED];
uint64_t core2cha_map[NUM_TILE_ENABLED];

void get_core2cha() {
    int fd, tile, counter, r;
    uint64_t msr_val, msr_num;

    /* Open msr fd of socket 0 */
    fd = open(MSRFLIENAME, O_RDWR);
    if (fd == -1) {
        fprintf(stderr, "ERROR %s when trying to open %s\n", strerror(errno), MSRFLIENAME);
        exit(-1);
    }

    /* Clear all CHA counters */
    for (tile = 0; tile < NUM_TILE_ENABLED; tile++) {
        for (counter = 0; counter < 4; counter++) {
            cha_counts[tile][counter][0] = 0;
            cha_counts[tile][counter][1] = 0;
        }
    }

    /* Set the PMON control resgister position*/
    cha_perfevtsel[0] = 0x00c8c7ff00000d92; // UNC_CHA_TOR_INSERTS.IA_CLFLUSHOPT
    // cha_perfevtsel[0] = 0x00c816fe00000136; // UNC_CHA_TOR_INSERTS.IA_CLFLUSHOPT
    // cha_perfevtsel[0] = 0x00000080000137; // UNC_CHA_LLC_VICTIMS.REMOTE_M
    // cha_perfevtsel[0] =    0x012a; // UNC_CHA_LLC_VICTIMS.REMOTE_M
    // cha_perfevtsel[1] = 0xc817fe00000135; // Instruction
    // cha_perfevtsel[2] = 0xc817fe00000534; // LLC Misses
    // cha_perfevtsel[3] = atoi(argv[1]);
    // gusts the first part is the
    uint64_t cha_filter0 = 0; // set bits 20,19,18 HES -- all SF lookups, no LLC lookups

    /*  Clear the Map */
    for (int i = 0; i < NUM_TILE_ENABLED; i++)
        core2cha_map[i] = 0xffff;

    /* Find the corresponding CHA for each core*/
    for (int core = 0; core < NUM_TILE_ENABLED; core++) {

        // clear the counters' array
        for (int i = 0; i < NUM_TILE_ENABLED; i++)
            counters_changes[i] = 0;

        for (int j = 0; j < EPOCHS; j++) {
            // printf("Programming CHA counters\n");
            for (tile = 0; tile < NUM_TILE_ENABLED; tile++) {

                msr_num = 0x2000 + 0x10 * tile; // box control register -- set enable bit
                msr_val = 0x1;
                r = pwrite(fd, &msr_val, sizeof(msr_val),
                           msr_num); // box control register -- set enable bit
                msr_val = 0x101;
                r = pwrite(fd, &msr_val, sizeof(msr_val), msr_num);

                msr_num = 0x2002 + 0x10 * tile; // ctl0
                msr_val = cha_perfevtsel[0];
                r = pwrite(fd, &msr_val, sizeof(msr_val), msr_num);

                // msr_num = 0x2003 + 0x10 * tile; // ctl1
                // msr_val = cha_perfevtsel[1];
                // r=pwrite(fd, &msr_val, sizeof(msr_val), msr_num);

                // msr_num = 0x2004 + 0x10 * tile; // ctl2
                // msr_val = cha_perfevtsel[2];
                // r=pwrite(fd, &msr_val, sizeof(msr_val), msr_num);

                // msr_num = 0x2005 + 0x10 * tile; // ctl3
                // msr_val = cha_perfevtsel[3];
                // r=pwrite(fd, &msr_val, sizeof(msr_val), msr_num);

                msr_num = 0x200e + 0x10 * tile; // filter0
                msr_val = cha_filter0; // core & thread
                r = pwrite(fd, &msr_val, sizeof(msr_val), msr_num);

                msr_num = 0x2000 + 0x10 * tile; // box control register -- set enable bit
                msr_val = 0x201;
                r = pwrite(fd, &msr_val, sizeof(msr_val),
                           msr_num); // box control register -- set enable bit
                msr_val = 0x0;
                r = pwrite(fd, &msr_val, sizeof(msr_val), msr_num);
            }
            msr_num = 0x2fc0; // filter0
            msr_val = 0x1; // core & thread
            r = pwrite(fd, &msr_val, sizeof(msr_val), msr_num);
            msr_num = 0x2fc0; // filter0
            msr_val = 0x101; // core & thread
            r = pwrite(fd, &msr_val, sizeof(msr_val), msr_num);
            msr_num = 0x2fce; // filter0
            msr_val = 0x28140c; // core & thread
            r = pwrite(fd, &msr_val, sizeof(msr_val), msr_num);
            msr_num = 0x2fc2; // read power
            msr_val = 0x1; // core & thread
            r = pwrite(fd, &msr_val, sizeof(msr_val), msr_num);
            msr_num = 0x2fc3; // filter0
            msr_val = 0xb; // core & thread
            r = pwrite(fd, &msr_val, sizeof(msr_val), msr_num);
            msr_num = 0x2fc4; // filter0
            msr_val = 0xc; // core & thread
            r = pwrite(fd, &msr_val, sizeof(msr_val), msr_num);
            msr_num = 0x2fc5; // filter0
            msr_val = 0xd; // core & thread
            r = pwrite(fd, &msr_val, sizeof(msr_val), msr_num);
            msr_num = 0x2fc0; // filter0
            msr_val = 0x201; // core & thread
            r = pwrite(fd, &msr_val, sizeof(msr_val), msr_num);
            msr_num = 0x2fc0; // filter0
            msr_val = 0x0; // core & thread
            r = pwrite(fd, &msr_val, sizeof(msr_val), msr_num);
            char *buf = (char *)malloc(LOOP_NUM);
            buf = buf + 64 - (((long)buf) % 64);
            //  Counters before core busy
            for (tile = 0; tile < NUM_TILE_ENABLED; tile++) {
                for (counter = 0; counter < NUM_CHA_COUNTERS; counter++) {
                    msr_num = 0x2000 + 0x10 * tile + 0x8 + counter;
                    pread(fd, &msr_val, sizeof(msr_val), msr_num);
                    cha_counts[tile][counter][0] = msr_val;
                }
            }
            make_core_busy(core, buf);

            //  Counters after core busy
            for (tile = 0; tile < NUM_TILE_ENABLED; tile++) {
                for (counter = 0; counter < NUM_CHA_COUNTERS; counter++) {
                    msr_num = 0x2000 + 0x10 * tile + 0x8 + counter;
                    pread(fd, &msr_val, sizeof(msr_val), msr_num);
                    cha_counts[tile][counter][1] = msr_val;
                    // printf("tile %llx counter %d: %ld\n", msr_num, counter, msr_val);
                }
            }

            // Record the changes of event "LCORE_PMA GV"
            for (tile = 0; tile < NUM_TILE_ENABLED; tile++)
                counters_changes[tile] += (cha_counts[tile][0][1] - cha_counts[tile][0][0]);
        }
        // Map the cha with largest counters to this core
        core2cha_map[core] = max_counter_cha();
        // show_counters();
    }

    /* Output the results */
    for (int i = 0; i < NUM_TILE_ENABLED; i++)
        printf("%ld\n", core2cha_map[i]);
}

void show_counters() {
    puts("----Original----");
    puts("tile\t counter0 counter1 counter2 counter3");
    for (int tile = 0; tile < NUM_TILE_ENABLED; tile++) {
        printf("%d:\t", tile);
        for (int counter = 0; counter < NUM_CHA_COUNTERS; counter++)
            printf("%ld\t", cha_counts[tile][counter][0]);
        putchar('\n');
    }
    puts("----------------------");

    puts("----Final----");
    puts("tile\t counter0 counter1 counter2 counter3");
    for (int tile = 0; tile < NUM_TILE_ENABLED; tile++) {
        printf("%d:\t", 0x2000 + 0x10 * tile);
        for (int counter = 0; counter < NUM_CHA_COUNTERS; counter++)
            printf("%ld\t", cha_counts[tile][counter][1]);
        putchar('\n');
    }

    puts("----------------------");
    puts("----Diff----");
    puts("tile\t counter0 counter1 counter2 counter3");
    for (int tile = 0; tile < NUM_TILE_ENABLED; tile++) {
        printf("%d:\t", 0x2000 + 0x10 * tile);
        for (int counter = 0; counter < NUM_CHA_COUNTERS; counter++)
            printf("%15ld\t", cha_counts[tile][counter][1] - cha_counts[tile][counter][0]);
        putchar('\n');
    }
}

void attach_core(int cpu) {
    cpu_set_t mask;
    cpu_set_t get;
    int num = 24;
    CPU_ZERO(&mask);
    CPU_SET(cpu, &mask);
    if (sched_setaffinity(0, sizeof(mask), &mask) == -1) {
        printf("warning: could not set CPU affinity, continuing...\n");
    }

    CPU_ZERO(&get);
    if (sched_getaffinity(0, sizeof(get), &get) == -1) {
        printf("warning: cound not get thread affinity, continuing...\n");
    }
    // for(int i=0; i< num; i++){
    //     if(CPU_ISSET(i, &get))
    //         printf("this thread is running on  processor : %d\n",i);
    // }
}

void make_core_busy(int processor, char *buf) {
    attach_core(processor);
    // char *start_addr = buf;
    // char *end_addr = buf + LOOP_NUM;
    // long stride = 64;
    // asm volatile("mov %[start_addr], %%r8 \n"
    //              "movq %[start_addr], %%xmm0 \n"
    //              "LOOP_CACHEPROBE: \n"
    //              "vmovdqa64 %%zmm0, 0x0(%%r8) \n"
    //              "clflush (%%r8) \n"
    //              "vmovdqa64 %%zmm0, 0x40(%%r8) \n"
    //              "clflush 0x40(%%r8) \n"
    //              "add %[stride], %%r8 \n"
    //              "cmp %[end_addr], %%r8 \n"
    //              "jl LOOP_CACHEPROBE \n"
    //              "mfence \n" ::[start_addr] "r"(start_addr),
    //              [end_addr] "r"(end_addr), [stride] "r"(stride)
    //              : "%r8");
    for (int i = 0; i < LOOP_NUM; i++) {
        _mm_clflushopt(buf);
        buf += 64;
    }
    return;
}

int max_counter_cha() {
    uint64_t m = 0;
    uint64_t index = 0;
    for (int i = 0; i < NUM_TILE_ENABLED; i++)
        if (m <= counters_changes[i]) {
            m = counters_changes[i];
            index = i;
        }
    return index;
}
