/**
 *****************************************************************************
 * @file     bg_shell.c
 * @author   BG Card Team
 * @version  V2.0.0
 * @date     16-December-2025
 * @brief    Universal shell command implementation (with input/output console support)
 *****************************************************************************
 */

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "bg_shell.h"
#include "debug.h"
#include "vfs.h"        /* VFS 路径补全 */
#include "banux_component.h"
#include "command_parser.h"

BANUX_COMPONENT_DEFINE(g_banux_component_shell,
                       "shell", "2.1.0", BANUX_COMPONENT_SYSTEM, 1,
                       "interactive command line system");

/*******************************************************************************
 * Static variables
 ******************************************************************************/
static const ShellModule_t *g_Modules[SHELL_MODULE_MAX];
static uint8_t              g_ModuleCount = 0;

static char                 g_CmdLine[SHELL_CMD_MAX_LEN];
static uint16_t             g_CmdLen = 0;

static char                 g_OutBuf[SHELL_OUT_BUF_SIZE];
static bool                 g_Init = FALSE;
static bool                 g_WelcomeShown = FALSE;

// Current IO interface
static const ShellIO_t     *g_IO = NULL;
static ShellInputHandler_t  g_InputHandler = NULL;
static void                *g_InputUserData = NULL;
static bool                 g_IgnoreNextLf = FALSE;

/*******************************************************************************
 * Tab Completion
 ******************************************************************************/
#define SHELL_COMPLETE_MAX_CANDS   20   /* 最多匹配候选数 */
#define SHELL_COMPLETE_CAND_LEN    32   /* 单个候选字符串最大长度 */
static char   g_CandBuf[SHELL_COMPLETE_MAX_CANDS][SHELL_COMPLETE_CAND_LEN];
static uint8_t g_CandCount = 0;

/*******************************************************************************
 * Command History
 ******************************************************************************/
#define SHELL_HISTORY_MAX       10      /* 最多记录 10 条命令 */
static char     g_History[SHELL_HISTORY_MAX][SHELL_CMD_MAX_LEN];
static uint8_t  g_HistoryCount = 0;     /* 已存储条数 (0..SHELL_HISTORY_MAX) */
static uint8_t  g_HistoryHead  = 0;     /* 环形写入位置 */
static int8_t   g_HistoryNav   = -1;    /* 上下键浏览位置 (-1=当前输入) */
static char     g_SavedInput[SHELL_CMD_MAX_LEN]; /* 浏览历史时暂存当前输入 */
/* ESC序列状态机: 0=正常, 1=收到ESC, 2=收到ESC[ */
static uint8_t  g_EscState = 0;

static const char *g_CatNames[MOD_CAT_MAX] = {
    "System", "Hardware", "Parameter", "Debug"
};

/*******************************************************************************
 * Static function declarations
 ******************************************************************************/
static void Shell_ProcessChar(char c);
static void Shell_Execute(void);
int Shell_ExecuteLine(const char *line);
static int  Shell_ParseArgs(char *line, char *argv[], int max);
static void Shell_Prompt(void);
static void Shell_Welcome(void);
static void Shell_ShowModuleHelp(const ShellModule_t *mod);
static void Shell_HistoryAdd(const char *cmd);
static void Shell_HistoryRecall(int8_t direction);  /* +1=older, -1=newer */
static void Shell_TabComplete(void);
static void Shell_TabCompleteVfs(uint16_t tokenStart, uint16_t prefixLen);
static void Shell_ProcessCandidates(uint16_t tokenStart, uint16_t prefixLen, bool addSpace);

static void Shell_SendRaw(const char *str);

/**
 * @brief  Send raw binary data
 * @param  data: Binary data
 * @param  len: Data length
 */
void Shell_WriteRaw(const uint8_t *data, uint16_t len)
{
    if(data && len > 0 && g_IO && g_IO->send)
    {
        g_IO->send((uint8_t*)data, len);
    }
}

/**
 * @brief  Receive raw binary data from current IO interface (non-blocking)
 */
uint16_t Shell_RecvRaw(uint8_t *buf, uint16_t maxLen)
{
    if(!buf || maxLen == 0 || !g_IO || !g_IO->recv)
        return 0;

    /* Check if data is available first */
    if(g_IO->available)
    {
        if(g_IO->available() == 0)
            return 0;
    }

    return g_IO->recv(buf, maxLen);
}

