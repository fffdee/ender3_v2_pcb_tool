/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    app_bl.c
  * @brief   APP 侧 Bootloader 联动。
  *
  * 机制:
  *   上位机对运行中的 APP 发送两种触发命令（二选一）:
  *     a) ENTER_BOOT(0x0B) 协议帧（帧格式: AA|cmd|seq:BE16|len:BE16|payload|crc:BE16）
  *     b) 文本命令 "boot"（大小写不敏感，\r\n 后缀可选；兼容上位机按钮与串口助手）
  *   本模块通过 UART1/UART3 中断接收嗅探上述命令，校验通过后:
  *     1. 回 ACK(0xA1)（帧路径）或 "OK\r\n"（文本路径）给上位机；
  *     2. 在配置区写入 enter_boot 魔数（与 Bootloader 的 bl_cfg_t 布局一致）；
  *     3. 软复位。
  *   Bootloader 启动时检测到 enter_boot 魔数后清除标志并留在升级模式，
  *   从而完成 APP -> Bootloader 的跳转。
  ******************************************************************************
  */
/* USER CODE END Header */
#include "app_bl.h"
#include "usart.h"
#include <string.h>

/* USER CODE BEGIN 0 */

/* ─── 与 Bootloader bl_config.h / bl_flash.c 保持一致的常量 ─── */
#define BL_CFG_PAGE0        0x0807F000u
#define BL_CFG_PAGE1        0x0807F800u
#define BL_MAGIC_CFG        0x424C4346u /* BLCF */
#define BL_MAGIC_ENTER_BOOT 0x454E4252u /* ENBR */

#define BL_SOF              0xAAu
#define BL_CMD_ENTER_BOOT   0x0Bu
#define BL_RSP_ACK          0xA1u
#define BL_PROTOCOL_VER     2u

#define BL_MAX_FRAME        72u /* 8 字节头/CRC + 64 字节 payload 上限 */

/* 配置区结构：字段顺序必须与 Bootloader 的 bl_cfg_t 完全一致 */
typedef struct {
    uint32_t cfg_magic;
    uint32_t seq;
    uint8_t  boot_mode;
    uint8_t  active_part;
    uint8_t  boot_fail_cnt;
    uint8_t  reserved;
    uint32_t pending;
    uint32_t expected_size;
    uint32_t enter_boot;
} app_cfg_t;

/* ─── CRC16-CCITT：poly 0x1021, init 0xFFFF（与上位机 bl_core.py 一致） ─── */
static uint16_t crc16_ccitt(const uint8_t *data, uint32_t len)
{
    uint32_t i;
    uint16_t crc = 0xFFFFu;
    uint8_t  bit;

    for (i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (bit = 0; bit < 8u; bit++) {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u)
                                  : (uint16_t)(crc << 1);
            crc &= 0xFFFFu;
        }
    }
    return crc;
}

/* ─── 调试打印：同时输出到两路 UART，便于上位机捕获 ─── */
void app_log(const char *s)
{
    uint16_t len;

    if (s == NULL) {
        return;
    }
    len = (uint16_t)strlen(s);
    uart_tx(&huart1, (const uint8_t *)s, len);
    uart_tx(&huart3, (const uint8_t *)s, len);
}

void app_log_u32(const char *prefix, uint32_t value)
{
    char buf[24];
    uint8_t n = 0;
    uint8_t i;
    static const char hex[] = "0123456789ABCDEF";

    if (prefix != NULL) {
        while (prefix[n] != '\0') {
            buf[n] = prefix[n];
            n++;
        }
    }
    buf[n++] = '0';
    buf[n++] = 'x';
    for (i = 0; i < 8; i++) {
        buf[n++] = hex[(value >> (28u - i * 4u)) & 0xFu];
    }
    buf[n++] = '\r';
    buf[n++] = '\n';
    buf[n] = '\0';
    app_log(buf);
}

/* ─── 配置区读写（复刻 Bootloader bl_flash.c 逻辑） ─── */
static int cfg_valid(const app_cfg_t *cfg)
{
    uint32_t i;
    const uint8_t *p = (const uint8_t *)cfg;

    for (i = 0; i < sizeof(app_cfg_t); i++) {
        if (p[i] != 0xFFu) {
            return cfg->cfg_magic == BL_MAGIC_CFG;
        }
    }
    return 0;
}

