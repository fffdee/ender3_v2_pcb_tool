/**
 * sys_state.h - 系统状态管控模块
 *
 * 核心职责：
 *   1. 系统运行态管理 (OFF → BOOT → RUNNING → IDLE → SHUTDOWN)
 *   2. 子系统状态追踪 (BT, USB, ADC, BLE 等)
 *   3. 开关机回调注册与调度
 *   4. 开机 IO 电平管理
 *   5. 低功耗模式集成
 *
 * 运行态状态机：
 *
 *   OFF ──[按键/上电]──> BOOT ──[初始化完成]──> RUNNING
 *                                              │    ^
 *                                    [空闲超时]─┘    │[活动恢复]
 *                                              v    │
 *                                             IDLE
 *                                              │
 *                                    [关机命令]─┘
 *                                              v
 *                                          SHUTDOWN ──[完成]──> OFF
 *
 * 状态变化通知：通过 BG_Event 发布-订阅系统
 *   - 运行态变化 → EVT_SYS_RUN_STATE    (携带 BG_EventSysRunState_t)
 *   - 子系统变化 → EVT_SYS_SUB_STATE    (携带 BG_EventSysSubState_t)
 *   - 开机完成   → EVT_SYS_POWER_ON
 *   - 关机开始   → EVT_SYS_POWER_OFF
 *   - 进入空闲   → EVT_SYS_IDLE_ENTER
 *   - 退出空闲   → EVT_SYS_IDLE_EXIT
 *   - 进入传输   → EVT_SYS_TRANSFER_ENTER
 *   - 退出传输   → EVT_SYS_TRANSFER_EXIT
 *
 * 订阅示例 (任意 .c 文件，文件作用域):
 *
 *   #include "bg_event.h"
 *   #include "sys_state.h"
 *
 *   static void on_run_state(BG_EventTopic_t t, const void *d, uint8_t s) {
 *       const BG_EventSysRunState_t *evt = (const BG_EventSysRunState_t *)d;
 *       DBG("RunState: %d -> %d\n", evt->old_state, evt->new_state);
 *   }
 *   BG_EVT_SUB(EVT_SYS_RUN_STATE, on_run_state);
 *
 *   static void on_sub_state(BG_EventTopic_t t, const void *d, uint8_t s) {
 *       const BG_EventSysSubState_t *evt = (const BG_EventSysSubState_t *)d;
 *       if (evt->changed_bits & SYS_SUB_BT_CONNECTED)
 *           DBG("BT %s\n", (evt->new_state & SYS_SUB_BT_CONNECTED) ? "connected" : "disconnected");
 *   }
 *   BG_EVT_SUB(EVT_SYS_SUB_STATE, on_sub_state);
 */

#ifndef __SYS_STATE_H__
#define __SYS_STATE_H__

#include <stdint.h>
#include <stdbool.h>

/* =========================================================================
 * 系统运行态
 * ========================================================================= */
typedef enum {
    SYS_RUN_OFF      = 0,  /* 关机态：系统未上电或已完全关闭 */
    SYS_RUN_BOOT     = 1,  /* 启动态：正在执行开机初始化序列 */
    SYS_RUN_RUNNING  = 2,  /* 运行态：系统正常工作 */
    SYS_RUN_IDLE     = 3,  /* 空闲态：无活动超时后进入低功耗 */
    SYS_RUN_SHUTDOWN = 4,  /* 关机态：正在执行关机序列 */
} SysRunState_t;

/* =========================================================================
 * 子系统状态位掩码
 * ========================================================================= */
#define SYS_SUB_BT_CONNECTED   0x0001U  /* 蓝牙 A2DP 已连接 */
#define SYS_SUB_BT_STREAMING   0x0002U  /* 蓝牙 A2DP 推流中 */
#define SYS_SUB_BLE_CONNECTED  0x0004U  /* BLE 已连接 */
#define SYS_SUB_USB_CONNECTED  0x0008U  /* USB 已连接 (设备模式) */
#define SYS_SUB_USB_AUDIO      0x0010U  /* USB 音频活跃 */
#define SYS_SUB_ADC_SIGNAL     0x0020U  /* ADC 输入有信号 */
#define SYS_SUB_CDC_COMM       0x0040U  /* CDC 串口通信中 */
#define SYS_SUB_TRANSFER       0x0080U  /* 数据传输中 (WAV/OTA) */
#define SYS_SUB_BATT_CHARGING  0x0100U  /* 电池充电中 */
#define SYS_SUB_BATT_LOW       0x0200U  /* 低电量 */

/* =========================================================================
 * 开关机回调 (按优先级顺序执行)
 * ========================================================================= */
#define SYS_CALLBACK_PRIO_LOW    0    /* 最后执行 (如: 日志) */
#define SYS_CALLBACK_PRIO_NORMAL 1   /* 正常优先级 (如: 保存参数) */
#define SYS_CALLBACK_PRIO_HIGH   2   /* 先执行 (如: 停止音频硬件) */
#define SYS_CALLBACK_PRIO_MAX    SYS_CALLBACK_PRIO_HIGH

/**
 * @brief 开机回调函数类型
 * @note  在 SYS_RUN_BOOT → SYS_RUN_RUNNING 转换时按优先级从高到低调用
 */
typedef void (*SysPowerOnCb_t)(void);

/**
 * @brief 关机回调函数类型
 * @note  在 SYS_RUN_RUNNING → SYS_RUN_SHUTDOWN 转换时按优先级从高到低调用
 */
typedef void (*SysPowerOffCb_t)(void);

/* =========================================================================
 * IO 电平配置 (开机初始化)
 * ========================================================================= */