/*******************************************************************************
 * Internal command processing
 ******************************************************************************/
static int Opt_HelpAll(int argc, char *argv[]);
static int Opt_HelpMod(int argc, char *argv[]);
static int Opt_List(int argc, char *argv[]);
static int Opt_Version(int argc, char *argv[]);
static int Opt_Clear(int argc, char *argv[]);
static int Opt_IO(int argc, char *argv[]);
static int Opt_History(int argc, char *argv[]);

// Help module options
static const ShellOpt_t g_HelpOpts[] = {
    OPT("a", "all",     NULL,       "Show all modules",     Opt_HelpAll),
    OPT("m", "module",  "<name>",   "Show module help",     Opt_HelpMod),
    OPT("l", "list",    NULL,       "List by category",     Opt_List),
    OPT("v", "version", NULL,       "Show version",         Opt_Version),
    OPT("c", "clear",   NULL,       "Clear screen",         Opt_Clear),
    OPT("i", "io",      NULL,       "Show current IO",      Opt_IO),
    OPT("h", "history", NULL,       "Show command history",  Opt_History),

    OPT_END()
};

static const ShellModule_t g_HelpModule = {
    "help", "Help and system info", MOD_CAT_SYSTEM, g_HelpOpts, 7
};

/*******************************************************************************
 * Common functionsMTU
 ******************************************************************************/

bool Shell_Init(void)
{
    if(g_Init) return TRUE;
    
    memset(g_Modules, 0, sizeof(g_Modules));
    g_ModuleCount = 0;
    g_CmdLen = 0;
    g_CmdLine[0] = '\0';
    g_IO = NULL;
    g_InputHandler = NULL;
    g_InputUserData = NULL;
    g_IgnoreNextLf = FALSE;
    g_WelcomeShown = FALSE;
    
    // Register default help module
    Shell_RegisterModule(&g_HelpModule);
    
    g_Init = TRUE;
    BanuxComponent_SetState("shell", BANUX_COMPONENT_READY);
    
    return TRUE;
}

bool Shell_SetIO(const ShellIO_t *io)
{
    if(io == NULL || io->send == NULL || io->recv == NULL)
        return FALSE;
    
    g_IO = io;
    g_WelcomeShown = FALSE;  // Reset welcome message after IO switch
    
    return TRUE;
}

const char* Shell_GetIOName(void)
{
    if(g_IO && g_IO->name)
        return g_IO->name;
    return "None";
}

bool Shell_RegisterModule(const ShellModule_t *module)
{
    if(module == NULL || g_ModuleCount >= SHELL_MODULE_MAX)
        return FALSE;
    uint8_t i;
    // Check module name uniqueness
    for(i = 0; i < g_ModuleCount; i++)
    {
        if(strcmp(g_Modules[i]->name, module->name) == 0)
            return FALSE;
    }
    
    g_Modules[g_ModuleCount++] = module;
    return TRUE;
}

void Shell_Process(void)
{
    if(!g_Init || !g_IO) return;
    
    // Show welcome message if not already done
    if(!g_WelcomeShown)
    {
        Shell_Welcome();
        Shell_Prompt();
        g_WelcomeShown = TRUE;
    }
    
    // Read data from IO interface
    uint8_t buf[64];
    uint16_t len = 0;
    uint16_t i;
    if(g_IO->available)
    {
        if(g_IO->available() > 0)
        {
            len = g_IO->recv(buf, sizeof(buf));
        }
    }
    else
    {
        // No available function, read directly
        len = g_IO->recv(buf, sizeof(buf));
    }
    
    // Process received data
    for(i = 0; i < len; i++)
    {
        if (g_IgnoreNextLf) {
            g_IgnoreNextLf = FALSE;
            if (buf[i] == '\n') continue;
        }
        if (g_InputHandler) {
            ShellInputHandler_t handler = g_InputHandler;
            handler(buf[i], g_InputUserData);
        } else {
            Shell_ProcessChar((char)buf[i]);
        }
    }
}

void Shell_InputChar(char c)
{
    if(!g_Init) return;
    
    // First input, show welcome message
    if(!g_WelcomeShown && g_IO)
    {
        Shell_Welcome();
        Shell_Prompt();
        g_WelcomeShown = TRUE;
    }
    
    if (g_IgnoreNextLf) {
        g_IgnoreNextLf = FALSE;
        if (c == '\n') return;
    }
    if (g_InputHandler) {
        ShellInputHandler_t handler = g_InputHandler;
        handler((uint8_t)c, g_InputUserData);
    } else {
        Shell_ProcessChar(c);
    }
}

