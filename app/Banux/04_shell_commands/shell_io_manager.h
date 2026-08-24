/**
 *****************************************************************************
 * @file     shell_io_manager.h
 * @author   BG Card Team
 * @version  V1.0.0
 * @date     16-December-2025
 * @brief    Shell IO Manager - Automatically switch CDC/BLE interfaces and provide access protection
 *****************************************************************************
 */

#ifndef __SHELL_IO_MANAGER_H__
#define __SHELL_IO_MANAGER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "bg_shell.h"



/*******************************************************************************
 * Configuration Definitions
 ******************************************************************************/
#define SHELL_IO_TIMEOUT_MS     3000    /* IO interface timeout (ms), can switch after timeout */
#define SHELL_IO_LOCK_TIMEOUT   5000    /* Lock timeout (ms), prevents deadlock */

/*******************************************************************************
 * IO Interface Type Enumeration
 ******************************************************************************/
typedef enum {
    SHELL_IO_NONE = 0,      /* No active interface */
    SHELL_IO_CDC,           /* USB CDC interface */
    SHELL_IO_BLE            /* BLE SPP interface */
} ShellIOType_t;

/*******************************************************************************
 * IO Manager State Enumeration
 ******************************************************************************/
typedef enum {
    SHELL_IO_STATE_IDLE = 0,    /* Idle, can accept data from any interface */
    SHELL_IO_STATE_ACTIVE,      /* Active, communicating with an interface */
    SHELL_IO_STATE_LOCKED       /* Locked, switching forbidden (processing command) */
} ShellIOState_t;

/*******************************************************************************
 * IO Manager Structure
 ******************************************************************************/
typedef struct {
    ShellIOType_t   active_io;          /* Currently active IO interface */
    ShellIOState_t  state;              /* Manager state */
    uint32_t        last_activity_tick; /* Last activity time */
    uint32_t        lock_tick;          /* Lock start time */
    uint8_t         cdc_pending;        /* CDC has pending data */
    uint8_t         ble_pending;        /* BLE has pending data */
} ShellIOManager_t;

/*******************************************************************************
 * API Function Declarations
 ******************************************************************************/

/**
 * @brief  Initialize IO manager
 * @note   Call after Shell_Init
 */
void ShellIOManager_Init(void);

/**
 * @brief  IO manager process function
 * @note   Call in main loop, replaces direct Shell_Process
 *         Automatically detects active interface and processes data
 */
void ShellIOManager_Process(void);

/**
 * @brief  Get current active IO type
 * @return Current active IO interface type
 */
ShellIOType_t ShellIOManager_GetActiveIO(void);

/**
 * @brief  Get current state
 * @return Manager state
 */
ShellIOState_t ShellIOManager_GetState(void);

/**
 * @brief  Try to lock IO interface (call when starting command processing)
 * @param  io_type IO type to request lock
 * @return 1=lock successful, 0=lock failed (other interface in use)
 */
uint8_t ShellIOManager_TryLock(ShellIOType_t io_type);

/**
 * @brief  Unlock IO interface (call when command processing finished)
 */
void ShellIOManager_Unlock(void);

/**
 * @brief  Force switch to specified IO interface
 * @param  io_type Target IO type
 * @return 1=success, 0=failed (currently locked)
 */
uint8_t ShellIOManager_SwitchIO(ShellIOType_t io_type);

/**
 * @brief  Update activity timestamp (call when data received)
 * @param  io_type IO type that received data
 */
void ShellIOManager_UpdateActivity(ShellIOType_t io_type);

/**
 * @brief  Get IO type name string
 * @param  io_type IO type
 * @return Name string
 */
const char* ShellIOManager_GetIOName(ShellIOType_t io_type);

/**
 * @brief  查询 CDC 或 BLE 是否有待处理的输入数据
 * @return 1=有数据（活跃），0=无数据（静默）
 * @note   供低功耗管理器判断通信活动状态
 */
uint8_t ShellIOManager_HasIncomingData(void);

#ifdef __cplusplus
}
#endif

#endif /* __SHELL_IO_MANAGER_H__ */
