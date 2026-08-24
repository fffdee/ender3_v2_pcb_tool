/**
 * sys_state.c - 系统状态管控模块实现
 *
 * 实现运行态状态机、子系统状态追踪、开关机回调调度、IO 管理。
 * 状态变化通过 BG_Event 发布-订阅系统通知，应用层可在任意位置订阅。
 */

#include "sys_state.h"
#include "debug.h"
#include "gpio.h"

/* 低功耗模块接口 (可选依赖) */
#include "bg_low_power.h"

/* BLE 导出接口 (可选依赖) */
#include "looper_wav_ble_export.h"

/* BLE 协议接口 (可选依赖) */
#include "ble_protocol.h"

/* USB 检测接口 */
#include "otg_detect.h"

/* 电池电量接口 */
#include "battery_calib.h"

/* BT 状态接口 */
#include "bt_manager.h"

/* 发布-订阅系统 */
#include "bg_event.h"

/* BLE 连接标志 (定义在 ble_app_callback.c) */
extern uint8_t BleConnectFlag;

/* ====================== 私有类型 ====================== */

typedef struct {
    SysPowerOnCb_t cb;
    uint8_t        prio;
} PowerOnCbEntry_t;

typedef struct {
    SysPowerOffCb_t cb;
    uint8_t         prio;
} PowerOffCbEntry_t;

/* ====================== 私有状态 ====================== */

/* 系统运行态 */
static SysRunState_t g_run_state = SYS_RUN_OFF;

/* 子系统状态位掩码 */
static uint16_t g_sub_state = 0;

/* 数据传输态标志 (RUNNING 的子态) */
static uint8_t g_transfer_active = 0;

/* 开机 IO 配置 */
static SysIoConfig_t g_io_config;
static uint8_t       g_io_config_set = 0;

/* 开机回调表 */
static PowerOnCbEntry_t  g_power_on_cbs[SYS_MAX_POWER_CBS];
static uint8_t           g_power_on_cb_count = 0;

/* 关机回调表 */
static PowerOffCbEntry_t g_power_off_cbs[SYS_MAX_POWER_CBS];
static uint8_t           g_power_off_cb_count = 0;

/* ====================== 内部辅助 ====================== */

/**
 * @brief 设置运行态并通过 BG_Event 发布事件
 *
 * 发布 EVT_SYS_RUN_STATE 事件，携带 BG_EventSysRunState_t 数据。
 * 应用层可用 BG_EVT_SUB(EVT_SYS_RUN_STATE, my_handler) 订阅。
 */
static void set_run_state(SysRunState_t new_state)
{
    SysRunState_t old_state = g_run_state;

    if (new_state == old_state) return;

    DBG("[SysState] RunState: %d -> %d\n", (int)old_state, (int)new_state);
    g_run_state = new_state;

    /* 通过发布-订阅系统通知所有订阅者 */
    BG_EventSysRunState_t evt = {
        .old_state = (uint8_t)old_state,
        .new_state = (uint8_t)new_state,
    };
    BG_EVT_PUB_DATA(EVT_SYS_RUN_STATE, &evt, sizeof(evt));
}

/**
 * @brief 通过 BG_Event 发布子系统状态变化
 *
 * 发布 EVT_SYS_SUB_STATE 事件，携带 BG_EventSysSubState_t 数据。
 * 应用层可用 BG_EVT_SUB(EVT_SYS_SUB_STATE, my_handler) 订阅。
 */
static void publish_sub_state_change(uint16_t changed_bits, uint16_t new_state)
{
    BG_EventSysSubState_t evt = {
        .changed_bits = changed_bits,
        .new_state    = new_state,
    };
    BG_EVT_PUB_DATA(EVT_SYS_SUB_STATE, &evt, sizeof(evt));
}

/**
 * @brief 按优先级从高到低执行开机回调
 */
static void execute_power_on_cbs(void)
{
    int8_t prio;
    uint8_t i;
    for (prio = SYS_CALLBACK_PRIO_MAX; prio >= 0; prio--) {
        for (i = 0; i < g_power_on_cb_count; i++) {
            if (g_power_on_cbs[i].cb && g_power_on_cbs[i].prio == (uint8_t)prio) {
                DBG("[SysState] PowerOn cb (prio=%d)\n", (int)prio);
                g_power_on_cbs[i].cb();
            }
        }
    }
}