void Shell_InputData(uint8_t *data, uint16_t len)
{
    if(!g_Init || !data) return;
    uint16_t i;
    for(i = 0; i < len; i++)
    {
        Shell_InputChar((char)data[i]);
    }
}

bool Shell_BeginInputMode(ShellInputHandler_t handler, void *userData)
{
    if (!handler || g_InputHandler) {
        return FALSE;
    }
    g_InputHandler = handler;
    g_InputUserData = userData;
    return TRUE;
}

void Shell_EndInputMode(void)
{
    g_InputHandler = NULL;
    g_InputUserData = NULL;
    g_CmdLen = 0;
    g_CmdLine[0] = '\0';
    g_HistoryNav = -1;
    g_IgnoreNextLf = TRUE;
    Shell_Prompt();
}

static void Shell_SendRaw(const char *str)
{
    if(str && g_IO && g_IO->send)
    {
        g_IO->send((uint8_t*)str, strlen(str));
    }
}

/* Print to CDC (for command output) */
void Shell_Print(const char *str)
{
    Shell_SendRaw(str);
}

void Shell_Printf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(g_OutBuf, sizeof(g_OutBuf), fmt, args);
    va_end(args);

    Shell_Print(g_OutBuf);  /* 输出到当前 IO 控制台 */
}

void Shell_NewLine(void)
{
    Shell_SendRaw("\r\n");
}

/*******************************************************************************
 * Static functions
 ******************************************************************************/

static void Shell_ProcessChar(char c)
{
    /* ESC 序列状态机 (处理方向键: ESC [ A/B) */
    if (g_EscState == 1) {
        if (c == '[') { g_EscState = 2; return; }
        g_EscState = 0;  /* 非 '[', 放弃 */
        return;
    }
    if (g_EscState == 2) {
        g_EscState = 0;
        if (c == 'A') { Shell_HistoryRecall(1); return; }  /* Up: 更早的命令 */
        if (c == 'B') { Shell_HistoryRecall(-1); return; } /* Down: 更近的命令 */
        return;  /* 其他 ESC[ 序列忽略 */
    }

    switch(c)
    {
        case 0x1B:  /* ESC */
            g_EscState = 1;
            break;

        case '\r':
        case '\n':
            Shell_SendRaw("\r\n");
            if(g_CmdLen > 0) {
                /* 保存到历史记录 */
                Shell_HistoryAdd(g_CmdLine);
                g_HistoryNav = -1;  /* 重置浏览位置 */
                Shell_Execute();
            }
            if (!g_InputHandler && !CommandParser_IsRunning()) {
                Shell_Prompt();
            }
            break;
            
        case '\b':
        case 0x7F:
            if(g_CmdLen > 0)
            {
                g_CmdLen--;
                g_CmdLine[g_CmdLen] = '\0';
                Shell_SendRaw("\b \b");
            }
            break;
            
        case 0x03:  // Ctrl+C
            Shell_SendRaw("\r\n");
            g_CmdLen = 0;
            g_CmdLine[0] = '\0';
            g_HistoryNav = -1;
            Shell_Prompt();
            break;

        case '\t':  /* Tab 补齐 */
            Shell_TabComplete();
            break;
            
        default:
            if(c >= 0x20 && c < 0x7F && g_CmdLen < SHELL_CMD_MAX_LEN - 1)
            {
                g_CmdLine[g_CmdLen++] = c;
                g_CmdLine[g_CmdLen] = '\0';
                // Echo to CDC only
                char echo[2] = {c, '\0'};
                Shell_SendRaw(echo);
            }
            break;
    }
}

static void Shell_Execute(void)
{
    (void)Shell_ExecuteLine(g_CmdLine);
    g_CmdLen = 0;
    g_CmdLine[0] = '\0';
}

