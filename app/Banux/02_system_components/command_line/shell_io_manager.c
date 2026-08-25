/**
 *****************************************************************************
 * @file     shell_io_manager.c
 * @author   BG Card Team
 * @version  V1.0.0
 * @date     16-December-2025
 * @brief    Shell IO Manager implementation - Automatically switch CDC/BLE interfaces and provide access protection
 *****************************************************************************
 */

#include "shell_io_manager.h"
#include "shell_io_cdc.h"
#include "shell_io_ble.h"
#include <string.h>

/*******************************************************************************
 * External dependency - System Tick acquisition
 ******************************************************************************/
/* Use FreeRTOS tick */
#include "FreeRTOS.h"
#include "task.h"
#define GET_TICK_MS()   (xTaskGetTickCount() * portTICK_PERIOD_MS)

/*******************************************************************************
 * Global variables
 ******************************************************************************/
static ShellIOManager_t g_io_manager;

/*******************************************************************************
 * Internal function declarations
 ******************************************************************************/
static uint8_t CheckCDCAvailable(void);
static uint8_t CheckBLEAvailable(void);
static void SwitchToIO(ShellIOType_t io_type);
static uint8_t IsTimeout(uint32_t start_tick, uint32_t timeout_ms);

/*******************************************************************************
 * API implementation
 ******************************************************************************/

void ShellIOManager_Init(void)
{
    memset(&g_io_manager, 0, sizeof(g_io_manager));
    g_io_manager.active_io = SHELL_IO_NONE;
    g_io_manager.state = SHELL_IO_STATE_IDLE;
    g_io_manager.last_activity_tick = GET_TICK_MS();
    g_io_manager.lock_tick = 0;
    g_io_manager.cdc_pending = 0;
    g_io_manager.ble_pending = 0;
    
    /* Initialize Shell system */
    Shell_Init();
    Shell_RegisterAllModules();
    
    /* Default to CDC interface */
    Shell_SetIO(ShellIO_CDC_Get());
    g_io_manager.active_io = SHELL_IO_CDC;
}

void ShellIOManager_Process(void)
{
    uint8_t cdc_has_data;
    uint8_t ble_has_data;
    uint32_t current_tick;
    
    current_tick = GET_TICK_MS();
    
    /* Check which interface has data */
    cdc_has_data = CheckCDCAvailable();
    ble_has_data = CheckBLEAvailable();
    
    /* Update pending flags */
    if (cdc_has_data) g_io_manager.cdc_pending = 1;
    if (ble_has_data) g_io_manager.ble_pending = 1;
    
    /* Check lock timeout (prevent deadlock) */
    if (g_io_manager.state == SHELL_IO_STATE_LOCKED)
    {
        if (IsTimeout(g_io_manager.lock_tick, SHELL_IO_LOCK_TIMEOUT))
        {
            /* Lock timeout, force unlock */
            g_io_manager.state = SHELL_IO_STATE_ACTIVE;
        }
    }
    
    /* Handle according to state */
    switch (g_io_manager.state)
    {
        case SHELL_IO_STATE_IDLE:
            /* Idle state: check which interface has data */
            if (cdc_has_data)
            {
                SwitchToIO(SHELL_IO_CDC);
                g_io_manager.state = SHELL_IO_STATE_ACTIVE;
                g_io_manager.last_activity_tick = current_tick;
            }
            else if (ble_has_data)
            {
                SwitchToIO(SHELL_IO_BLE);
                g_io_manager.state = SHELL_IO_STATE_ACTIVE;
                g_io_manager.last_activity_tick = current_tick;
            }
            break;
            
        case SHELL_IO_STATE_ACTIVE:
            /* Active state: prioritize current interface, but can switch */
            if (g_io_manager.active_io == SHELL_IO_CDC)
            {
                if (cdc_has_data)
                {
                    /* CDC has data, continue processing */
                    g_io_manager.last_activity_tick = current_tick;
                }
                else if (ble_has_data && IsTimeout(g_io_manager.last_activity_tick, SHELL_IO_TIMEOUT_MS))
                {
                    /* CDC timeout and BLE has data, switch to BLE */
                    SwitchToIO(SHELL_IO_BLE);
                    g_io_manager.last_activity_tick = current_tick;
                }
                else if (!cdc_has_data && !ble_has_data && 
                         IsTimeout(g_io_manager.last_activity_tick, SHELL_IO_TIMEOUT_MS))
                {
                    /* Both sides have no data and timeout, return to idle */
                    g_io_manager.state = SHELL_IO_STATE_IDLE;
                }
            }
            else if (g_io_manager.active_io == SHELL_IO_BLE)
            {
                if (ble_has_data)
                {
                    /* BLE has data, continue processing */
                    g_io_manager.last_activity_tick = current_tick;
                }
                else if (cdc_has_data && IsTimeout(g_io_manager.last_activity_tick, SHELL_IO_TIMEOUT_MS))
                {
                    /* BLE timeout and CDC has data, switch to CDC */
                    SwitchToIO(SHELL_IO_CDC);
                    g_io_manager.last_activity_tick = current_tick;
                }
                else if (!cdc_has_data && !ble_has_data && 
                         IsTimeout(g_io_manager.last_activity_tick, SHELL_IO_TIMEOUT_MS))
                {
                    /* Both sides have no data and timeout, return to idle */
                    g_io_manager.state = SHELL_IO_STATE_IDLE;
                }
            }
            break;
            
        case SHELL_IO_STATE_LOCKED:
            /* Locked state: only process current interface, do not switch */
            /* Update activity time to prevent forced unlock */
            if ((g_io_manager.active_io == SHELL_IO_CDC && cdc_has_data) ||
                (g_io_manager.active_io == SHELL_IO_BLE && ble_has_data))
            {
                g_io_manager.lock_tick = current_tick;
            }
            break;
    }
    
    /* Call Shell process function */
    Shell_Process();
    
    /* Clear processed pending flags */
    if (g_io_manager.active_io == SHELL_IO_CDC && !cdc_has_data)
    {
        g_io_manager.cdc_pending = 0;
    }
    if (g_io_manager.active_io == SHELL_IO_BLE && !ble_has_data)
    {
        g_io_manager.ble_pending = 0;
    }
}