/**
 * @brief 按优先级从高到低执行关机回调
 */
static void execute_power_off_cbs(void)
{
    int8_t prio;
    uint8_t i;
    for (prio = SYS_CALLBACK_PRIO_MAX; prio >= 0; prio--) {
        for (i = 0; i < g_power_off_cb_count; i++) {
            if (g_power_off_cbs[i].cb && g_power_off_cbs[i].prio == (uint8_t)prio) {
                DBG("[SysState] PowerOff cb (prio=%d)\n", (int)prio);
                g_power_off_cbs[i].cb();
            }
        }
    }
}

/**
 * @brief 通过 BLE 上报系统状态到 App
 */
static void ble_notify_state(SysRunState_t state)
{
    uint8_t payload[2];
    if (!BleConnectFlag) return;
    payload[0] = BLE_SYSTEM_SUB_STATE;
    payload[1] = (uint8_t)state;
    BleProto_SendOnce(BLE_CMD_SYSTEM, payload, 2);
}

/* ====================== 公开 API: 初始化与主循环 ====================== */

void SysState_Init(void)
{
    g_run_state          = SYS_RUN_OFF;
    g_sub_state          = 0;
    g_transfer_active    = 0;
    g_power_on_cb_count  = 0;
    g_power_off_cb_count = 0;
    g_io_config_set      = 0;
    DBG("[SysState] Initialized (OFF)\n");
}

void SysState_Update(void)
{
    uint16_t old_sub = g_sub_state;
    uint16_t new_sub = g_sub_state;
    uint16_t changed;

    /* ---- 传输态：优先级最高，Update 不改变它 ---- */
    if (g_transfer_active) {
        new_sub |= SYS_SUB_TRANSFER;
    } else {
        new_sub &= ~SYS_SUB_TRANSFER;
    }

    /* ---- 自动探测蓝牙状态 ---- */
    {
        BT_A2DP_STATE a2dp = GetA2dpState();
        if (a2dp >= BT_A2DP_STATE_CONNECTED) {
            new_sub |= SYS_SUB_BT_CONNECTED;
        } else {
            new_sub &= ~SYS_SUB_BT_CONNECTED;
            new_sub &= ~SYS_SUB_BT_STREAMING;
        }
        if (a2dp == BT_A2DP_STATE_STREAMING) {
            new_sub |= SYS_SUB_BT_STREAMING;
        } else {
            new_sub &= ~SYS_SUB_BT_STREAMING;
        }
    }

    /* ---- 自动探测 BLE 状态 ---- */
    if (BleConnectFlag) {
        new_sub |= SYS_SUB_BLE_CONNECTED;
    } else {
        new_sub &= ~SYS_SUB_BLE_CONNECTED;
    }

    /* ---- 自动探测 USB 状态 ---- */
    if (OTG_PortDeviceIsLink()) {
        new_sub |= SYS_SUB_USB_CONNECTED;
    } else {
        new_sub &= ~SYS_SUB_USB_CONNECTED;
        new_sub &= ~SYS_SUB_USB_AUDIO;
    }

    /* ---- 自动探测电池状态 ---- */
    {
        uint8_t soc = BattCalib_GetSOC();

        if (soc < 15U) {
            new_sub |= SYS_SUB_BATT_LOW;
        } else {
            new_sub &= ~SYS_SUB_BATT_LOW;
        }

        if ((new_sub & SYS_SUB_USB_CONNECTED) && soc < 100U) {
            new_sub |= SYS_SUB_BATT_CHARGING;
        } else {
            new_sub &= ~SYS_SUB_BATT_CHARGING;
        }
    }

    /* ---- 自动探测 WAV BLE 传输 ---- */
    if (LooperWavBle_IsBusy()) {
        if (!g_transfer_active) {
            SysState_EnterTransfer();
        }
        new_sub |= SYS_SUB_TRANSFER;
    }

    /* ---- 更新子系统状态并发布事件 ---- */
    changed = old_sub ^ new_sub;
    if (changed) {
        g_sub_state = new_sub;
        DBG("[SysState] SubState changed: 0x%04X -> 0x%04X (diff=0x%04X)\n",
            (unsigned)old_sub, (unsigned)new_sub, (unsigned)changed);
        publish_sub_state_change(changed, new_sub);
    }

    /* ---- 运行态管理 (非传输态时) ---- */
    if (!g_transfer_active) {
        if (g_run_state == SYS_RUN_RUNNING && LowPower_IsLowPower()) {
            set_run_state(SYS_RUN_IDLE);
            ble_notify_state(SYS_RUN_IDLE);
        } else if (g_run_state == SYS_RUN_IDLE && !LowPower_IsLowPower()) {
            set_run_state(SYS_RUN_RUNNING);
            ble_notify_state(SYS_RUN_RUNNING);
        }
    }
}

