/**
 * @file  cdc_upgrade.c
 * @brief USB CDC firmware upgrade bridge.
 *
 * 升级模式流程：
 *   1. 正常运行时 CDC 数据完全给 Shell，本模块不做任何拦截。
 *   2. 进入升级模式有两种方式：
 *      a. Shell 命令 "upg" 调用 CDC_Upgrade_EnterMode()
 *      b. CDC_Upgrade_CheckEnter() 自动嗅探 0xAA SOF 字节
 *   3. main loop 检查 CDC_Upgrade_InMode()，进入升级分支：
 *      只调用 CDC_Upgrade_Process()，不再调用 Audio_Loop()/Shell。
 *   4. CDC_Upgrade_Process() 直接把 CDC RX 全部数据喂给 App_Upgrade 引擎。
 *   5. 升级完成 (STATE_FINISH) 后调用 Reset_McuSystem() 重启。
 */

#include "cdc_upgrade.h"
#include "app_upgrade.h"
#include "dual_partition.h"
#include "otg_device_cdc.h"
#include "reset.h"
#include "debug.h"
#include <string.h>

/* -- Module state ----------------------------------------------------------- */
static int s_initialised  = 0;
static int s_upgrade_mode = 0;   /* 1 = 已进入升级模式 */

/* -- CDC TX callback (used by App_Upgrade engine) --------------------------- */
static void cdc_tx(const uint8_t *buf, uint16_t len)
{
    OTG_DeviceCDC_Send((uint8_t *)buf, len);
}

/* -- UpgradeChannel_t callbacks --------------------------------------------- */
static uint16_t cdc_rx_read_cb(uint8_t *buf, uint16_t maxLen)
{
    return OTG_DeviceCDC_Receive(buf, maxLen);
}

static int cdc_rx_avail_cb(void)
{
    return (int)OTG_DeviceCDC_GetRxCount();
}

static UpgradeChannel_t s_cdc_ch;

/* -- Public API -------------------------------------------------------------- */

void CDC_Upgrade_Init(void)
{
    memset(&s_cdc_ch, 0, sizeof(s_cdc_ch));
    s_cdc_ch.id           = UPG_CH_CDC;
    s_cdc_ch.rx_read      = cdc_rx_read_cb;
    s_cdc_ch.tx_write     = cdc_tx;
    s_cdc_ch.rx_available = cdc_rx_avail_cb;
    s_upgrade_mode = 0;
    s_initialised  = 1;
    DBG("[CDC_UPG] init\n");
}

void CDC_Upgrade_EnterMode(void)
{
    uint8_t tmp[64];
    uint16_t n;
    const char *notice = "\r\n[UPG] Upgrade mode — send protocol packets now\r\n";

    if (!s_initialised) {
        return;
    }

    /* 清空 CDC RX 缓冲区，丢弃 shell 遗留的回车等字节 */
    do {
        n = OTG_DeviceCDC_Receive(tmp, sizeof(tmp));
    } while (n > 0);

    /* 通知上位机 */
    OTG_DeviceCDC_Send((uint8_t *)notice, (uint16_t)strlen(notice));

    s_upgrade_mode = 1;
    DBG("[CDC_UPG] upgrade mode entered\n");
}

int CDC_Upgrade_InMode(void)
{
    return s_upgrade_mode;
}

int CDC_Upgrade_CheckEnter(void)
{
    uint8_t byte;

    if (!s_initialised || s_upgrade_mode) {
        return 0;
    }

    /* 没有 CDC 数据，不处理 */
    if (OTG_DeviceCDC_GetRxCount() == 0) {
        return 0;
    }

    /* Peek at first byte WITHOUT consuming it.
     * If this is not an upgrade SOF, leave the byte for Shell or other
     * CDC consumers instead of stealing input from the normal console path. */
    if (OTG_DeviceCDC_PeekByte(&byte) == 1) {
        if (byte == UPG_SOF) {
            /* SOF matched — now consume the byte */
            OTG_DeviceCDC_Receive(&byte, 1);
            DBG("[CDC_UPG] SOF detected, entering upgrade mode\n");

            /* 进入升级模式 */
            s_upgrade_mode = 1;

            /* 把嗅探到的 SOF 字节注入升级引擎 */
            {
                uint8_t sof_pkt[1] = { UPG_SOF };
                App_Upgrade_InjectRaw(UPG_CH_CDC, sof_pkt, 1, cdc_tx);
            }
            return 1;
        }
        /* 不是 SOF (0xAA)，不消费字节，留给 Shell 或其他模块处理 */
    }

    return 0;
}

void CDC_Upgrade_Process(void)
{
    if (!s_initialised || !s_upgrade_mode) {
        return;
    }

    /* 把所有待处理的 CDC 数据直接喂给升级引擎 */
    if (OTG_DeviceCDC_GetRxCount() > 0) {
        App_Upgrade_ProcessChannel(&s_cdc_ch);
    }

    /* 升级完成后复位（App_Upgrade 已把 active_part 写入 flash） */
    if (App_Upgrade_IsFinished()) {
        DBG("[CDC_UPG] upgrade finished, rebooting...\n");
        Reset_McuSystem();
    }
}

int CDC_Upgrade_IsActive(void)
{
    return App_Upgrade_IsActive();
}