int Shell_ExecuteLine(const char *line)
{
    char cmdLine[SHELL_CMD_MAX_LEN];
    char *argv[SHELL_CMD_MAX_ARGS];
    int argc;
    int ret = 0;
    uint16_t i;
    const ShellModule_t *mod = NULL;
    const ShellOpt_t *defaultOpt = NULL;
    char *optStr = NULL;
    bool isLong = FALSE;
    const ShellOpt_t *opt = NULL;

    if (line == NULL) {
        return -1;
    }
    if (strlen(line) >= sizeof(cmdLine)) {
        Shell_Print("Command too long\r\n");
        return -2;
    }

    strncpy(cmdLine, line, sizeof(cmdLine) - 1);
    cmdLine[sizeof(cmdLine) - 1] = '\0';

    argc = Shell_ParseArgs(cmdLine, argv, SHELL_CMD_MAX_ARGS);
    if(argc == 0) goto done;
    // Find module
    for(i = 0; i < g_ModuleCount; i++)
    {
        if(strcmp(argv[0], g_Modules[i]->name) == 0)
        {
            mod = g_Modules[i];
            break;
        }
    }

    if(mod == NULL)
    {
        Shell_Printf("Unknown module: %s\r\n", argv[0]);
        Shell_Print("Type 'help -a' for available modules\r\n");
        ret = -3;
        goto done;
    }

    // Check if module has a default option (opt == "" means direct command like 'ls')
    if(mod->optCount > 0 && mod->options[0].opt != NULL && mod->options[0].opt[0] == '\0')
    {
        defaultOpt = &mod->options[0];
    }

    // No option provided
    if(argc < 2)
    {
        // If has default option, call it directly (like 'ls', 'pwd')
        if(defaultOpt && defaultOpt->handler)
        {
            ret = defaultOpt->handler(0, NULL);
            if(ret != 0)
            {
                Shell_Printf("Error: %d\r\n", ret);
            }
            goto done;
        }
        // Otherwise show module help
        Shell_ShowModuleHelp(mod);
        goto done;
    }

    // Parse option
    optStr = argv[1];
    
    // If not starting with '-', treat as argument to default option (like 'ls /path', 'cd /dir')
    if(optStr[0] != '-')
    {
        if(defaultOpt && defaultOpt->handler)
        {
            ret = defaultOpt->handler(argc - 1, &argv[1]);
            if(ret != 0)
            {
                Shell_Printf("Error: %d\r\n", ret);
            }
            goto done;
        }
        // No default option, this is invalid
        Shell_Printf("Invalid option: %s\r\n", optStr);
        Shell_Printf("Use '%s' to see options\r\n", mod->name);
        ret = -4;
        goto done;
    }
    
    optStr++;
    if(optStr[0] == '-')
    {
        optStr++;
        isLong = TRUE;
    }

    // Find option
    for(i = 0; i < mod->optCount; i++)
    {
        if(isLong)
        {
            if(mod->options[i].longOpt && strcmp(optStr, mod->options[i].longOpt) == 0)
            {
                opt = &mod->options[i];
                break;
            }
        }
        else
        {
            if(mod->options[i].opt && strcmp(optStr, mod->options[i].opt) == 0)
            {
                opt = &mod->options[i];
                break;
            }
        }
    }
    
    if(opt == NULL)
    {
        Shell_Printf("Unknown option: %s\r\n", argv[1]);
        Shell_ShowModuleHelp(mod);
        ret = -5;
        goto done;
    }
    
    // Call handler function
    if(opt->handler)
    {
        ret = opt->handler(argc - 2, &argv[2]);
        if(ret != 0)
        {
            Shell_Printf("Error: %d\r\n", ret);
        }
    }
    
done:
    return ret;
}

static int Shell_ParseArgs(char *line, char *argv[], int max)
{
    int argc = 0;
    char *p = line;
    
    while(*p && argc < max)
    {
        while(*p == ' ' || *p == '\t') p++;
        if(*p == '\0') break;
        
        argv[argc++] = p;
        while(*p && *p != ' ' && *p != '\t') p++;
        if(*p) *p++ = '\0';
    }
    
    return argc;
}

static void Shell_Prompt(void)
{
    Shell_SendRaw("banux$ ");
}

void Shell_ShowPrompt(void)
{
    Shell_Prompt();
}

static void Shell_Welcome(void)
{
    Shell_SendRaw("\r\nBanux Shell v2.1\r\n");
    Shell_SendRaw("IO:");
    Shell_SendRaw(Shell_GetIOName());
    Shell_SendRaw("\r\n");
    Shell_SendRaw("'help -a' for cmds\r\n");
}

