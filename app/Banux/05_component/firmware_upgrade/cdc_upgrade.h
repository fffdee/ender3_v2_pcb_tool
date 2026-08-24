/**
 * @file  cdc_upgrade.h
 * @brief USB CDC firmware upgrade bridge.
 *
 * 升级模式架构：
 *   - 正常运行时，CDC 接收缓冲区完全交给 Shell 处理，不做任何拦截。
 *   - 通过 shell 命令 "upg" 调用 CDC_Upgrade_EnterMode()，进入升级模式。
 *   - 升级模式下，main loop 停止调用 Audio_Loop()/Shell，改为只调用
 *     CDC_Upgrade_Process()，直到升级完成后触发系统复位。
 *   - 这样彻底消除了 Shell 与升级协议的 CDC 缓冲区冲突。
 *
 * Usage:
 *   1. Call CDC_Upgrade_Init() once after USB init.
 *   2. In main loop: if CDC_Upgrade_InMode() → only call CDC_Upgrade_Process()
 *                    else → run normal Audio/Shell loop
 *   3. Shell "upg" command calls CDC_Upgrade_EnterMode() to switch mode.
 */
#ifndef __CDC_UPGRADE_H__
#define __CDC_UPGRADE_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Initialise the CDC upgrade bridge. Call once after USB CDC ready.
 */
void CDC_Upgrade_Init(void);

/**
 * @brief  Enter upgrade mode.
 *         Flushes the CDC RX buffer, prints a notice via CDC TX, then sets
 *         the internal mode flag. After this, CDC_Upgrade_InMode() returns 1.
 *         Called by the "upg" shell command.
 */
void CDC_Upgrade_EnterMode(void);

/**
 * @brief  Returns 1 if currently in upgrade mode (entered via EnterMode).
 *         Used by main loop to switch between normal/upgrade scheduling.
 */
int CDC_Upgrade_InMode(void);

/**
 * @brief  Auto-detect upgrade protocol SOF (0xAA) on CDC.
 *         Call from main loop before normal Audio/Shell processing.
 *         If SOF detected, enters upgrade mode automatically and injects
 *         the SOF byte into the upgrade engine.
 *         @return 1 if upgrade mode entered, 0 otherwise.
 */
int CDC_Upgrade_CheckEnter(void);

/**
 * @brief  Drive CDC upgrade state machine. Call from main loop ONLY when
 *         CDC_Upgrade_InMode() == 1.
 *         Reads all pending CDC data and feeds it to the App_Upgrade engine.
 *         If the engine reaches STATE_FINISH, triggers Reset_McuSystem().
 */
void CDC_Upgrade_Process(void);

/**
 * @brief  Returns 1 if the upgrade engine is currently writing firmware.
 */
int CDC_Upgrade_IsActive(void);

#ifdef __cplusplus
}
#endif

#endif /* __CDC_UPGRADE_H__ */
