/**
 ******************************************************************************
 * @file    bg_event_topics.h
 * @brief   事件话题 (Topic) 全局定义
 *
 * 所有话题 ID 在此集中定义, 驱动层发布 (Publish), 应用层订阅 (Subscribe)
 * 命名规范: EVT_<域>_<动作>
 ******************************************************************************
 */
#ifndef __BG_EVENT_TOPICS_H__
#define __BG_EVENT_TOPICS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/*===========================================================================
 * 话题 ID 枚举 (每组预留32个ID)
 *===========================================================================*/
typedef enum {

    /* ---- 按钮事件 (0x0000 ~ 0x001F) ---- */
    EVT_BTN_CLICK           = 0x0000,
    EVT_BTN_DOUBLE_CLICK    = 0x0001,
    EVT_BTN_LONG_PRESS      = 0x0002,
    EVT_BTN_LONG_RELEASE    = 0x0003,
    EVT_BTN_REPEAT          = 0x0004,
    EVT_BTN_RAW_DOWN        = 0x0005,
    EVT_BTN_RAW_UP          = 0x0006,

    /* ---- 编码器事件 (0x0020 ~ 0x003F) ---- */
    EVT_ENCODER_CW          = 0x0020,
    EVT_ENCODER_CCW         = 0x0021,
    EVT_ENCODER_CLICK       = 0x0022,

    /* ---- 音频事件 (0x0040 ~ 0x005F) ---- */
    EVT_AUDIO_LINE_IN       = 0x0040,
    EVT_AUDIO_MIC_IN        = 0x0041,
    EVT_AUDIO_HP_OUT        = 0x0043,
    EVT_AUDIO_VOLUME_CHG    = 0x0044,
    EVT_AUDIO_CLIP          = 0x0045,

    /* ---- 系统事件 (0x0060 ~ 0x007F) ---- */
    EVT_SYS_BATTERY_LOW     = 0x0060,
    EVT_SYS_BATTERY_CHG     = 0x0061,
    EVT_SYS_USB_CONNECT     = 0x0062,
    EVT_SYS_USB_DISCONNECT  = 0x0063,
    EVT_SYS_BT_CONNECT      = 0x0064,
    EVT_SYS_BT_DISCONNECT   = 0x0065,
    EVT_SYS_POWER_ON        = 0x0068,
    EVT_SYS_POWER_OFF       = 0x0069,
    EVT_SYS_RUN_STATE       = 0x006A,

    /* ---- UART/串口事件 (0x00E0 ~ 0x00FF) ---- */
    EVT_UART_DATA_RECEIVED  = 0x00E0,
    EVT_UART_TX_COMPLETE    = 0x00E1,

    /* ---- 用户自定义事件 (0x0200 ~ 0x02FF) ---- */
    EVT_USER_BASE           = 0x0200,
    EVT_TOPIC_MAX           = 0x02FF

} BG_EventTopic_t;

/*===========================================================================
 * 事件数据结构
 *===========================================================================*/

/** 按钮事件数据 */
typedef struct {
    uint8_t  btn_id;
    uint16_t duration_ms;
} BG_EventBtnData_t;

/** 编码器事件数据 */
typedef struct {
    uint8_t  encoder_id;
    int8_t   delta;
} BG_EventEncoderData_t;

/** 音频检测事件数据 */
typedef struct {
    uint8_t  port_id;
    uint8_t  connected;
} BG_EventAudioDetData_t;

/** 电池事件数据 */
typedef struct {
    uint8_t  level;
    uint8_t  charging;
} BG_EventBatteryData_t;

/** UART 数据接收事件 */
typedef struct {
    uint8_t        port_id;
    const uint8_t *data;
    uint16_t       len;
} BG_EventUartRxData_t;

/** 系统运行态变化事件 */
typedef struct {
    uint8_t old_state;
    uint8_t new_state;
} BG_EventSysRunState_t;

#ifdef __cplusplus
}
#endif

#endif /* __BG_EVENT_TOPICS_H__ */