static void Shell_ShowModuleHelp(const ShellModule_t *mod)
{
    Shell_Printf("[%s] %s\r\n", mod->name, mod->desc);
    uint16_t i;
    for(i = 0; i < mod->optCount; i++)
    {
        const ShellOpt_t *opt = &mod->options[i];
        
        Shell_Print(" ");
        if(opt->opt)
        {
            Shell_Printf("-%s", opt->opt);
            if(opt->longOpt) Shell_Print("/");
        }
        if(opt->longOpt)
        {
            Shell_Printf("--%s", opt->longOpt);
        }
        if(opt->args)
        {
            Shell_Printf(" %s", opt->args);
        }
        Shell_Printf(": %s\r\n", opt->help);
    }
}


static int Opt_HelpAll(int argc, char *argv[])
{
    (void)argc; (void)argv;
    uint16_t i;
    Shell_Print("Modules:\r\n");
    for(i = 0; i < g_ModuleCount; i++)
    {
        Shell_Printf(" %s: %s\r\n", g_Modules[i]->name, g_Modules[i]->desc);
    }
    return 0;
}

static int Opt_HelpMod(int argc, char *argv[])
{
    if(argc < 1)
    {
        Shell_Print("Usage: help -m <mod>\r\n");
        return -1;
    }
    uint16_t i;
    for(i = 0; i < g_ModuleCount; i++)
    {
        if(strcmp(argv[0], g_Modules[i]->name) == 0)
        {
            Shell_ShowModuleHelp(g_Modules[i]);
            return 0;
        }
    }

    Shell_Printf("Unknown: %s\r\n", argv[0]);
    return -1;
}

static int Opt_List(int argc, char *argv[])
{
    (void)argc; (void)argv;
    uint16_t i,cat;
    for(cat = 0; cat < MOD_CAT_MAX; cat++)
    {
        bool has = FALSE;
        for(i = 0; i < g_ModuleCount; i++)
        {
            if(g_Modules[i]->category == cat)
            {
                if(!has)
                {
                    Shell_Printf("[%s]\r\n", g_CatNames[cat]);
                    has = TRUE;
                }
                Shell_Printf(" %s: %s\r\n", g_Modules[i]->name, g_Modules[i]->desc);
            }
        }
    }
    return 0;
}

static int Opt_Version(int argc, char *argv[])
{
    (void)argc; (void)argv;

    Shell_Print("BG Card v1.0.0\r\n");
    Shell_Printf("%s %s\r\n", __DATE__, __TIME__);
    Shell_Printf("IO:%s\r\n", Shell_GetIOName());
    return 0;
}

static int Opt_Clear(int argc, char *argv[])
{
    (void)argc; (void)argv;
    Shell_Print("\033[2J\033[H");
    return 0;
}

static int Opt_IO(int argc, char *argv[])
{
    (void)argc; (void)argv;
    Shell_Printf("Current IO: %s\r\n", Shell_GetIOName());
    return 0;
}

static int Opt_History(int argc, char *argv[])
{
    uint8_t i, idx;
    (void)argc; (void)argv;

    if (g_HistoryCount == 0) {
        Shell_Printf("(no history)\r\n");
        return 0;
    }

    Shell_Printf("=== Command History (last %u) ===\r\n", g_HistoryCount);
    for (i = 0; i < g_HistoryCount; i++) {
        /* 从最早到最近输出 */
        if (g_HistoryCount >= SHELL_HISTORY_MAX) {
            idx = (g_HistoryHead + i) % SHELL_HISTORY_MAX;
        } else {
            idx = i;
        }
        Shell_Printf("  [%u] %s\r\n", (unsigned)(i + 1), g_History[idx]);
    }
    return 0;
}

/*******************************************************************************
 * Command History Implementation
 ******************************************************************************/

/**
 * @brief  将命令添加到历史记录 (环形缓冲区)
 * @param  cmd: 命令字符串
 */
static void Shell_HistoryAdd(const char *cmd)
{
    if (!cmd || cmd[0] == '\0') return;

    /* 跳过与最近一条完全相同的命令 (避免重复记录) */
    if (g_HistoryCount > 0) {
        uint8_t last = (g_HistoryHead + SHELL_HISTORY_MAX - 1) % SHELL_HISTORY_MAX;
        if (strcmp(g_History[last], cmd) == 0) return;
    }

    strncpy(g_History[g_HistoryHead], cmd, SHELL_CMD_MAX_LEN - 1);
    g_History[g_HistoryHead][SHELL_CMD_MAX_LEN - 1] = '\0';
    g_HistoryHead = (g_HistoryHead + 1) % SHELL_HISTORY_MAX;
    if (g_HistoryCount < SHELL_HISTORY_MAX) g_HistoryCount++;
}

