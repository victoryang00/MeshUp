
int set_pci(unsigned int bus_id, unsigned int device_id, unsigned int function_id, unsigned int offset, uint16_t val)
{
	int fd; 
    int ret;

    ioctl_query_setgetpci_t q;
	fd = open(DEV_PATH, O_RDONLY);
	if (fd < 0) {
		DBG_LOG(ERROR, "Can't open %s - Is the NVM emulator device driver installed?\n", DEV_PATH);
		return E_ERROR;
	}
    q.bus_id = bus_id;
    q.device_id = device_id;
    q.function_id = function_id;
    q.offset = offset;
    q.val = val;
    if ((ret = ioctl(fd, IOCTL_SETPCI, &q)) < 0) {
    	close(fd);
        return E_ERROR;
    }
	close(fd);
    return E_SUCCESS;
}

int get_pci(unsigned int bus_id, unsigned int device_id, unsigned int function_id, unsigned int offset, uint16_t* val)
{
	int fd; 
    int ret;

    ioctl_query_setgetpci_t q;
	fd = open(DEV_PATH, O_RDWR);
	if (fd < 0) {
		DBG_LOG(ERROR, "Can't open %s - Is the NVM emulator device driver installed?\n", DEV_PATH);
		return E_ERROR;
	}
    q.bus_id = bus_id;
    q.device_id = device_id;
    q.function_id = function_id;
    q.offset = offset;
    q.val = 0;
    if ((ret = ioctl(fd, IOCTL_GETPCI, &q)) < 0) {
    	close(fd);
        return E_ERROR;
    }
    *val = q.val;
	close(fd);
    return E_SUCCESS;
}

int intel_xeon_ex_set_throttle_register(pci_regs_t *regs, throttle_type_t throttle_type, uint16_t val)
{
    int offset;
    int i;

    switch(throttle_type) {
        case THROTTLE_DDR_ACT:
            offset = 0x190; break;
        case THROTTLE_DDR_READ:
            offset = 0x192; break;
        case THROTTLE_DDR_WRITE:
            offset = 0x194; break;
        default:
            offset = 0x190;
    }

    // write to all 4 channels

    // first Activate throttling
    /*set_pci(bus_id, 0x10, 0x0, 0x190, (uint16_t) val);
    set_pci(bus_id, 0x10, 0x1, 0x190, (uint16_t) val);
    set_pci(bus_id, 0x10, 0x4, 0x190, (uint16_t) val);
    set_pci(bus_id, 0x10, 0x5, 0x190, (uint16_t) val);*/

    // then the Read or Write throttling
    for (i=0; i < regs->channels; ++i) {
        set_pci(regs->addr[i].bus_id, regs->addr[i].dev_id, regs->addr[i].funct, offset, (uint16_t) val);
    }

    return 0;
}

int intel_xeon_ex_get_throttle_register(pci_regs_t *regs, throttle_type_t throttle_type, uint16_t* val)
{
    int offset;

    switch(throttle_type) {
        case THROTTLE_DDR_ACT:
            offset = 0x190; break;
        case THROTTLE_DDR_READ:
            offset = 0x192; break;
        case THROTTLE_DDR_WRITE:
            offset = 0x194; break;
        default:
            offset = 0x190;
    }

    // read just channel 1
    get_pci(regs->addr[0].bus_id, regs->addr[0].dev_id, regs->addr[0].funct, offset, val);
    return 0;
}
