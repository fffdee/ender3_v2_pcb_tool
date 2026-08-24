#include "bl_proto.h"
#include "bl_config.h"
#include "bl_update.h"
#include "bl_flash.h"
#include <string.h>

typedef enum {
    PS_SOF = 0,
    PS_HDR,
    PS_PAYLOAD,
    PS_CRC0,
    PS_CRC1
} parser_state_t;

typedef struct {
    UART_HandleTypeDef *huart;
    parser_state_t state;
    uint8_t  cmd;
    uint16_t seq;
    uint16_t len;
    uint16_t idx;
    uint8_t  payload[BL_MAX_PAYLOAD];
    uint8_t  crc_bytes[2];
} parser_t;

static parser_t s_p1;
static parser_t s_p3;
static int      s_activity;

static uint16_t crc16_ccitt(const uint8_t *data, uint32_t len)
{
    uint32_t i;
    uint16_t crc = 0xFFFFu;
    uint8_t bit;

    for (i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (bit = 0; bit < 8u; bit++) {
            if (crc & 0x8000u) {
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            } else {
                crc = (uint16_t)(crc << 1);
            }
            crc &= 0xFFFFu;
        }
    }
    return crc;
}

static uint16_t crc16_frame(uint8_t cmd, uint16_t seq, uint16_t len, const uint8_t *payload)
{
    uint8_t hdr[5];
    uint16_t crc;
    uint32_t i;

    hdr[0] = cmd;
    hdr[1] = (uint8_t)(seq >> 8);
    hdr[2] = (uint8_t)(seq & 0xFFu);
    hdr[3] = (uint8_t)(len >> 8);
    hdr[4] = (uint8_t)(len & 0xFFu);
    crc = crc16_ccitt(hdr, 5);
    if (len > 0u && payload != NULL) {
        for (i = 0; i < len; i++) {
            uint8_t b = payload[i];
            uint8_t bit;
            crc ^= (uint16_t)b << 8;
            for (bit = 0; bit < 8u; bit++) {
                if (crc & 0x8000u) {
                    crc = (uint16_t)((crc << 1) ^ 0x1021u);
                } else {
                    crc = (uint16_t)(crc << 1);
                }
                crc &= 0xFFFFu;
            }
        }
    }
    return crc;
}

static void send_frame(UART_HandleTypeDef *huart, uint8_t rsp, uint16_t seq,
                       const uint8_t *payload, uint16_t len)
{
    uint8_t pkt[BL_MAX_PAYLOAD + 8];
    uint16_t crc;
    uint16_t n = 0;

    pkt[n++] = BL_SOF;
    pkt[n++] = rsp;
    pkt[n++] = (uint8_t)(seq >> 8);
    pkt[n++] = (uint8_t)(seq & 0xFFu);
    pkt[n++] = (uint8_t)(len >> 8);
    pkt[n++] = (uint8_t)(len & 0xFFu);
    if (len > 0u && payload != NULL) {
        memcpy(&pkt[n], payload, len);
        n += len;
    }
    crc = crc16_frame(rsp, seq, len, payload);
    pkt[n++] = (uint8_t)(crc >> 8);
    pkt[n++] = (uint8_t)(crc & 0xFFu);
    uart_tx(huart, pkt, n);
}

static void send_ack(UART_HandleTypeDef *huart, uint16_t seq,
                     const uint8_t *payload, uint16_t len)
{
    send_frame(huart, BL_RSP_ACK, seq, payload, len);
}

static void send_nack(UART_HandleTypeDef *huart, uint16_t seq, uint8_t err)
{
    send_frame(huart, BL_RSP_NACK, seq, &err, 1);
}

static void handle_query_info(UART_HandleTypeDef *huart, uint16_t seq)
{
    uint8_t info[20];
    uint32_t base = BL_APP_BASE;
    uint32_t size = BL_APP_SIZE;

    info[0] = 0;
    info[1] = 0;
    info[2] = bl_upg_boot_fail_cnt();
    info[3] = BL_PROTOCOL_VER;
    info[4] = (uint8_t)(base & 0xFFu);
    info[5] = (uint8_t)((base >> 8) & 0xFFu);
    info[6] = (uint8_t)((base >> 16) & 0xFFu);
    info[7] = (uint8_t)((base >> 24) & 0xFFu);
    info[8] = (uint8_t)(size & 0xFFu);
    info[9] = (uint8_t)((size >> 8) & 0xFFu);
    info[10] = (uint8_t)((size >> 16) & 0xFFu);
    info[11] = (uint8_t)((size >> 24) & 0xFFu);
    memset(&info[12], 0, 8);
    send_ack(huart, seq, info, 20);
}

