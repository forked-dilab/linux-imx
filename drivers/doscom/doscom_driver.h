/*
 * TYPES
 */
typedef struct {
    uint32_t num_fifochs;           // number of fifo channels
    uint32_t num_smemch;            // number of smem channels
    uint32_t sregion_pending_events; // offset in shared memory to pending events
} doscom_init_data_t;

// 'j' 00-3F   linux/joystick.h
#define DOSCOM_IOCTL_CMD_INIT            _IO('j',  0x40)
#define DOSCOM_IOCTL_CMD_NOTIFY          _IOW('j', 0x41, int)
#define DOSCOM_IOCTL_CMD_WAIT            _IOW('j', 0x42, int)
#define DOSCOM_IOCTL_CMD_REGFIFOCH       _IOW('j', 0x44, int)
#define DOSCOM_IOCTL_CMD_GETFIFOCHMASK   _IOW('j', 0x45, int)
#define DOSCOM_IOCTL_CMD_INITFIFO        _IOW('j', 0x46, int)

#define DOSCOM_IOCTL_CMD_TRYLOCK         _IOW('j', 0x47, int)
#define DOSCOM_IOCTL_CMD_UNLOCK          _IOW('j', 0x48, int)