/**
 * @brief  通过上下方向键浏览命令历史
 * @param  direction: +1=向更早的命令(Up), -1=向更近的命令(Down)
 */
static void Shell_HistoryRecall(int8_t direction)
{
    const char *recall;
    uint16_t i;
    uint8_t idx;

    if (g_HistoryCount == 0) return;

    if (direction > 0) {
        /* Up: 向更早的命令 */
        if (g_HistoryNav < 0) {
            /* 首次按Up: 暂存当前输入, 跳到最近一条 */
            strncpy(g_SavedInput, g_CmdLine, SHELL_CMD_MAX_LEN - 1);
            g_SavedInput[SHELL_CMD_MAX_LEN - 1] = '\0';
            g_HistoryNav = 0;
        } else if (g_HistoryNav < (int8_t)(g_HistoryCount - 1)) {
            g_HistoryNav++;
        } else {
            return;  /* 已到最早 */
        }
    } else {
        /* Down: 向更近的命令 */
        if (g_HistoryNav < 0) return;  /* 已在当前输入 */
        g_HistoryNav--;
    }

    /* 获取要回显的内容 */
    if (g_HistoryNav < 0) {
        /* 回到用户原始输入 */
        recall = g_SavedInput;
    } else {
        /* 从环形缓冲区取: nav=0 是最近, nav=count-1 是最早 */
        idx = (g_HistoryHead + SHELL_HISTORY_MAX - 1 - (uint8_t)g_HistoryNav) % SHELL_HISTORY_MAX;
        recall = g_History[idx];
    }

    /* 清除当前行显示 */
    for (i = 0; i < g_CmdLen; i++) {
        Shell_SendRaw("\b \b");
    }

    /* 设置新命令行 */
    strncpy(g_CmdLine, recall, SHELL_CMD_MAX_LEN - 1);
    g_CmdLine[SHELL_CMD_MAX_LEN - 1] = '\0';
    g_CmdLen = (uint16_t)strlen(g_CmdLine);

    /* 回显 */
    Shell_SendRaw(g_CmdLine);
}

/*******************************************************************************
 * Tab Completion Implementation
 ******************************************************************************/

/**
 * @brief  Tab 补齐 (Linux 风格):
 *         唯一匹配 -> 直接补全 (命令名后自动补一个空格);
 *         多个匹配 -> 先扩展到共同前缀, 再换行列出自有候选, 最后恢复 prompt+输入行;
 *         无匹配   -> 响铃 '\a'。
 *         命令位置补全模块名; 参数位置补全 -短选项 / --长选项。
 */