/* ====================== 公开 API: 运行态管理 ====================== */

SysRunState_t SysState_GetRunState(void)
{
    return g_run_state;
}

int SysState_PowerOn(void)
{
    if (g_run_state != SYS_RUN_OFF) {
        DBG("[SysState] PowerOn rejected: state=%d (must be OFF)\n",
            (int)g_run_state);
        return -1;
    }

    /* OFF → BOOT */
    set_run_state(SYS_RUN_BOOT);
    DBG("[SysState] PowerOn: entering BOOT sequence\n");

    /* 执行 IO 初始化 */
    if (g_io_config_set) {
        SysState_ApplyIoConfig();
    }

    /* 执行开机回调链 (高优先级先执行) */
    execute_power_on_cbs();

    /* BOOT → RUNNING */
    set_run_state(SYS_RUN_RUNNING);
    ble_notify_state(SYS_RUN_RUNNING);
    BG_EVT_PUB(EVT_SYS_POWER_ON);
    DBG("[SysState] PowerOn complete: RUNNING\n");

    return 0;
}

int SysState_PowerOff(void)
{
    if (g_run_state != SYS_RUN_RUNNING && g_run_state != SYS_RUN_IDLE) {
        DBG("[SysState] PowerOff rejected: state=%d\n", (int)g_run_state);
        return -1;
    }

    /* RUNNING/IDLE → SHUTDOWN */
    set_run_state(SYS_RUN_SHUTDOWN);
    ble_notify_state(SYS_RUN_SHUTDOWN);
    BG_EVT_PUB(EVT_SYS_POWER_OFF);

    /* 退出传输态 (如果活跃) */
    if (g_transfer_active) {
        g_transfer_active = 0;
        LowPower_ForceClear();
    }

    /* 执行关机回调链 (高优先级先执行) */
    execute_power_off_cbs();

    /* SHUTDOWN → OFF */
    set_run_state(SYS_RUN_OFF);
    DBG("[SysState] PowerOff complete: OFF\n");

    return 0;
}

/* ====================== 公开 API: 子系统状态 ====================== */

uint16_t SysState_GetSubState(void)
{
    return g_sub_state;
}

void SysState_SetSubBits(uint16_t mask, uint8_t value)
{
    uint16_t old_sub = g_sub_state;
    uint16_t new_sub;

    if (value) {
        new_sub = old_sub | mask;
    } else {
        new_sub = old_sub & ~mask;
    }

    if (new_sub != old_sub) {
        uint16_t changed = old_sub ^ new_sub;
        g_sub_state = new_sub;
        publish_sub_state_change(changed, new_sub);
    }
}

uint8_t SysState_IsSubActive(uint16_t mask)
{
    return (g_sub_state & mask) ? 1 : 0;
}

/* ====================== 公开 API: 开关机回调注册 ====================== */

int SysState_RegisterPowerOnCb(SysPowerOnCb_t cb, uint8_t prio)
{
    if (!cb || g_power_on_cb_count >= SYS_MAX_POWER_CBS) return -1;
    g_power_on_cbs[g_power_on_cb_count].cb   = cb;
    g_power_on_cbs[g_power_on_cb_count].prio  = prio;
    g_power_on_cb_count++;
    return 0;
}

