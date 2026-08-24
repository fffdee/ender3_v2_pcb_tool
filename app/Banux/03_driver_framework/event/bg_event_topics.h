/**
 * @file    bg_event_topics.h
 * @brief   事件话题 (Topic) 全局定义
 * @author  BanGO
 * @date    2026-04-01
 *
 * 所有话题 ID 在此集中定义, 驱动层发布 (Publish), 应用层订阅 (Subscribe)。
 * 话题按功能域分组, 每组预留 32 个 ID 用于扩展。
 *
 * 命名规范: EVT_<域>_<动作>
 */

#ifndef __BG_EVENT_TOPICS_H__
#define __BG_EVENT_TOPICS_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================
 * 话题 ID 枚举
 * ============================================ */
typedef enum {

    /* ---- 按钮事件 (0x0000 ~ 0x001F) ---- */
    EVT_BTN_CLICK           = 0x0000,   /* 单击 */
    EVT_BTN_DOUBLE_CLICK    = 0x0001,   /* 双击 */
    EVT_BTN_LONG_PRESS      = 0x0002,   /* 长按 (达到阈值时触发一次) */
    EVT_BTN_LONG_RELEASE    = 0x0003,   /* 长按后释放 */
    EVT_BTN_REPEAT          = 0x0004,   /* 长按重复 (持续触发) */
    EVT_BTN_RAW_DOWN        = 0x0005,   /* 原始按下 (去抖后立即触发) */
    EVT_BTN_RAW_UP          = 0x0006,   /* 原始释放 */

    /* ---- 编码器事件 (0x0020 ~ 0x003F) ---- */
    EVT_ENCODER_CW          = 0x0020,   /* 顺时针旋转 */
    EVT_ENCODER_CCW         = 0x0021,   /* 逆时针旋转 */
    EVT_ENCODER_CLICK       = 0x0022,   /* 编码器按下 */

    /* ---- 音频事件 (0x0040 ~ 0x005F) ---- */
    EVT_AUDIO_LINE_IN       = 0x0040,   /* Line-In 插入/拔出 */
    EVT_AUDIO_MIC_IN        = 0x0041,   /* MIC 插入/拔出 */
    EVT_AUDIO_GUITAR_IN     = 0x0042,   /* 吉他输入 插入/拔出 */
    EVT_AUDIO_HP_OUT        = 0x0043,   /* 耳机输出 插入/拔出 */
    EVT_AUDIO_VOLUME_CHG    = 0x0044,   /* 音量变化 */
    EVT_AUDIO_CLIP          = 0x0045,   /* 削波警告 */

    /* ---- 系统事件 (0x0060 ~ 0x007F) ---- */
    EVT_SYS_BATTERY_LOW     = 0x0060,   /* 低电量 */
    EVT_SYS_BATTERY_CHG     = 0x0061,   /* 电量变化 */
    EVT_SYS_USB_CONNECT     = 0x0062,   /* USB 连接 */
    EVT_SYS_USB_DISCONNECT  = 0x0063,   /* USB 断开 */
    EVT_SYS_BT_CONNECT      = 0x0064,   /* 蓝牙 A2DP 连接 */
    EVT_SYS_BT_DISCONNECT   = 0x0065,   /* 蓝牙 A2DP 断开 */
    EVT_SYS_BT_STREAMING    = 0x0066,   /* 蓝牙 A2DP 开始推流 */
    EVT_SYS_BT_SUSPENDED    = 0x0067,   /* 蓝牙 A2DP 暂停推流 */
    EVT_SYS_POWER_ON        = 0x0068,   /* 开机 */
    EVT_SYS_POWER_OFF       = 0x0069,   /* 关机 */
    EVT_SYS_RUN_STATE       = 0x006A,   /* 运行态变化 (OFF/BOOT/RUNNING/IDLE/SHUTDOWN) */
    EVT_SYS_SUB_STATE       = 0x006B,   /* 子系统状态变化 (位掩码) */
    EVT_SYS_IDLE_ENTER      = 0x006C,   /* 进入空闲态 (低功耗) */
    EVT_SYS_IDLE_EXIT       = 0x006D,   /* 退出空闲态 */
    EVT_SYS_TRANSFER_ENTER  = 0x006E,   /* 进入传输态 (WAV/OTA) */
    EVT_SYS_TRANSFER_EXIT   = 0x006F,   /* 退出传输态 */

    /* ---- MIDI 事件 (0x0080 ~ 0x009F) ---- */
    EVT_MIDI_NOTE_ON        = 0x0080,   /* MIDI Note On */
    EVT_MIDI_NOTE_OFF       = 0x0081,   /* MIDI Note Off */
    EVT_MIDI_CC             = 0x0082,   /* MIDI CC */
    EVT_MIDI_PITCH_BEND     = 0x0083,   /* MIDI Pitch Bend */
    EVT_MIDI_PROGRAM_CHG    = 0x0084,   /* MIDI Program Change */

    /* ---- Looper 事件 (0x00A0 ~ 0x00BF) ---- */
    EVT_LOOPER_REC_START    = 0x00A0,   /* 开始录音 */
    EVT_LOOPER_REC_STOP     = 0x00A1,   /* 停止录音 */
    EVT_LOOPER_PLAY_START   = 0x00A2,   /* 开始播放 */
    EVT_LOOPER_PLAY_STOP    = 0x00A3,   /* 停止播放 */
    EVT_LOOPER_OVERDUB      = 0x00A4,   /* 叠录 */
    EVT_LOOPER_UNDO         = 0x00A5,   /* 撤销 */

    /* ---- BLE 事件 (0x00C0 ~ 0x00DF) ---- */
    EVT_BLE_CONNECTED       = 0x00C0,   /* BLE 连接建立 */
    EVT_BLE_DISCONNECTED    = 0x00C1,   /* BLE 断开 */
    EVT_BLE_DATA_RECEIVED   = 0x00C2,   /* BLE 收到数据 (AB01 写特征) */
    EVT_BLE_CCCD_ENABLED    = 0x00C3,   /* BLE CCCD 已使能 (可通知) */

    /* ---- UART/串口事件 (0x00E0 ~ 0x00FF) ---- */
    EVT_UART_DATA_RECEIVED  = 0x00E0,   /* 串口收到数据帧 */
    EVT_UART_TX_COMPLETE    = 0x00E1,   /* 串口发送完成 */
    EVT_UART_ERROR          = 0x00E2,   /* 串口错误 (溢出/帧错误) */

    /* ---- Shell/命令行事件 (0x0100 ~ 0x011F) ---- */
    EVT_SHELL_CMD_RECEIVED  = 0x0100,   /* Shell 收到完整命令 */
    EVT_SHELL_IO_SWITCH     = 0x0101,   /* Shell IO 通道切换 */

    /* ---- 用户自定义事件 (0x0200 ~ 0x02FF) ---- */
    EVT_USER_BASE           = 0x0200,   /* 应用层自定义事件起始 */

    /* ---- 最大值 ---- */
    EVT_TOPIC_MAX           = 0x02FF

} BG_EventTopic_t;