ShellIOType_t ShellIOManager_GetActiveIO(void)
{
    return g_io_manager.active_io;
}

ShellIOState_t ShellIOManager_GetState(void)
{
    return g_io_manager.state;
}

uint8_t ShellIOManager_TryLock(ShellIOType_t io_type)
{
    /* If already locked by other interface, return failure */
    if (g_io_manager.state == SHELL_IO_STATE_LOCKED && 
        g_io_manager.active_io != io_type)
    {
        return 0;
    }
    
    /* Lock */
    g_io_manager.state = SHELL_IO_STATE_LOCKED;
    g_io_manager.active_io = io_type;
    g_io_manager.lock_tick = GET_TICK_MS();
    
    /* Switch to corresponding interface */
    SwitchToIO(io_type);
    
    return 1;
}

void ShellIOManager_Unlock(void)
{
    if (g_io_manager.state == SHELL_IO_STATE_LOCKED)
    {
        g_io_manager.state = SHELL_IO_STATE_ACTIVE;
        g_io_manager.last_activity_tick = GET_TICK_MS();
    }
}

uint8_t ShellIOManager_SwitchIO(ShellIOType_t io_type)
{
    /* Switching not allowed in locked state */
    if (g_io_manager.state == SHELL_IO_STATE_LOCKED)
    {
        return 0;
    }
    
    SwitchToIO(io_type);
    g_io_manager.state = SHELL_IO_STATE_ACTIVE;
    g_io_manager.last_activity_tick = GET_TICK_MS();
    
    return 1;
}

void ShellIOManager_UpdateActivity(ShellIOType_t io_type)
{
    if (g_io_manager.active_io == io_type)
    {
        g_io_manager.last_activity_tick = GET_TICK_MS();
        
        /* If in locked state, update lock time as well */
        if (g_io_manager.state == SHELL_IO_STATE_LOCKED)
        {
            g_io_manager.lock_tick = GET_TICK_MS();
        }
    }
}

const char* ShellIOManager_GetIOName(ShellIOType_t io_type)
{
    switch (io_type)
    {
        case SHELL_IO_CDC: return "CDC";
        case SHELL_IO_BLE: return "BLE";
        default: return "NONE";
    }
}

/*******************************************************************************
 * Internal function implementation
 ******************************************************************************/

static uint8_t CheckCDCAvailable(void)
{
    const ShellIO_t *cdc_io = ShellIO_CDC_Get();
    if (cdc_io && cdc_io->available)
    {
        return (cdc_io->available() > 0) ? 1 : 0;
    }
    return 0;
}

static uint8_t CheckBLEAvailable(void)
{
    const ShellIO_t *ble_io = ShellIO_BLE_Get();
    if (ble_io && ble_io->available)
    {
        return (ble_io->available() > 0) ? 1 : 0;
    }
    return 0;
}

static void SwitchToIO(ShellIOType_t io_type)
{
    if (g_io_manager.active_io == io_type)
    {
        return;  /* Already this interface */
    }
    
    switch (io_type)
    {
        case SHELL_IO_CDC:
            Shell_SetIO(ShellIO_CDC_Get());
            break;
        case SHELL_IO_BLE:
            Shell_SetIO(ShellIO_BLE_Get());
            break;
        default:
            return;
    }
    
    g_io_manager.active_io = io_type;
}

uint8_t ShellIOManager_HasIncomingData(void)
{
    return (CheckCDCAvailable() || CheckBLEAvailable()) ? 1U : 0U;
}

static uint8_t IsTimeout(uint32_t start_tick, uint32_t timeout_ms)
{
    uint32_t current = GET_TICK_MS();
    uint32_t elapsed;
    
    /* Handle overflow */
    if (current >= start_tick)
    {
        elapsed = current - start_tick;
    }
    else
    {
        elapsed = (0xFFFFFFFF - start_tick) + current + 1;
    }
    
    return (elapsed >= timeout_ms) ? 1 : 0;
}