static void Shell_TabComplete(void)
{
    uint16_t tokenStart = 0, prefixLen, i;
    bool isCmdWord = TRUE;

    if (g_CmdLen == 0) return;

    /* 定位最后一个 token 的起始位置 (只要出现过空格, 就不再是命令位置) */
    for (i = 0; i < g_CmdLen; i++) {
        if (g_CmdLine[i] == ' ') {
            tokenStart = i + 1;
            isCmdWord = FALSE;
        }
    }
    prefixLen = (uint16_t)(g_CmdLen - tokenStart);

    g_CandCount = 0;

    if (isCmdWord) {
        /* --- 命令名补全 --- */
        for (i = 0; i < g_ModuleCount && g_CandCount < SHELL_COMPLETE_MAX_CANDS; i++) {
            if (strncmp(g_Modules[i]->name, &g_CmdLine[tokenStart], prefixLen) == 0) {
                strncpy(g_CandBuf[g_CandCount], g_Modules[i]->name, SHELL_COMPLETE_CAND_LEN - 1);
                g_CandBuf[g_CandCount][SHELL_COMPLETE_CAND_LEN - 1] = '\0';
                g_CandCount++;
            }
        }
        /* 唯一命令名补全后自动加空格 */
        Shell_ProcessCandidates(tokenStart, prefixLen, TRUE);
    } else {
        /* --- 参数位置补全: 先解析命令名 --- */
        char line[SHELL_CMD_MAX_LEN];
        char *argv[SHELL_CMD_MAX_ARGS];
        int argc;
        const ShellModule_t *mod = NULL;

        strncpy(line, g_CmdLine, SHELL_CMD_MAX_LEN);
        line[SHELL_CMD_MAX_LEN - 1] = '\0';
        argc = Shell_ParseArgs(line, argv, SHELL_CMD_MAX_ARGS);

        if (argc > 0) {
            for (i = 0; i < g_ModuleCount; i++) {
                if (strcmp(argv[0], g_Modules[i]->name) == 0) {
                    mod = g_Modules[i];
                    break;
                }
            }
        }

        if (mod != NULL && g_CmdLine[tokenStart] == '-') {
            /* --- 选项补全 (跳过空 opt/longOpt, 即 default 参数, 避免 '-<空>' 候选) --- */
            bool isLong = (prefixLen >= 2 && g_CmdLine[tokenStart + 1] == '-');
            const char *optPrefix = &g_CmdLine[tokenStart + (isLong ? 2 : 1)];
            uint16_t optPrefixLen = (uint16_t)(prefixLen - (isLong ? 2 : 1));
            uint16_t j;
            for (j = 0; j < mod->optCount && g_CandCount < SHELL_COMPLETE_MAX_CANDS; j++) {
                const ShellOpt_t *o = &mod->options[j];
                if (isLong) {
                    if (o->longOpt && o->longOpt[0] && strncmp(o->longOpt, optPrefix, optPrefixLen) == 0) {
                        snprintf(g_CandBuf[g_CandCount], SHELL_COMPLETE_CAND_LEN, "--%s", o->longOpt);
                        g_CandCount++;
                    }
                } else {
                    if (o->opt && o->opt[0] && strncmp(o->opt, optPrefix, optPrefixLen) == 0) {
                        snprintf(g_CandBuf[g_CandCount], SHELL_COMPLETE_CAND_LEN, "-%s", o->opt);
                        g_CandCount++;
                    }
                }
            }
            Shell_ProcessCandidates(tokenStart, prefixLen, FALSE);
        } else {
            /* --- VFS 路径补全 --- */
            Shell_TabCompleteVfs(tokenStart, prefixLen);
        }
    }
}

/**
 * @brief  处理已收集的候选: 无匹配响铃; 唯一匹配直接补全; 多个匹配先补共同前缀再列表
 * @param  tokenStart: 当前 token 在命令行中的起始位置
 * @param  prefixLen:  当前 token 长度
 * @param  addSpace:   唯一补全后是否追加空格 (命令名补全为 TRUE, 选项/VFS 为 FALSE)
 */
static void Shell_ProcessCandidates(uint16_t tokenStart, uint16_t prefixLen, bool addSpace)
{
    uint16_t i, common;

    if (g_CandCount == 0) {
        Shell_SendRaw("\a");   /* 无匹配: 响铃 */
        return;
    }

    /* 计算所有候选的共同前缀 */
    common = (uint16_t)strlen(g_CandBuf[0]);
    for (i = 1; i < g_CandCount; i++) {
        uint16_t k = 0;
        while (k < common && g_CandBuf[i][k] && g_CandBuf[0][k] == g_CandBuf[i][k]) k++;
        common = k;
        if (common == 0) break;
    }

    if (g_CandCount == 1) {
        /* 唯一匹配: 补全剩余部分 */
        uint16_t addLen = (uint16_t)strlen(g_CandBuf[0]);
        if (addLen > prefixLen) {
            for (i = prefixLen; i < addLen && g_CmdLen < SHELL_CMD_MAX_LEN - 2; i++) {
                g_CmdLine[g_CmdLen++] = g_CandBuf[0][i];
            }
            g_CmdLine[g_CmdLen] = '\0';
            Shell_SendRaw(&g_CandBuf[0][prefixLen]);
        }
        if (addSpace && g_CmdLen < SHELL_CMD_MAX_LEN - 1) {
            g_CmdLine[g_CmdLen++] = ' ';
            g_CmdLine[g_CmdLen] = '\0';
            Shell_SendRaw(" ");
        }
        return;
    }

    /* 多个匹配: 先扩展到共同前缀 (若比当前输入长) */
    if (common > prefixLen) {
        uint16_t addLen = (uint16_t)(common - prefixLen);
        for (i = 0; i < addLen && g_CmdLen < SHELL_CMD_MAX_LEN - 1; i++) {
            g_CmdLine[g_CmdLen++] = g_CandBuf[0][prefixLen + i];
        }
        g_CmdLine[g_CmdLen] = '\0';
        Shell_SendRaw(&g_CandBuf[0][prefixLen]);
    }

    /* 换行列出自有候选 (每行 4 个) */
    Shell_SendRaw("\r\n");
    for (i = 0; i < g_CandCount; i++) {
        Shell_Printf("%s  ", g_CandBuf[i]);
        if ((i & 3) == 3) Shell_SendRaw("\r\n");
    }
    Shell_SendRaw("\r\n");

    /* 恢复 prompt 和当前输入行 */
    Shell_Prompt();
    Shell_SendRaw(g_CmdLine);
}