typedef struct {
    uint32_t port_out_set;      /* 需要拉高的 GPIO 引脚掩码 */
    uint32_t port_out_clear;    /* 需要拉低的 GPIO 引脚掩码 */
    uint32_t port_oe_set;       /* 需要设为输出的引脚掩码 */
    uint32_t port_oe_clear;     /* 需要设为输入的引脚掩码 */
    uint32_t port_ie_clear;     /* 需要禁用输入的引脚掩码 (输出引脚) */
    uint32_t port_pu_set;       /* 需要上拉的引脚掩码 */
    uint32_t port_pd_set;       /* 需要下拉的引脚掩码 */
} SysIoConfig_t;

/* =========================================================================
 * 配置常量
 * ========================================================================= */
#define SYS_MAX_POWER_CBS   8    /* 最大开关机回调注册数 */

/* =========================================================================
 * API: 初始化与主循环
 * ========================================================================= */

/**
 * @brief 初始化系统状态管控模块
 *        设置初始状态为 SYS_RUN_OFF，清零所有子系统状态
 *        必须在硬件驱动初始化之后、其他模块 Init 之前调用
 */
void SysState_Init(void);

/**
 * @brief 周期性更新（在 hardware_check() 中每 50ms 调用）
 *        自动探测子系统状态变化，管理运行态切换
 */
void SysState_Update(void);

/* =========================================================================
 * API: 运行态管理
 * ========================================================================= */

/**
 * @brief 获取当前系统运行态
 */
SysRunState_t SysState_GetRunState(void);

/**
 * @brief 请求开机 (从 OFF → BOOT → RUNNING)
 *        执行开机回调链和 IO 初始化
 * @return 0=成功, -1=当前状态不允许开机
 */
int SysState_PowerOn(void);

/**
 * @brief 请求关机 (从 RUNNING/IDLE → SHUTDOWN → OFF)
 *        执行关机回调链，保存参数，关闭外设
 * @return 0=成功, -1=当前状态不允许关机
 */
int SysState_PowerOff(void);

/* =========================================================================
 * API: 子系统状态
 * ========================================================================= */

/**
 * @brief 获取当前子系统状态掩码
 */
uint16_t SysState_GetSubState(void);

/**
 * @brief 更新子系统状态位 (自动检测变化并触发回调)
 * @param mask   要修改的位掩码
 * @param value  新值 (1=置位, 0=清除)
 */
void SysState_SetSubBits(uint16_t mask, uint8_t value);

/**
 * @brief 查询指定子系统是否活跃
 * @param mask  子系统位掩码
 * @return 1=活跃, 0=不活跃
 */
uint8_t SysState_IsSubActive(uint16_t mask);

/* =========================================================================
 * API: 开关机回调注册
 * ========================================================================= */

/**
 * @brief 注册开机回调 (在 PowerOn 时按优先级从高到低调用)
 * @param cb    回调函数
 * @param prio  优先级 (SYS_CALLBACK_PRIO_HIGH 先执行)
 * @return 0=成功, -1=已满
 */
int SysState_RegisterPowerOnCb(SysPowerOnCb_t cb, uint8_t prio);

/**
 * @brief 注册关机回调 (在 PowerOff 时按优先级从高到低调用)
 * @param cb    回调函数
 * @param prio  优先级 (SYS_CALLBACK_PRIO_HIGH 先执行)
 * @return 0=成功, -1=已满
 */
int SysState_RegisterPowerOffCb(SysPowerOffCb_t cb, uint8_t prio);

/* =========================================================================
 * API: IO 电平管理
 * ========================================================================= */

/**
 * @brief 设置开机 IO 初始化配置
 * @param config  IO 配置 (GPIO 引脚、方向、电平)
 * @note  必须在 SysState_PowerOn() 之前调用
 */
void SysState_SetIoConfig(const SysIoConfig_t *config);

/**
 * @brief 执行开机 IO 初始化 (拉高/拉低/设方向/上下拉)
 *        在 PowerOn 流程中自动调用
 */
void SysState_ApplyIoConfig(void);

/* =========================================================================
 * API: 低功耗管理
 * ========================================================================= */

/**
 * @brief 请求进入空闲态 (RUNNING → IDLE)
 *        DAC 静音，跳过 DSP 处理
 */
void SysState_EnterIdle(void);

/**
 * @brief 退出空闲态 (IDLE → RUNNING)
 *        恢复 DAC 和 DSP 处理
 */
void SysState_ExitIdle(void);

/**
 * @brief 强制进入数据传输态 (RUNNING/IDLE → TRANSFER 子态)
 *        立即静音 DAC
 */
void SysState_EnterTransfer(void);

/**
 * @brief 退出数据传输态，恢复正常运行
 */
void SysState_ExitTransfer(void);

/**
 * @brief 查询当前是否处于数据传输态
 */
uint8_t SysState_IsTransferActive(void);

/**
 * @brief 查询当前是否处于空闲态
 */
uint8_t SysState_IsIdle(void);

/* =========================================================================
 * API: 兼容旧接口 (SysState_Get 映射到运行态)
 * ========================================================================= */

/** 旧版状态枚举 (向后兼容) */
typedef enum {
    SYS_STATE_IDLE     = SYS_RUN_IDLE,
    SYS_STATE_NORMAL   = SYS_RUN_RUNNING,
    SYS_STATE_TRANSFER = SYS_RUN_RUNNING,  /* TRANSFER 是 RUNNING 的子态 */
} SysState_t;

/**
 * @brief 兼容旧接口：获取系统状态
 * @note  新代码应使用 SysState_GetRunState() + SysState_IsTransferActive()
 */
SysState_t SysState_Get(void);

#endif /* __SYS_STATE_H__ */