/* ============================================
 * 事件数据结构
 * ============================================ */

/**
 * @brief 按钮事件数据 (伴随 EVT_BTN_xxx 发布)
 */
typedef struct {
    uint8_t  btn_id;        /* 按钮 ID (由驱动定义) */
    uint16_t duration_ms;   /* 按下持续时间 (ms) */
} BG_EventBtnData_t;

/**
 * @brief 编码器事件数据 (伴随 EVT_ENCODER_xxx 发布)
 */
typedef struct {
    uint8_t  encoder_id;    /* 编码器 ID */
    int8_t   delta;         /* 旋转增量 (+1/-1) */
} BG_EventEncoderData_t;

/**
 * @brief 音频检测事件数据 (伴随 EVT_AUDIO_xxx 发布)
 */
typedef struct {
    uint8_t  port_id;       /* 端口 ID */
    uint8_t  connected;     /* 1=插入, 0=拔出 */
} BG_EventAudioDetData_t;

/**
 * @brief 电池事件数据 (伴随 EVT_SYS_BATTERY_xxx 发布)
 */
typedef struct {
    uint8_t  level;         /* 电量百分比 (0-100) */
    uint8_t  charging;      /* 1=充电中, 0=未充电 */
} BG_EventBatteryData_t;

/**
 * @brief BLE 数据接收事件 (伴随 EVT_BLE_DATA_RECEIVED 发布)
 */
typedef struct {
    const uint8_t *data;    /* 数据指针 (仅在回调期间有效!) */
    uint16_t       len;     /* 数据长度 */
} BG_EventBleRxData_t;

/**
 * @brief UART 数据接收事件 (伴随 EVT_UART_DATA_RECEIVED 发布)
 */
typedef struct {
    uint8_t        port_id; /* UART 端口号 (0=UART0, 1=UART1) */
    const uint8_t *data;    /* 数据指针 (仅在回调期间有效!) */
    uint16_t       len;     /* 数据长度 */
} BG_EventUartRxData_t;

/**
 * @brief 蓝牙 A2DP 状态事件 (伴随 EVT_SYS_BT_xxx 发布)
 */
typedef struct {
    uint8_t  addr[6];       /* 远端蓝牙地址 */
} BG_EventBtData_t;

/**
 * @brief 系统运行态变化事件 (伴随 EVT_SYS_RUN_STATE 发布)
 */
typedef struct {
    uint8_t old_state;      /* 旧运行态 (SysRunState_t) */
    uint8_t new_state;      /* 新运行态 (SysRunState_t) */
} BG_EventSysRunState_t;

/**
 * @brief 系统子系统状态变化事件 (伴随 EVT_SYS_SUB_STATE 发布)
 */
typedef struct {
    uint16_t changed_bits;  /* 变化的位掩码 */
    uint16_t new_state;     /* 变化后的子系统状态 */
} BG_EventSysSubState_t;

/**
 * @brief MIDI 事件数据 (伴随 EVT_MIDI_xxx 发布)
 */
typedef struct {
    uint8_t  channel;       /* MIDI 通道 (0-15) */
    uint8_t  param1;        /* Note/CC number / Program number */
    uint8_t  param2;        /* Velocity / CC value */
    uint16_t bend;          /* Pitch Bend 14-bit value (仅 PITCH_BEND 事件) */
} BG_EventMidiData_t;

/**
 * @brief Shell 命令事件 (伴随 EVT_SHELL_CMD_RECEIVED 发布)
 */
typedef struct {
    const char *cmd;        /* 命令字符串 (仅在回调期间有效!) */
    uint8_t     io_source;  /* 命令来源 (0=UART, 1=BLE, 2=USB_CDC) */
} BG_EventShellCmdData_t;

#ifdef __cplusplus
}
#endif

#endif /* __BG_EVENT_TOPICS_H__ */