static void cfg_load(app_cfg_t *cfg)
{
    const app_cfg_t *p0 = (const app_cfg_t *)BL_CFG_PAGE0;
    const app_cfg_t *p1 = (const app_cfg_t *)BL_CFG_PAGE1;
    int v0 = cfg_valid(p0);
    int v1 = cfg_valid(p1);

    if (v0 && v1) {
        *cfg = (p0->seq >= p1->seq) ? *p0 : *p1;
    } else if (v0) {
        *cfg = *p0;
    } else if (v1) {
        *cfg = *p1;
    } else {
        memset(cfg, 0, sizeof(*cfg));
        cfg->cfg_magic = BL_MAGIC_CFG;
        cfg->seq = 1;
    }
}

static int flash_erase_page(uint32_t addr)
{
    FLASH_EraseInitTypeDef erase;
    uint32_t page_err = 0;

    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = addr;
    erase.NbPages = 1;
    return (HAL_FLASHEx_Erase(&erase, &page_err) == HAL_OK) ? 0 : -1;
}

static int flash_program_buf(uint32_t addr, const uint8_t *data, uint32_t len)
{
    uint32_t i;
    uint16_t hw;

    if ((addr & 1u) != 0u) {
        return -1;
    }
    for (i = 0; i < len; i += 2u) {
        hw = data[i];
        if ((i + 1u) < len) {
            hw |= ((uint16_t)data[i + 1u]) << 8;
        } else {
            hw |= 0xFF00u;
        }
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, addr + i, hw) != HAL_OK) {
            return -1;
        }
    }
    return 0;
}

static int cfg_set_enter_boot(int on)
{
    const app_cfg_t *p0 = (const app_cfg_t *)BL_CFG_PAGE0;
    const app_cfg_t *p1 = (const app_cfg_t *)BL_CFG_PAGE1;
    app_cfg_t tmp;
    uint32_t dst;
    uint32_t old;
    int v0 = cfg_valid(p0);
    int v1 = cfg_valid(p1);

    cfg_load(&tmp);
    tmp.cfg_magic = BL_MAGIC_CFG;
    tmp.enter_boot = on ? BL_MAGIC_ENTER_BOOT : 0u;

    if (v0 && v1) {
        if (p0->seq >= p1->seq) {
            tmp.seq = p0->seq + 1u;
            dst = BL_CFG_PAGE1;
            old = BL_CFG_PAGE0;
        } else {
            tmp.seq = p1->seq + 1u;
            dst = BL_CFG_PAGE0;
            old = BL_CFG_PAGE1;
        }
    } else if (v0) {
        tmp.seq = p0->seq + 1u;
        dst = BL_CFG_PAGE1;
        old = BL_CFG_PAGE0;
    } else if (v1) {
        tmp.seq = p1->seq + 1u;
        dst = BL_CFG_PAGE0;
        old = BL_CFG_PAGE1;
    } else {
        tmp.seq = 1;
        dst = BL_CFG_PAGE0;
        old = 0;
    }

    HAL_FLASH_Unlock();
    if (flash_erase_page(dst) != 0) {
        HAL_FLASH_Lock();
        return -1;
    }
    if (flash_program_buf(dst, (const uint8_t *)&tmp, sizeof(tmp)) != 0) {
        HAL_FLASH_Lock();
        return -1;
    }
    if (old != 0u) {
        (void)flash_erase_page(old);
    }
    HAL_FLASH_Lock();
    return 0;
}

/* 置位 enter_boot 魔数后软复位（供 Shell boot 命令调用）。
 * 成功: 复位进入 Bootloader（不返回），返回 0；
 * 失败: 留在 APP，返回 -1。 */
int app_bl_enter_boot(void)
{
    app_log("UPGRADE: shell 'boot', set enter_boot flag ...\r\n");
    if (cfg_set_enter_boot(1) == 0) {
        app_log("UPGRADE: flag saved, reset to bootloader\r\n");
        HAL_Delay(50); /* 留时间发出日志 */
        NVIC_SystemReset();
        return 0; /* 不返回 */
    }
    app_log("UPGRADE: flag save FAILED, stay in APP\r\n");
    return -1;
}

/* ─── ENTER_BOOT 帧嗅探状态机 ─── */
typedef enum {
    ST_WAIT_SOF,
    ST_HDR,
    ST_PAYLOAD,
    ST_CRC
} sniff_state_t;

