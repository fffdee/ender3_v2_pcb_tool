/**
 *****************************************************************************
 * @file     bg_shell.h
 * @author   BG Card Team
 * @version  V2.0.0
 * @date     16-December-2025
 * @brief    General command line Shell interface (decoupled from transport layer)
 *****************************************************************************
 * @attention
 *
 * This module is an independent command line system component.
 * Does not depend on specific transport method.
 * Supports USB CDC, BLE SPP, UART and other transport channels.
 * 
 * Usage:
 * 1. Shell_Init() - Initialize
 * 2. Shell_SetIO() - Set IO interface
 * 3. Shell_RegisterAllModules() - Register command modules
 * 4. Shell_Process() - Call in main loop
 * 
 *****************************************************************************
 */

#ifndef __BG_SHELL_H__
#define __BG_SHELL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "type.h"

/*******************************************************************************
 * Configuration Definitions
 ******************************************************************************/
#define SHELL_CMD_MAX_LEN       128     // Max command line length
#define SHELL_CMD_MAX_ARGS      15      // Max argument count
#define SHELL_MODULE_MAX        40     // Max module count (increased for remind + future modules)
#define SHELL_OUT_BUF_SIZE      256     // Output buffer size

/*******************************************************************************
 * IO Interface Definitions (transport layer abstraction)
 ******************************************************************************/

/**
 * @brief  IO send function type
 * @param  data: Data pointer
 * @param  len: Data length
 * @return Actual bytes sent
 */
typedef uint16_t (*ShellIO_Send_t)(uint8_t *data, uint16_t len);

/**
 * @brief  IO receive function type
 * @param  data: Receive buffer
 * @param  maxLen: Max receive length
 * @return Actual bytes received
 */
typedef uint16_t (*ShellIO_Recv_t)(uint8_t *data, uint16_t maxLen);

/**
 * @brief  IO query available data function type
 * @return Readable byte count
 */
typedef uint16_t (*ShellIO_Available_t)(void);

/**
 * @brief  Shell IO interface structure
 */
typedef struct {
    const char         *name;       // Interface name (e.g. "CDC", "BLE", "UART")
    ShellIO_Send_t      send;       // Send function
    ShellIO_Recv_t      recv;       // Receive function
    ShellIO_Available_t available;  // Query available data
} ShellIO_t;

/*******************************************************************************
 * Module Category Definitions
 ******************************************************************************/
typedef enum {
    MOD_CAT_SYSTEM = 0,     // System info
    MOD_CAT_HARDWARE,       // Hardware control
    MOD_CAT_AUDIO,          // Audio effects and control
    MOD_CAT_PARAM,          // Function parameters
    MOD_CAT_DEBUG,          // Debug
    MOD_CAT_MAX
} ModCategory_t;

/*******************************************************************************
 * Option Handler Function Type
 ******************************************************************************/
typedef int (*OptHandler_t)(int argc, char *argv[]);

/*******************************************************************************
 * Option Definition Structure
 ******************************************************************************/
typedef struct {
    const char     *opt;        // Short option (e.g. "v")
    const char     *longOpt;    // Long option (e.g. "volume"), can be NULL
    const char     *args;       // Argument description (e.g. "<0-100>")
    const char     *help;       // Help info
    OptHandler_t    handler;    // Handler function
} ShellOpt_t;

/*******************************************************************************
 * Module Definition Structure
 ******************************************************************************/
typedef struct {
    const char         *name;       // Module name (e.g. "audio")
    const char         *desc;       // Module description
    ModCategory_t       category;   // Module category
    const ShellOpt_t   *options;    // Option array (NULL terminated)
    uint8_t             optCount;   // Option count
} ShellModule_t;

/*******************************************************************************
 * API Function Declarations
 ******************************************************************************/

/**
 * @brief  Initialize Shell system
 * @return TRUE on success
 */
bool Shell_Init(void);

/**
 * @brief  Set Shell IO interface
 * @param  io: IO interface pointer
 * @return TRUE on success
 * @note   Can switch IO interface at runtime
 */
bool Shell_SetIO(const ShellIO_t *io);

/**
 * @brief  Get current IO interface name
 * @return IO interface name string
 */
const char* Shell_GetIOName(void);

/**
 * @brief  Register module
 * @param  module: Module pointer (static storage)
 * @return TRUE on success
 */
bool Shell_RegisterModule(const ShellModule_t *module);

/**
 * @brief  Shell process function (call in main loop)
 * @note   Automatically reads data from current IO interface and processes
 */
void Shell_Process(void);

/**
 * @brief  Manually input a character to Shell
 * @param  c: Character
 * @note   Can be used for external direct input (e.g. data received in interrupt)
 */
void Shell_InputChar(char c);

/**
 * @brief  Manually input data to Shell
 * @param  data: Data
 * @param  len: Length
 */
void Shell_InputData(uint8_t *data, uint16_t len);

/**
 * @brief  Output string
 */
void Shell_Print(const char *str);

/**
 * @brief  Formatted output
 */
void Shell_Printf(const char *fmt, ...);

/** * @brief  Output raw binary data (for APP communication)
 * @param  data: Binary data pointer
 * @param  len: Data length
 * @note   Bypasses string formatting, directly sends binary data
 */
void Shell_WriteRaw(const uint8_t *data, uint16_t len);

/**
 * @brief  Receive raw binary data from current IO interface
 * @param  buf: Buffer to receive data
 * @param  maxLen: Maximum bytes to receive
 * @return Number of bytes actually received (0 if no data available)
 * @note   Non-blocking, used by download protocol for packet reception
 */
uint16_t Shell_RecvRaw(uint8_t *buf, uint16_t maxLen);

/** * @brief  Output newline
 */
void Shell_NewLine(void);

/**
 * @brief  Register all predefined modules
 */
void Shell_RegisterAllModules(void);

/**
 * @brief  Register system commands (sys, info, etc)
 */
void SysCmd_Register(void);

/*******************************************************************************
 * Convenience Macros
 ******************************************************************************/

// Define option: short opt, long opt, args desc, help, handler
#define OPT(s, l, a, h, fn)     { s, l, a, h, fn }

// Option list end marker
#define OPT_END()               { NULL, NULL, NULL, NULL, NULL }

// Calculate option count (excluding end marker)
#define OPT_COUNT(opts)         ((sizeof(opts) / sizeof(opts[0])) - 1)

// Define module: name, desc, category, options array
#define DEFINE_MODULE(n, d, cat, opts) \
    static const ShellModule_t _mod_##n = { #n, d, cat, opts, OPT_COUNT(opts) }

// Register module
#define REGISTER_MODULE(n)      Shell_RegisterModule(&_mod_##n)

#ifdef __cplusplus
}
#endif

#endif /* __BG_SHELL_H__ */
