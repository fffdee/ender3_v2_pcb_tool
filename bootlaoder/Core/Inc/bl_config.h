#ifndef BL_CONFIG_H
#define BL_CONFIG_H

#include <stdint.h>

#define BL_FLASH_BASE           0x08000000u
#define BL_SIZE                 0x00008000u
#define BL_APP_BASE             0x08008000u
#define BL_APP_SIZE             0x00070000u
#define BL_APP_END              (BL_APP_BASE + BL_APP_SIZE)

#define BL_CFG_PAGE_SIZE        0x00000800u
#define BL_CFG_PAGE0            0x0807F000u
#define BL_CFG_PAGE1            0x0807F800u

#define BL_SRAM_BASE            0x20000000u
#define BL_SRAM_SIZE            0x00010000u

#define BL_FLASH_PAGE_SIZE      2048u

#define BL_PROTOCOL_VER         2u
#define BL_SOF                  0xAAu

#define BL_CMD_SYNC             0x01u
#define BL_CMD_START            0x02u
#define BL_CMD_DATA             0x03u
#define BL_CMD_FINISH           0x04u
#define BL_CMD_JUMP             0x05u
#define BL_CMD_ERASE            0x06u
#define BL_CMD_QUERY_INFO       0x07u
#define BL_CMD_SET_PART         0x08u
#define BL_CMD_REBOOT           0x09u
#define BL_CMD_ENTER_BOOT       0x0Bu

#define BL_RSP_ACK              0xA1u
#define BL_RSP_NACK             0xA2u

#define BL_ERR_CRC_MISMATCH     0x01u
#define BL_ERR_FLASH            0x02u
#define BL_ERR_SIZE_OVERFLOW    0x03u
#define BL_ERR_STATE_INVALID    0x04u
#define BL_ERR_BAD_PARAM        0x05u

#define BL_MAGIC_CFG            0x424C4346u /* BLCF */
#define BL_MAGIC_PEND           0x50454E44u /* PEND */
#define BL_MAGIC_BGPF           0x42475046u /* BGPF */
#define BL_MAGIC_ENTER_BOOT     0x454E4252u /* ENBR: App 请求进入 Bootloader */

#define BL_UPGRADE_NAME         "UPGRADE.BIN"
#define BL_MAX_PAYLOAD          300u
#define BL_UART_RX_SIZE         1024u

/* 上电跳转 App 前的探测窗口(ms)：窗口内收到 SYNC/ENTER_BOOT 帧则留在 BL 进入升级服务 */
#define BL_BOOT_WINDOW_MS       1000u

/*
 * 调试开关：串口收到"非协议数据"(不以 0xAA 开头)时原样回发（echo）。
 * 用途：串口助手(USART1:2000000 / USART3:115200, 8N1)发任意非 AA 开头数据(如 'A' 或 hello)，
 *       若 MCU 收到会原样返回，快速确认串口链路。
 * 安全性：协议帧(0xAA 开头)不 echo，仅正常回 ACK，不影响上位机识别/升级，
 *         因此可始终保持 1。
 */
#define BL_UART_ECHO_ENABLE     1u

typedef struct {
    uint32_t cfg_magic;
    uint32_t seq;
    uint8_t  boot_mode;
    uint8_t  active_part;
    uint8_t  boot_fail_cnt;
    uint8_t  reserved;
    uint32_t pending;
    uint32_t expected_size;
    uint32_t enter_boot;   /* == BL_MAGIC_ENTER_BOOT 表示 App 请求停留在 BL */
} bl_cfg_t;

#endif /* BL_CONFIG_H */
