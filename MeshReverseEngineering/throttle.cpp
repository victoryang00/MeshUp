#include <fcntl.h>
#include <memory.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
/* MMIO_BASE found at Bus U0, Device 0, Function 1, offset D0h. */
#define SPR_IMC_MMIO_BASE_OFFSET 0xD0
#define SPR_IMC_MMIO_BASE_MASK 0x1FFFFFFF
#define SPR_IMC_MMIO_BASE_SHIFT 23
/* MEM0_BAR found at Bus U0, Device 0, Function 1, offset D8h. */
#define SPR_IMC_MMIO_MEM0_OFFSET 0xD8
#define SPR_IMC_MMIO_MEM_STRIDE 0x4
#define SPR_IMC_MMIO_MEM_MASK 0x7FF
#define SPR_IMC_MMIO_MEM_SHIFT 12
/* MEM1_BAR found at Bus U0, Device 0, Function 1, offset DCh. */
#define SPR_IMC_MMIO_MEM1_OFFSET 0xDC
/* MEM2_BAR found at Bus U0, Device 0, Function 1, offset AE0h. */
#define SPR_IMC_MMIO_MEM2_OFFSET 0xE0
/* MEM3_BAR found at Bus U0, Device 0, Function 1, offset E4h. */
#define SPR_IMC_MMIO_MEM3_OFFSET 0xE4
/*
 * Each IMC has two channels.
 * The offset starts from 0x22800 with stride 0x4000
 */
#define SPR_IMC_MMIO_CHN_OFFSET 0x22800
#define SPR_IMC_MMIO_CHN_STRIDE 0x4000
/* IMC MMIO size*/
#define SPR_IMC_MMIO_SIZE 0x4000

#define SPR_IMC_MMIO_FREERUN_OFFSET 0x2290
#define SPR_IMC_MMIO_FREERUN_SIZE 0x4000
/*
 * I'm following the Linux kernel here but documentation tells us that
 * there are three channels out of which 2 are active.
 */
#define SPR_NUMBER_IMC_CHN 2
#define SPR_IMC_MEM_STRIDE 0x4
#define SPR_NUMBER_IMC_DEVS 4

static int servermem_getStartAddr(int socketId, int pmc_idx, void **mmap_addr) {
    off_t addr;
    off_t addr2;
    uint32_t tmp = 0x0U;
    int pagesize = sysconf(_SC_PAGE_SIZE);
    char sysfile[1000];
    uint32_t pci_bus[2] = {0x7e, 0xfe};
    // pcipath = malloc(1024 * sizeof(char));
    // memset(pcipath, '\0', 1024 * sizeof(char));
    // int sid = getBusFromSocket(socketId, &(pci_devices_daemon[UBOX]), 999, &sysfile);
    // syslog(LOG_ERR, "Sysfs file %s", sysfile);

    int ret = snprintf(sysfile, 999, "/sys/bus/pci/devices/0000:%.2x:00.1/config", pci_bus[socketId]);
    if (ret >= 0) {
        sysfile[ret] = '\0';
    } else {
        return -1;
    }
    int pcihandle = open(sysfile, O_RDONLY);
    if (pcihandle < 0) {
        return -1;
    }

    ret = pread(pcihandle, &tmp, sizeof(uint32_t), SPR_IMC_MMIO_BASE_OFFSET);
    if (ret < 0) {
        close(pcihandle);
        return -1;
    }
    if (!tmp) {
        close(pcihandle);
        return -1;
    }
    addr = ((tmp & SPR_IMC_MMIO_BASE_MASK)) << SPR_IMC_MMIO_BASE_SHIFT;
    int mem_offset = SPR_IMC_MMIO_MEM0_OFFSET + (pmc_idx / SPR_NUMBER_IMC_CHN) * SPR_IMC_MEM_STRIDE;
    ret = pread(pcihandle, &tmp, sizeof(uint32_t), mem_offset);
    if (ret < 0) {
        close(pcihandle);
        return -1;
    }
    addr2 = ((tmp & SPR_IMC_MMIO_MEM_MASK)) << SPR_IMC_MMIO_MEM_SHIFT;
    addr |= addr2;
    addr += SPR_IMC_MMIO_CHN_OFFSET + SPR_IMC_MMIO_CHN_STRIDE * (pmc_idx % SPR_NUMBER_IMC_CHN);

    close(pcihandle);
// addr = 0xfbb00000;
    // addr = 0xc8a80000;
//  addr = 0xc8b00000;
// addr = 0xc8b80000;
// addr = 0xc8c00000;
// addr = 0xfba80000;
// addr = 0xfbb80000;
addr = 0xc8a80000;
    pcihandle = open("/dev/mem", O_RDWR | O_SYNC);
    uint64_t page_mask = ~(pagesize - 1);
    void *maddr = mmap(0, SPR_IMC_MMIO_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, pcihandle, 3366592512);
    if (maddr == MAP_FAILED) {
        printf("ServerMem: MMAP of addr 0x%lx failed\n", addr & page_mask);
        close(pcihandle);
        return -1;
    }
    close(pcihandle);
    //*reg_offset = addr - (addr & page_mask);
    //*raw_offset = addr;
    // printf("%p",addr);

    *mmap_addr =(void*) ((uint64_t)maddr + addr - (addr & page_mask));
    printf("%p %ld\n",*mmap_addr,addr - (addr & page_mask));
    return 0;
}

int set_pci(void *mmap_addr, int offset, uint16_t val) {
    uint16_t *addr = (uint16_t *)((uint64_t)mmap_addr + offset);
    *addr = val;
    return 0;
}

int get_pci(void *mmap_addr, int offset, uint16_t *val) {
    uint16_t *addr = (uint16_t *)((uint64_t)mmap_addr + offset);
    *val = *addr;
    return 0;
}

int set_throttle_register(void *mmap_addr, int dimm, uint16_t val) {
    int offset = 0x2241c + dimm * 0x4;
    int i;

    // write to all 4 channels

    // first Activate throttling
    /*set_pci(bus_id, 0x10, 0x0, 0x190, (uint16_t) val);
    set_pci(bus_id, 0x10, 0x1, 0x190, (uint16_t) val);
    set_pci(bus_id, 0x10, 0x4, 0x190, (uint16_t) val);
    set_pci(bus_id, 0x10, 0x5, 0x190, (uint16_t) val);*/

    // then the Read or Write throttling
    // for (i=0; i < regs->channels; ++i) {
    for (int i =0;i<4;i++){ set_pci(mmap_addr, offset+2*i, (uint16_t)val);
    }

    return 0;
}

int get_throttle_register(void *mmap_addr, int dimm, uint16_t *val) {
    int offset = 0x2241c + dimm * 0x4;

    // read just channel 1
   for (int i =0;i<4;i++){ get_pci(mmap_addr, offset+2*i, val);
    printf("Value: 0x%x\n", *val);}

    return 0;
}
int main() {
    void *mmap_addr;
    servermem_getStartAddr(0, 1, &mmap_addr);
    printf("Got mmap addr%p\n", mmap_addr);
    uint16_t val=0;
    get_throttle_register(mmap_addr, 0, &val);
    return 0;
}