static sniff_state_t      s_st = ST_WAIT_SOF;
static uint8_t            s_buf[BL_MAX_FRAME];
static uint16_t           s_idx;
static uint16_t           s_payload_len;
static uint16_t           s_frame_len;
static UART_HandleTypeDef *s_rx_port;

static void send_ack(UART_HandleTypeDef *huart, uint16_t seq)
{
    uint8_t pkt[9];
    uint16_t crc;

    /* 帧: AA|A1|seq:BE16|len:BE16=1|payload(协议版本)|crc:BE16 */
    pkt[0] = BL_SOF;
    pkt[1] = BL_RSP_ACK;
    pkt[2] = (uint8_t)(seq >> 8);
    pkt[3] = (uint8_t)(seq & 0xFFu);
    pkt[4] = 0;
    pkt[5] = 1;
    pkt[6] = BL_PROTOCOL_VER;
    crc = crc16_ccitt(&pkt[1], 6u); /* cmd+seq+len+payload */
    pkt[7] = (uint8_t)(crc >> 8);
    pkt[8] = (uint8_t)(crc & 0xFFu);
    uart_tx(huart, pkt, 9);
}

static void handle_enter_boot(uint16_t seq, UART_HandleTypeDef *huart)
{
    app_log("UPGRADE: ENTER_BOOT received, reply ACK\r\n");
    send_ack(huart, seq);
    app_log("UPGRADE: set enter_boot flag ...\r\n");

    if (cfg_set_enter_boot(1) == 0) {
        app_log("UPGRADE: flag saved, reset to bootloader\r\n");
    } else {
        app_log("UPGRADE: flag save FAILED, stay in APP\r\n");
        return;
    }
    HAL_Delay(50); /* 留时间发出 ACK 与日志 */
    NVIC_SystemReset();
}

/* ─── 'boot' 文本命令识别（兼容上位机 _on_enter_boot 发送的 shell 命令） ─── */
#define BL_TXT_CMD_LEN 4u /* "boot"，大小写不敏感；\r\n 后缀可选 */
static const uint8_t s_txt_cmd[BL_TXT_CMD_LEN] = { 'b', 'o', 'o', 't' };
static uint8_t s_txt_idx;
static uint8_t s_txt_prev;   /* 前一字符（词边界判定） */
static uint8_t s_txt_ready;  /* "boot" 已匹配，等待确认后边界 */

/* 诊断开关（排查 RX 链路用）：
 * BL_DIAG_ECHO=1: 收到任意字节立即原样回显到同一串口。GUI 能看到回显 => RX 链路通。
 *   注意：Shell 接入后也会逐字回显输入，开启后可能双回显；诊断完毕请恢复 0。
 * BL_DIAG_HEX=1: 收到数据时打印 hex。会破坏帧解析与 Shell 回显，默认保持 0。 */
#define BL_DIAG_ECHO 0u
#define BL_DIAG_HEX  0u

/* 调试：收到数据时打印前 16 字节（端口+hex），用于确认 RX 链路 */
#if BL_DIAG_HEX
static uint8_t s_rx_dbg_cnt;
#define BL_RX_DBG_LIMIT 16u
#endif

static void dbg_rx_byte(uint8_t b, UART_HandleTypeDef *huart)
{
#if BL_DIAG_HEX
    static const char hex[] = "0123456789ABCDEF";
    char s[8];

    if (s_rx_dbg_cnt >= BL_RX_DBG_LIMIT) {
        return;
    }
    s[0] = (huart == &huart1) ? '1' : '3';
    s[1] = ':';
    s[2] = hex[(b >> 4) & 0xFu];
    s[3] = hex[b & 0xFu];
    s[4] = ' ';
    s[5] = '\0';
    app_log(s);
    s_rx_dbg_cnt++;
#else
    (void)b;
    (void)huart;
#endif
}

/* 诊断：收到任意字节立即原样回显。若串口助手能收到自身发送的内容 => RX 链路通。 */
static void echo_byte(uint8_t b, UART_HandleTypeDef *huart)
{
#if BL_DIAG_ECHO
    uart_tx(huart, &b, 1);
#else
    (void)b;
    (void)huart;
#endif
}

