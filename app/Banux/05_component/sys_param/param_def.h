#ifndef __PARAM_DEF_H__
#define __PARAM_DEF_H__

// Version and Magic
#define SYS_PARAM_VERSION       0x0107      /* Parameter structure version, changes require firmware update */
                                            /* 0x0106: Added storage abstraction params to SysParam_Looper_t */
                                            /* 0x0107: Added bt_max/usb_max/usb_out/usb_out_mute to SysParam_Volume_t */
#define SYS_PARAM_MAGIC         0x50415241  /* "PARA" Magic number*/

// Flash Storage Configuration
// NOTE: Both SYS_PARAM and BATT_CALIB NVM sectors MUST stay above the firmware
// binary end.  Current binary = ~0x13C000 (316 sectors).  These addresses are
// placed at 0x150000 / 0x151000 to give a comfortable 84 KB safety margin.
// If "Error: NVM overlaps firmware" fires at build time, move them further up
// (but stay below CONST_DATA_ADDR = 0x198000 / audio-data region).
#define SYS_PARAM_SECTOR_NUM    336         /* sector 336 = 0x150000, safely past firmware binary */
#define SYS_PARAM_FLASH_ADDR    (SYS_PARAM_SECTOR_NUM * 4096)  /* 0x150000 */
#define SYS_PARAM_SECTOR_SIZE   4096        /* Sector size 4KB */
#define SYS_PARAM_FLASH_TIMEOUT 100         /* Flash operation timeout (ms) */

// Header partition address and size
#define SYS_PARAM_ADDR_HEADER      (SYS_PARAM_FLASH_ADDR)
#define SYS_PARAM_HEADER_SIZE      0x1000    /* 4KB header area */

// Module Flash Address Definitions (interval 0x1000, after header)
#define SYS_PARAM_ADDR_SYSTEM      (SYS_PARAM_ADDR_HEADER + SYS_PARAM_HEADER_SIZE + 0x0000)
#define SYS_PARAM_ADDR_AUDIO       (SYS_PARAM_ADDR_HEADER + SYS_PARAM_HEADER_SIZE + 0x1000)
#define SYS_PARAM_ADDR_LOOPER      (SYS_PARAM_ADDR_HEADER + SYS_PARAM_HEADER_SIZE + 0x2000)
#define SYS_PARAM_ADDR_BLUETOOTH   (SYS_PARAM_ADDR_HEADER + SYS_PARAM_HEADER_SIZE + 0x3000)
#define SYS_PARAM_ADDR_ENCODER     (SYS_PARAM_ADDR_HEADER + SYS_PARAM_HEADER_SIZE + 0x4000)
#define SYS_PARAM_ADDR_LCD         (SYS_PARAM_ADDR_HEADER + SYS_PARAM_HEADER_SIZE + 0x5000)
#define SYS_PARAM_ADDR_USER        (SYS_PARAM_ADDR_HEADER + SYS_PARAM_HEADER_SIZE + 0x6000)
#define SYS_PARAM_ADDR_BATT_CALIB  (SYS_PARAM_ADDR_HEADER + SYS_PARAM_HEADER_SIZE + 0x7000)  /* 0x158000 (unused; battery_calib.h owns its own addr 0x151000) */

/* Compile-time guard: NVM must not overlap firmware binary.
 * Current binary end ~0x13C000.  Keep NVM above 0x140000 at minimum.
 * C89-compatible negative-size-array trick. */
typedef char _nvm_addr_check[(SYS_PARAM_FLASH_ADDR >= 0x140000u) ? 1 : -1];

// Each module parameter area size (4KB)
#define SYS_PARAM_MODULE_SIZE      0x1000    /* 4KB per module */




#endif /* __PARAM_DEF_H__ */