static void handle_command(UART_HandleTypeDef *huart, uint8_t cmd, uint16_t seq,
                           const uint8_t *payload, uint16_t len)
{
    int rc;
    uint32_t u32;
    uint8_t ack1;

    switch (cmd) {
    case BL_CMD_SYNC:
    case BL_CMD_ENTER_BOOT:
        s_activity = 1;
        ack1 = BL_PROTOCOL_VER;
        send_ack(huart, seq, &ack1, 1);
        break;

    case BL_CMD_QUERY_INFO:
        handle_query_info(huart, seq);
        break;

    case BL_CMD_START:
        if (len != 4u) {
            send_nack(huart, seq, BL_ERR_BAD_PARAM);
            return;
        }
        u32 = ((uint32_t)payload[0] << 24) | ((uint32_t)payload[1] << 16) |
              ((uint32_t)payload[2] << 8) | (uint32_t)payload[3];
        rc = bl_upg_start(u32);
        if (rc == 0) {
            send_ack(huart, seq, NULL, 0);
        } else {
            send_nack(huart, seq, (uint8_t)rc);
        }
        break;

    case BL_CMD_DATA:
        if (len < 5u) {
            send_nack(huart, seq, BL_ERR_BAD_PARAM);
            return;
        }
        u32 = ((uint32_t)payload[0] << 24) | ((uint32_t)payload[1] << 16) |
              ((uint32_t)payload[2] << 8) | (uint32_t)payload[3];
        rc = bl_upg_data(u32, &payload[4], (uint32_t)(len - 4u));
        if (rc == 0) {
            send_ack(huart, seq, NULL, 0);
        } else {
            send_nack(huart, seq, (uint8_t)rc);
        }
        break;

    case BL_CMD_FINISH:
        if (len != 4u) {
            send_nack(huart, seq, BL_ERR_BAD_PARAM);
            return;
        }
        u32 = ((uint32_t)payload[0] << 24) | ((uint32_t)payload[1] << 16) |
              ((uint32_t)payload[2] << 8) | (uint32_t)payload[3];
        rc = bl_upg_finish(u32);
        if (rc == 0) {
            send_ack(huart, seq, NULL, 0);
        } else {
            send_nack(huart, seq, (uint8_t)rc);
        }
        break;

    case BL_CMD_ERASE:
        rc = bl_upg_erase();
        if (rc == 0) {
            send_ack(huart, seq, NULL, 0);
        } else {
            send_nack(huart, seq, (uint8_t)rc);
        }
        break;

    case BL_CMD_JUMP:
        if (!bl_upg_can_jump()) {
            send_nack(huart, seq, BL_ERR_STATE_INVALID);
            return;
        }
        bl_upg_on_jump_ok();
        send_ack(huart, seq, NULL, 0);
        HAL_Delay(10);
        bl_jump_to_app();
        break;

    case BL_CMD_SET_PART:
        if (len != 1u) {
            send_nack(huart, seq, BL_ERR_BAD_PARAM);
            return;
        }
        if (payload[0] == 0u) {
            send_ack(huart, seq, NULL, 0);
        } else {
            send_nack(huart, seq, BL_ERR_BAD_PARAM);
        }
        break;

    case BL_CMD_REBOOT:
        send_ack(huart, seq, NULL, 0);
        HAL_Delay(10);
        NVIC_SystemReset();
        break;

    default:
        send_nack(huart, seq, BL_ERR_BAD_PARAM);
        break;
    }
}

static void parser_reset(parser_t *p)
{
    p->state = PS_SOF;
    p->idx = 0;
    p->len = 0;
}

static void parser_feed(parser_t *p, uint8_t b)
{
    uint16_t recv_crc;
    uint16_t exp_crc;

    switch (p->state) {
    case PS_SOF:
        if (b == BL_SOF) {
            p->state = PS_HDR;
            p->idx = 0;
        }
        break;

    case PS_HDR:
        if (p->idx == 0u) {
            p->cmd = b;
        } else if (p->idx == 1u) {
            p->seq = (uint16_t)b << 8;
        } else if (p->idx == 2u) {
            p->seq |= b;
        } else if (p->idx == 3u) {
            p->len = (uint16_t)b << 8;
        } else if (p->idx == 4u) {
            p->len |= b;
            if (p->len > BL_MAX_PAYLOAD) {
                parser_reset(p);
                return;
            }
            p->idx = 0;
            if (p->len == 0u) {
                p->state = PS_CRC0;
            } else {
                p->state = PS_PAYLOAD;
            }
            return;
        }
        p->idx++;
        break;

    case PS_PAYLOAD:
        p->payload[p->idx++] = b;
        if (p->idx >= p->len) {
            p->idx = 0;
            p->state = PS_CRC0;
        }
        break;

    case PS_CRC0:
        p->crc_bytes[0] = b;
        p->state = PS_CRC1;
        break;

    case PS_CRC1:
        p->crc_bytes[1] = b;
        recv_crc = (uint16_t)((uint16_t)p->crc_bytes[0] << 8) | p->crc_bytes[1];
        exp_crc = crc16_frame(p->cmd, p->seq, p->len, p->payload);
        if (recv_crc != exp_crc) {
            send_nack(p->huart, p->seq, BL_ERR_CRC_MISMATCH);
        } else {
            handle_command(p->huart, p->cmd, p->seq, p->payload, p->len);
        }
        parser_reset(p);
        break;

    default:
        parser_reset(p);
        break;
    }
}

static void poll_port(parser_t *p)
{
    uint8_t b;
    while (uart_rb_pop(p->huart, &b)) {
#if BL_UART_ECHO_ENABLE
        /* 调试 echo：仅回发非协议数据(非 0xAA 开头)，避免干扰应答帧 */
        if (p->state == PS_SOF && b != BL_SOF) {
            uart_tx(p->huart, &b, 1);
        }
#endif
        parser_feed(p, b);
    }
}

void bl_proto_init(void)
{
    s_p1.huart = &huart1;
    s_p3.huart = &huart3;
    s_activity = 0;
    parser_reset(&s_p1);
    parser_reset(&s_p3);
    uart_rx_start();
}

void bl_proto_poll(void)
{
    poll_port(&s_p1);
    poll_port(&s_p3);
}

int bl_proto_activity(void)
{
    return s_activity;
}