/* 字母部分大小写不敏感 */
static uint8_t text_char_eq(uint8_t got, uint8_t want)
{
    if (want >= 'a' && want <= 'z') {
        return (uint8_t)((got == want) || (got == (uint8_t)(want - 0x20u)));
    }
    return got == want;
}

static void handle_text_boot(UART_HandleTypeDef *huart)
{
    app_log("UPGRADE: 'boot' cmd received, set enter_boot flag ...\r\n");
    if (cfg_set_enter_boot(1) == 0) {
        app_log("UPGRADE: flag saved, reset to bootloader\r\n");
        uart_tx(huart, (const uint8_t *)"OK\r\n", 4u);
    } else {
        app_log("UPGRADE: flag save FAILED, stay in APP\r\n");
        uart_tx(huart, (const uint8_t *)"FAIL\r\n", 6u);
        return;
    }
    HAL_Delay(50); /* 留时间发出响应与日志 */
    NVIC_SystemReset();
}

/* 单词字符判定：字母/数字/下划线 */
static uint8_t is_word_char(uint8_t c)
{
    return (uint8_t)((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                     (c >= '0' && c <= '9') || c == '_');
}

/* "boot" 文本命令识别（兼容上位机 _on_enter_boot 发送的 shell 命令）：
 * 采用词边界匹配，避免 "reboot" / "bootloader" / "xboot" 等含 "boot" 子串的
 * 普通命令被误识别而复位到 Bootloader：
 *  - 前边界：仅当前一字符为行首/空白/控制符时才允许 "b" 启动匹配；
 *  - 后边界：匹配完 "boot" 后，下一字符若为单词字符则取消触发。 */
static void feed_text(uint8_t b, UART_HandleTypeDef *huart)
{
    dbg_rx_byte(b, huart);

    if (s_txt_ready) {
        /* "boot" 已匹配，本字节用于确认后边界 */
        s_txt_ready = 0;
        s_txt_prev = b;
        if (!is_word_char(b)) {
            handle_text_boot(huart);
        }
        return;
    }

    if (s_txt_idx > 0) {
        if (text_char_eq(b, s_txt_cmd[s_txt_idx])) {
            s_txt_idx++;
            if (s_txt_idx == BL_TXT_CMD_LEN) {
                s_txt_idx = 0;
                s_txt_ready = 1;   /* 匹配完成，待下一字节确认后边界 */
            }
        } else if ((b == 'b' || b == 'B') && !is_word_char(s_txt_prev)) {
            s_txt_idx = 1;        /* 前边界合法，重新开始匹配 */
        } else {
            s_txt_idx = 0;
        }
    } else {
        /* IDLE：仅当前一字符为词边界时才允许启动 "boot" 匹配 */
        if ((b == 'b' || b == 'B') && !is_word_char(s_txt_prev)) {
            s_txt_idx = 1;
        }
        /* 其余字节一律忽略，保持 IDLE */
    }
    s_txt_prev = b;
}

static void feed_byte(uint8_t b, UART_HandleTypeDef *huart)
{
    switch (s_st) {
    case ST_WAIT_SOF:
        if (b == BL_SOF) {
            s_buf[0] = b;
            s_idx = 1;
            s_rx_port = huart;
            s_st = ST_HDR;
        }
        break;

    case ST_HDR:
        if (s_idx < 6u) {
            s_buf[s_idx++] = b;
        }
        if (s_idx == 6u) {
            s_payload_len = ((uint16_t)s_buf[4] << 8) | s_buf[5];
            if (s_payload_len > (BL_MAX_FRAME - 8u)) {
                s_st = ST_WAIT_SOF;
            } else {
                s_frame_len = (uint16_t)(8u + s_payload_len);
                s_st = (s_payload_len > 0u) ? ST_PAYLOAD : ST_CRC;
            }
        }
        break;

    case ST_PAYLOAD:
        if (s_idx < (s_frame_len - 2u)) {
            s_buf[s_idx++] = b;
            if (s_idx == (s_frame_len - 2u)) {
                s_st = ST_CRC;
            }
        }
        break;

    case ST_CRC:
        s_buf[s_idx++] = b;
        if (s_idx == s_frame_len) {
            uint16_t rx_crc = ((uint16_t)s_buf[s_frame_len - 2u] << 8) |
                              s_buf[s_frame_len - 1u];
            uint16_t ex_crc = crc16_ccitt(&s_buf[1], s_frame_len - 3u);
            if ((rx_crc == ex_crc) && (s_buf[1] == BL_CMD_ENTER_BOOT)) {
                uint16_t seq = ((uint16_t)s_buf[2] << 8) | s_buf[3];
                handle_enter_boot(seq, s_rx_port);
            }
            s_st = ST_WAIT_SOF;
        }
        break;

    default:
        s_st = ST_WAIT_SOF;
        break;
    }
}

/* USER CODE END 0 */

void app_bl_init(void)
{
    s_st = ST_WAIT_SOF;
    s_idx = 0;
    s_txt_idx = 0;
    s_txt_prev = 0;
    s_txt_ready = 0;
#if BL_DIAG_HEX
    s_rx_dbg_cnt = 0;
#endif
    uart_rx_start();
}

/* ─── Shell 输入镜像缓冲 ───
 * app_bl_poll() 是 UART 环形缓冲的唯一消费者（嗅探 ENTER_BOOT 帧 / "boot" 命令）。
 * 为让 Shell 与嗅探共存：嗅探后将 UART1 字节镜像转发到 Shell 输入缓冲，
 * shell_io_uart.c 的 UART1 recv/available 改为从该缓冲读取，避免两个消费者抢数据。 */
#define APP_BL_SHELL_RB_SIZE 256u
static uint8_t            s_shell1_rb[APP_BL_SHELL_RB_SIZE];
static volatile uint16_t  s_shell1_head = 0;
static volatile uint16_t  s_shell1_tail = 0;

static void shell1_push(uint8_t b)
{
    uint16_t next = (uint16_t)((s_shell1_head + 1u) % APP_BL_SHELL_RB_SIZE);
    if (next != s_shell1_tail) {
        s_shell1_rb[s_shell1_head] = b;
        s_shell1_head = next;
    }
}

uint16_t app_bl_shell1_available(void)
{
    return (uint16_t)((s_shell1_head - s_shell1_tail) % APP_BL_SHELL_RB_SIZE);
}

uint16_t app_bl_shell1_pop(uint8_t *data, uint16_t maxLen)
{
    uint16_t n = 0;
    while (n < maxLen && s_shell1_tail != s_shell1_head) {
        data[n++] = s_shell1_rb[s_shell1_tail];
        s_shell1_tail = (uint16_t)((s_shell1_tail + 1u) % APP_BL_SHELL_RB_SIZE);
    }
    return n;
}

/* UART3 镜像缓冲：供 /driver/uart/uart3 设备 read 使用（drv_uart.c） */
static uint8_t            s_shell3_rb[APP_BL_SHELL_RB_SIZE];
static volatile uint16_t  s_shell3_head = 0;
static volatile uint16_t  s_shell3_tail = 0;

static void shell3_push(uint8_t b)
{
    uint16_t next = (uint16_t)((s_shell3_head + 1u) % APP_BL_SHELL_RB_SIZE);
    if (next != s_shell3_tail) {
        s_shell3_rb[s_shell3_head] = b;
        s_shell3_head = next;
    }
}

uint16_t app_bl_shell3_available(void)
{
    return (uint16_t)((s_shell3_head - s_shell3_tail) % APP_BL_SHELL_RB_SIZE);
}

uint16_t app_bl_shell3_pop(uint8_t *data, uint16_t maxLen)
{
    uint16_t n = 0;
    while (n < maxLen && s_shell3_tail != s_shell3_head) {
        data[n++] = s_shell3_rb[s_shell3_tail];
        s_shell3_tail = (uint16_t)((s_shell3_tail + 1u) % APP_BL_SHELL_RB_SIZE);
    }
    return n;
}

void app_bl_poll(void)
{
    uint8_t b;

    while (uart_rb_pop(&huart1, &b)) {
        echo_byte(b, &huart1);
        feed_text(b, &huart1);
        feed_byte(b, &huart1);
        shell1_push(b);   /* 转发给 Shell 输入缓冲 */
    }
    while (uart_rb_pop(&huart3, &b)) {
        echo_byte(b, &huart3);
        feed_text(b, &huart3);
        feed_byte(b, &huart3);
        shell3_push(b);   /* 转发给 UART3 设备镜像缓冲 */
    }
}