/**
 * @brief  VFS 路径补全: 匹配当前节点下的子节点名。
 *         目录/DEV 节点补 '/' (可继续补子路径), 参数/CMD 节点补 ' '。
 *         支持相对路径、绝对路径 (/...)、'..' 等 (由 Vfs_FindNode 解析)。
 */
static void Shell_TabCompleteVfs(uint16_t tokenStart, uint16_t prefixLen)
{
    const char *token = &g_CmdLine[tokenStart];
    const char *slash = NULL;
    char dirPath[VFS_MAX_PATH_LEN];
    char namePrefix[VFS_MAX_NAME_LEN];
    uint16_t namePrefixLen, dirLen;
    VfsNode_t *dir;
    uint16_t i, j, k;

    /* 找到最后一个 '/' 以分离目录部分与名字前缀 */
    for (i = 0; i < prefixLen; i++) {
        if (token[i] == '/') slash = &token[i];
    }

    if (slash) {
        dirLen = (uint16_t)(slash - token);
        if (dirLen == 0) { dirPath[0] = '/'; dirPath[1] = '\0'; }
        else {
            if (dirLen >= VFS_MAX_PATH_LEN) dirLen = VFS_MAX_PATH_LEN - 1;
            memcpy(dirPath, token, dirLen);
            dirPath[dirLen] = '\0';
        }
        namePrefixLen = (uint16_t)(prefixLen - dirLen - 1);
        if (namePrefixLen >= VFS_MAX_NAME_LEN) namePrefixLen = VFS_MAX_NAME_LEN - 1;
        memcpy(namePrefix, slash + 1, namePrefixLen);
        namePrefix[namePrefixLen] = '\0';
        dir = Vfs_FindNode(dirPath);
    } else {
        dirLen = 0;
        namePrefixLen = prefixLen;
        if (namePrefixLen >= VFS_MAX_NAME_LEN) namePrefixLen = VFS_MAX_NAME_LEN - 1;
        memcpy(namePrefix, token, namePrefixLen);
        namePrefix[namePrefixLen] = '\0';
        dir = Vfs_GetCwd();
    }

    if (!dir) { Shell_SendRaw("\a"); return; }   /* 目录无效 */

    /* 收集子节点候选 */
    g_CandCount = 0;
    for (i = 0; i < dir->childCount && g_CandCount < SHELL_COMPLETE_MAX_CANDS; i++) {
        VfsNode_t *child = dir->children[i];
        if (!child) continue;
        if (strncmp(child->name, namePrefix, namePrefixLen) != 0) continue;

        /* 候选 = 输入中的目录部分 + 子节点名 + 后缀 */
        k = 0;
        for (j = 0; j < dirLen && k < SHELL_COMPLETE_CAND_LEN - 2; j++) {
            g_CandBuf[g_CandCount][k++] = token[j];
        }
        for (j = 0; child->name[j] && k < SHELL_COMPLETE_CAND_LEN - 2; j++) {
            g_CandBuf[g_CandCount][k++] = child->name[j];
        }
        if (k < SHELL_COMPLETE_CAND_LEN - 1) {
            bool isDir = (child->type == VFS_NODE_DIR || child->type == VFS_NODE_DEV);
            g_CandBuf[g_CandCount][k++] = isDir ? '/' : ' ';
        }
        g_CandBuf[g_CandCount][k] = '\0';
        g_CandCount++;
    }

    Shell_ProcessCandidates(tokenStart, prefixLen, FALSE);
}