int SysState_RegisterPowerOffCb(SysPowerOffCb_t cb, uint8_t prio)
{
    if (!cb || g_power_off_cb_count >= SYS_MAX_POWER_CBS) return -1;
    g_power_off_cbs[g_power_off_cb_count].cb   = cb;
    g_power_off_cbs[g_power_off_cb_count].prio  = prio;
    g_power_off_cb_count++;
    return 0;
}

/* ====================== 公开 API: IO 电平管理 ====================== */

void SysState_SetIoConfig(const SysIoConfig_t *config)
{
    if (config) {
        g_io_config = *config;
        g_io_config_set = 1;
    }
}

void SysState_ApplyIoConfig(void)
{
    if (!g_io_config_set) return;

    /* 禁用输入使能 (输出引脚必须先禁用 IE) */
    if (g_io_config.port_ie_clear)
        GPIO_RegOneBitClear(GPIO_A_IE, g_io_config.port_ie_clear);

    /* 输入/输出方向 */
    if (g_io_config.port_oe_set)
        GPIO_RegOneBitSet(GPIO_A_OE, g_io_config.port_oe_set);
    if (g_io_config.port_oe_clear)
        GPIO_RegOneBitClear(GPIO_A_OE, g_io_config.port_oe_clear);

    /* 上下拉 */
    if (g_io_config.port_pu_set)
        GPIO_RegOneBitSet(GPIO_A_PU, g_io_config.port_pu_set);
    if (g_io_config.port_pd_set)
        GPIO_RegOneBitSet(GPIO_A_PD, g_io_config.port_pd_set);

    /* 输出电平 (最后设置，避免方向未配置时产生毛刺) */
    if (g_io_config.port_out_set)
        GPIO_RegOneBitSet(GPIO_A_OUT, g_io_config.port_out_set);
    if (g_io_config.port_out_clear)
        GPIO_RegOneBitClear(GPIO_A_OUT, g_io_config.port_out_clear);

    DBG("[SysState] IO config applied: IE_CLR=0x%08X OE=0x%08X OUT=0x%08X\n",
        (unsigned)g_io_config.port_ie_clear,
        (unsigned)g_io_config.port_oe_set,
        (unsigned)g_io_config.port_out_set);
}

/* ====================== 公开 API: 低功耗管理 ====================== */

void SysState_EnterIdle(void)
{
    if (g_run_state == SYS_RUN_RUNNING) {
        set_run_state(SYS_RUN_IDLE);
        ble_notify_state(SYS_RUN_IDLE);
        BG_EVT_PUB(EVT_SYS_IDLE_ENTER);
    }
}

void SysState_ExitIdle(void)
{
    if (g_run_state == SYS_RUN_IDLE) {
        set_run_state(SYS_RUN_RUNNING);
        ble_notify_state(SYS_RUN_RUNNING);
        BG_EVT_PUB(EVT_SYS_IDLE_EXIT);
    }
}

void SysState_EnterTransfer(void)
{
    g_transfer_active = 1;
    if (g_run_state == SYS_RUN_IDLE) {
        set_run_state(SYS_RUN_RUNNING);
    }
    LowPower_ForceEnter();
    SysState_SetSubBits(SYS_SUB_TRANSFER, 1);
    ble_notify_state(SYS_RUN_RUNNING);
    BG_EVT_PUB(EVT_SYS_TRANSFER_ENTER);
}

void SysState_ExitTransfer(void)
{
    g_transfer_active = 0;
    LowPower_ForceClear();
    SysState_SetSubBits(SYS_SUB_TRANSFER, 0);
    BG_EVT_PUB(EVT_SYS_TRANSFER_EXIT);
    /* 下次 Update 会根据 LowPower 状态决定 IDLE/RUNNING */
}

uint8_t SysState_IsTransferActive(void)
{
    return g_transfer_active;
}

uint8_t SysState_IsIdle(void)
{
    return (g_run_state == SYS_RUN_IDLE) ? 1 : 0;
}

/* ====================== 兼容旧接口 ====================== */

SysState_t SysState_Get(void)
{
    if (g_transfer_active) {
        return SYS_STATE_TRANSFER;
    }
    return (SysState_t)g_run_state;
}
