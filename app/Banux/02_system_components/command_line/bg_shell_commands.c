/**
 *****************************************************************************
 * @file     bg_shell_commands.c
 * @author   Ender-3 V2 Porting
 * @brief    Shell command module implementation (STM32F103RET6)
 *
 * 本文件由 BG Card 原版裁剪移植到 Ender-3 V2 APP 工程：
 *   1. sys     模块：改用 STM32 HAL API（时钟频率 / 软复位）
 *   2. VFS     模块：ls/pwd/cd/cat/echo/tree/drivers 原样保留
 *   3. boot    模块：改用 APP 侧 app_bl_enter_boot()（写魔数后复位）
 *   4. 删除 audio/gpio/led/dbg/looper/flash/battery/bt/ble/chain/upg 等
 *      与 Ender-3 V2 平台无关的命令模块
 *****************************************************************************
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "stm32f1xx_hal.h" /* HAL: HAL_RCC_GetSysClockFreq / NVIC_SystemReset */
#include "bg_shell.h"
#include "drv_init.h"
#include "vfs.h"        /* 虚拟文件系统API */
#include "drv_fs.h"     /* 驱动文件系统API */
#include "drv_device.h" /* 驱动设备管理 */
#include "fs_sd.h"      /* SD file create/write API */
#include "app_bl.h"     /* APP 侧 boot 联动：app_bl_enter_boot() */
#include "app_version.h"
#include "banux_component.h"
#include "banux_io.h"
#include "command_parser.h"
#include "bg_event.h"

/*============================================================================
 * sys module - System information
 *===========================================================================*/

static int sys_info(int argc, char *argv[])
{
    uint32_t sysclk;

    (void)argc; (void)argv;

    sysclk = HAL_RCC_GetSysClockFreq();

    Shell_Print("\r\nSystem Information:\r\n");
    Shell_Print("  Device:    Ender-3 V2\r\n");
    Shell_Print("  MCU:       STM32F103RET6\r\n");
    Shell_Printf("  Clock:     %lu MHz\r\n", (unsigned long)(sysclk / 1000000u));
    Shell_Print("  Flash:     512 KB\r\n");
    Shell_Print("  RAM:       64 KB\r\n");
    Shell_Printf("  Shell IO:  %s\r\n\r\n", Shell_GetIOName());
    return 0;
}

static int sys_mem(int argc, char *argv[])
{
    (void)argc; (void)argv;
    Shell_Print("\r\nMemory Status:\r\n");
    Shell_Print("  Flash:     512 KB (App @ 0x08008000)\r\n");
    Shell_Print("  SRAM:      64 KB\r\n\r\n");
    return 0;
}

static int sys_reboot(int argc, char *argv[])
{
    (void)argc; (void)argv;
    Shell_Print("System resetting...\r\n");
    NVIC_SystemReset();
    return 0;
}

static int sys_version(int argc, char *argv[])
{
    (void)argc; (void)argv;
    Shell_Printf("Firmware version: %s\r\n", APP_VERSION_STRING);
    return 0;
}

static const ShellOpt_t sys_opts[] = {
    OPT("i", "info",    NULL,   "Show system info",     sys_info),
    OPT("m", "mem",     NULL,   "Show memory status",   sys_mem),
    OPT("v", "version", NULL,   "Show firmware version", sys_version),
    OPT("r", "reset",   NULL,   "Software reset system", sys_reboot),
    OPT("b", "reboot",  NULL,   "Reboot system",        sys_reboot),
    OPT_END()
};

DEFINE_MODULE(sys, "System information", MOD_CAT_SYSTEM, sys_opts);

/*============================================================================
 * banux module - Framework version and component inventory
 *===========================================================================*/

static void banux_print_component_type(BanuxComponentType_t type,
                                       const char *heading)
{
    uint8_t i;

    Shell_Printf("\r\n%s:\r\n", heading);
    for (i = 0; i < BanuxComponent_GetCount(); i++) {
        const BanuxComponentInfo_t *info = BanuxComponent_Get(i);
        const BanuxComponentDescriptor_t *component;

        if (!info || !info->descriptor || info->descriptor->type != type) {
            continue;
        }
        component = info->descriptor;
        Shell_Printf("  %-18s %-10s v%-7s %s\r\n",
                     component->name,
                     BanuxComponent_StateName(info->state),
                     component->version,
                     component->description);
    }
}

static int banux_info(int argc, char *argv[])
{
    uint8_t i;
    uint8_t enabled = 0;
    uint8_t ready = 0;

    (void)argc; (void)argv;
    for (i = 0; i < BanuxComponent_GetCount(); i++) {
        const BanuxComponentInfo_t *info = BanuxComponent_Get(i);
        if (!info || !info->descriptor) continue;
        if (info->descriptor->enabled) enabled++;
        if (info->state == BANUX_COMPONENT_READY) ready++;
    }

    Shell_Print("\r\nBanux Information:\r\n");
    Shell_Printf("  Framework version: %s\r\n", BANUX_VERSION_STRING);
    Shell_Printf("  Firmware version:  %s\r\n", APP_VERSION_STRING);
    Shell_Printf("  Components:        %u total, %u enabled, %u ready\r\n",
                 (unsigned)BanuxComponent_GetCount(),
                 (unsigned)enabled, (unsigned)ready);
    banux_print_component_type(BANUX_COMPONENT_SYSTEM, "System components");
    banux_print_component_type(BANUX_COMPONENT_APPLICATION,
                               "Application components (03_application_components)");
    Shell_Print("\r\n");
    return 0;
}

static const ShellOpt_t banux_opts[] = {
    OPT("i", "info", NULL, "Show Banux framework information", banux_info),
    OPT_END()
};

DEFINE_MODULE(banux, "Banux framework management", MOD_CAT_SYSTEM, banux_opts);

#if BG_EVENT_EN
static int event_tree(int argc, char *argv[])
{
    BG_EventSubscriptionInfo_t current;
    BG_EventSubscriptionInfo_t candidate;
    uint8_t count = BG_Event_GetSubscriberCount();
    uint8_t i;
    uint8_t j;
    uint8_t seen;

    (void)argc;
    (void)argv;
    Shell_Printf("\r\nSubscription tree: %u active\r\n", (unsigned int)count);
    for (i = 0u; i < count; i++) {
        if (BG_Event_GetSubscription(i, &current) != 0) continue;
        seen = 0u;
        for (j = 0u; j < i; j++) {
            if (BG_Event_GetSubscription(j, &candidate) == 0 &&
                candidate.topic == current.topic) {
                seen = 1u;
                break;
            }
        }
        if (seen) continue;

        Shell_Printf("+-- %s [0x%04X]\r\n",
                     BG_Event_GetTopicName(current.topic),
                     (unsigned int)current.topic);
        for (j = i; j < count; j++) {
            if (BG_Event_GetSubscription(j, &candidate) == 0 &&
                candidate.topic == current.topic) {
                Shell_Printf("    +-- %s\r\n",
                             candidate.name ? candidate.name : "unnamed");
            }
        }
    }
    Shell_Print("\r\n");
    return 0;
}

static const ShellOpt_t event_opts[] = {
    OPT("t", "tree", NULL, "Show topic subscription tree", event_tree),
    OPT_END()
};

DEFINE_MODULE(event, "Event subscription diagnostics", MOD_CAT_SYSTEM, event_opts);
#endif

/*============================================================================
 * VFS 文件系统命令模块
 *===========================================================================*/

static int cmd_ls(int argc, char *argv[])
{
    FsNode_t *node;
    int i;
    char lineBuf[256];
    int lineLen = 0;
    int itemsInLine = 0;

    /* 如果有参数，使用参数查找节点；否则使用当前工作目录 */
    if (argc > 0) {
        node = DrvFs_FindNode(argv[0]);
        if (!node) {
            Shell_Printf("ls: cannot access '%s': No such file or directory\r\n", argv[0]);
            return -1;
        }
    } else {
        /* 无参数时，列出当前工作目录 */
        node = DrvFs_GetCwd();
        if (!node) {
            Shell_Print("ls: cannot get current directory\r\n");
            return -1;
        }
    }

    if (node->type != FS_NODE_DIR && node->type != FS_NODE_DEV) {
        Shell_Printf("ls: '%s': Not a directory\r\n", node->name);
        return -1;
    }

    /* 动态目录 (如 /sd 挂载点) 先懒加载子节点 */
    DrvFs_RefreshDir(node);

    Shell_Printf("\r\n");

    lineBuf[0] = '\0';
    lineLen = 0;
    itemsInLine = 0;

    for (i = 0; i < node->childCount; i++) {
        FsNode_t *child = node->children[i];
        char itemBuf[64];

        /* 格式化单个项目 */
        switch (child->type) {
            case FS_NODE_DIR:
                snprintf(itemBuf, sizeof(itemBuf), "\033[1;34m%s\033[0m", child->name);
                break;
            case FS_NODE_DEV:
                snprintf(itemBuf, sizeof(itemBuf), "\033[1;32m%s\033[0m", child->name);
                break;
            case FS_NODE_PARAM:
                snprintf(itemBuf, sizeof(itemBuf), "%s", child->name);
                break;
            case FS_NODE_FILE:
                snprintf(itemBuf, sizeof(itemBuf), "%s (%lu)", child->name, (unsigned long)child->fileSize);
                break;
            default:
                snprintf(itemBuf, sizeof(itemBuf), "%s", child->name);
                break;
        }

        /* 添加到行缓冲 */
        if (lineLen + strlen(itemBuf) + 4 < sizeof(lineBuf)) {
            strcat(lineBuf, itemBuf);
            lineLen += strlen(itemBuf);
            itemsInLine++;

            /* 添加分隔符或换行 */
            if (itemsInLine >= 2) {
                /* 输出这一行 */
                Shell_Printf("%s\r\n", lineBuf);
                lineBuf[0] = '\0';
                lineLen = 0;
                itemsInLine = 0;
            } else if (i < node->childCount - 1) {
                strcat(lineBuf, "    ");  /* 4个空格分隔 */
                lineLen += 4;
            }
        }
    }

    /* 输出剩余项目 */
    if (itemsInLine > 0) {
        Shell_Printf("%s\r\n", lineBuf);
    }

    Shell_Print("\r\n");
    return 0;
}

static const ShellOpt_t ls_opts[] = {
    OPT("", "", "[path]", "List directory contents", cmd_ls),
    OPT_END()
};

DEFINE_MODULE(ls, "List directory contents", MOD_CAT_SYSTEM, ls_opts);

static int cmd_pwd(int argc, char *argv[])
{
    char path[64];

    (void)argc; (void)argv;

    if (DrvFs_GetCwdPath(path, sizeof(path)) == FS_OK) {
        Shell_Printf("%s\r\n", path);
    } else {
        Shell_Print("pwd: error getting current directory\r\n");
        return -1;
    }

    return 0;
}

static const ShellOpt_t pwd_opts[] = {
    OPT("", "", NULL, "Print working directory", cmd_pwd),
    OPT_END()
};

DEFINE_MODULE(pwd, "Print working directory", MOD_CAT_SYSTEM, pwd_opts);

static int cmd_cd(int argc, char *argv[])
{
    if (argc < 1) {
        /* 无参数时返回根目录 */
        if (DrvFs_Cd("/") != FS_OK) {
            Shell_Print("cd: error\r\n");
            return -1;
        }
        return 0;
    }

    if (DrvFs_Cd(argv[0]) != FS_OK) {
        Shell_Printf("cd: %s: No such directory\r\n", argv[0]);
        return -1;
    }

    return 0;
}

static const ShellOpt_t cd_opts[] = {
    OPT("", "", "[path]", "Change directory", cmd_cd),
    OPT_END()
};

DEFINE_MODULE(cd, "Change directory", MOD_CAT_SYSTEM, cd_opts);

static int cmd_cat(int argc, char *argv[])
{
    FsNode_t *node;
    char buf[128];
    int ret;

    if (argc < 1) {
        Shell_Print("cat: missing operand\r\n");
        Shell_Print("Usage: cat <parameter>\r\n");
        return -1;
    }

    node = DrvFs_FindNode(argv[0]);
    if (!node) {
        Shell_Printf("cat: %s: No such file or directory\r\n", argv[0]);
        return -1;
    }

    if (node->type == FS_NODE_FILE) {
        /* 文件节点 (真实文件系统, 如 SD 卡文件): 分块读取全部内容 */
        uint32_t offset = 0;

        for (;;) {
            int n;

            if (offset >= node->fileSize) {
                break;
            }
            n = DrvFs_ReadFile(node, buf, (uint16_t)(sizeof(buf) - 1), offset);
            if (n < 0) {
                Shell_Printf("\r\ncat: %s: Read error at offset %lu\r\n",
                             argv[0], (unsigned long)offset);
                return -1;
            }
            if (n == 0) {
                break;
            }
            buf[n] = '\0';
            Shell_Print(buf);
            offset += (uint32_t)n;
        }
        Shell_Print("\r\n");
        return 0;
    }

    if (node->type != FS_NODE_PARAM) {
        Shell_Printf("cat: %s: Not a parameter file\r\n", argv[0]);
        return -1;
    }

    ret = banux_read(argv[0], buf, sizeof(buf));
    if (ret < 0) {
        if (ret == -2) {
            Shell_Printf("cat: %s: Write-only parameter (use echo to set)\r\n", argv[0]);
        } else {
            Shell_Printf("cat: %s: Read error\r\n", argv[0]);
        }
        return -1;
    }

    Shell_Printf("%s\r\n", buf);
    return 0;
}

static const ShellOpt_t cat_opts[] = {
    OPT("", "", "<file>", "Display parameter value", cmd_cat),
    OPT_END()
};

DEFINE_MODULE(cat, "Display file contents", MOD_CAT_SYSTEM, cat_opts);

/*============================================================================
 * SD text file creation and line editor
 *===========================================================================*/

#define EDITOR_BUFFER_SIZE  2048u

typedef enum {
    EDITOR_COMMAND_MODE = 0,
    EDITOR_APPEND_MODE
} EditorMode_t;

typedef struct {
    char path[VFS_MAX_PATH_LEN];
    uint8_t data[EDITOR_BUFFER_SIZE];
    uint32_t length;
    char line[SHELL_CMD_MAX_LEN];
    uint16_t lineLen;
    EditorMode_t mode;
    bool dirty;
    bool skipLf;
} EditorState_t;

static EditorState_t s_editor;

static void editor_prompt(void)
{
    Shell_Print((s_editor.mode == EDITOR_APPEND_MODE) ? "insert> " : "vim> ");
}

static void editor_print(void)
{
    uint32_t start = 0;
    uint32_t lineNo = 1;

    Shell_Printf("\r\n--- %s (%lu bytes)%s ---\r\n", s_editor.path,
                 (unsigned long)s_editor.length, s_editor.dirty ? " [modified]" : "");
    while (start < s_editor.length) {
        uint32_t end = start;
        while (end < s_editor.length && s_editor.data[end] != '\n') end++;
        Shell_Printf("%3lu | ", (unsigned long)lineNo++);
        if (end > start) {
            Shell_WriteRaw(&s_editor.data[start], (uint16_t)(end - start));
        }
        Shell_Print("\r\n");
        start = (end < s_editor.length) ? end + 1u : end;
    }
    if (s_editor.length == 0) Shell_Print("(empty)\r\n");
}

static int editor_delete_line(uint32_t wanted)
{
    uint32_t lineNo = 1;
    uint32_t start = 0;
    uint32_t end;

    if (wanted == 0) return -1;
    while (start < s_editor.length && lineNo < wanted) {
        while (start < s_editor.length && s_editor.data[start] != '\n') start++;
        if (start < s_editor.length) start++;
        lineNo++;
    }
    if (start >= s_editor.length || lineNo != wanted) return -1;

    end = start;
    while (end < s_editor.length && s_editor.data[end] != '\n') end++;
    if (end < s_editor.length) end++;
    memmove(&s_editor.data[start], &s_editor.data[end], s_editor.length - end);
    s_editor.length -= end - start;
    s_editor.dirty = TRUE;
    return 0;
}

static int editor_save(void)
{
    int ret = SdFs_WriteFile(s_editor.path, s_editor.data, s_editor.length);
    if (ret != 0) {
        Shell_Printf("vim: write failed (%d)\r\n", ret);
        return ret;
    }
    s_editor.dirty = FALSE;
    Shell_Printf("%s: %lu bytes written\r\n", s_editor.path,
                 (unsigned long)s_editor.length);
    return 0;
}

static void editor_exit(void)
{
    Shell_Print("\r\n");
    memset(&s_editor, 0, sizeof(s_editor));
    Shell_EndInputMode();
}

static void editor_process_line(void)
{
    char *line = s_editor.line;

    line[s_editor.lineLen] = '\0';
    s_editor.lineLen = 0;

    if (s_editor.mode == EDITOR_APPEND_MODE) {
        size_t len;

        if (strcmp(line, ".") == 0) {
            s_editor.mode = EDITOR_COMMAND_MODE;
            editor_prompt();
            return;
        }
        len = strlen(line);
        if (s_editor.length + len + 1u >= EDITOR_BUFFER_SIZE) {
            Shell_Print("vim: buffer full\r\n");
            editor_prompt();
            return;
        }
        memcpy(&s_editor.data[s_editor.length], line, len);
        s_editor.length += (uint32_t)len;
        s_editor.data[s_editor.length++] = '\n';
        s_editor.dirty = TRUE;
        editor_prompt();
        return;
    }

    if (strcmp(line, ":p") == 0 || strcmp(line, "p") == 0) {
        editor_print();
    } else if (strcmp(line, ":a") == 0 || strcmp(line, "a") == 0 ||
               strcmp(line, "i") == 0) {
        s_editor.mode = EDITOR_APPEND_MODE;
    } else if (strcmp(line, ":c") == 0) {
        s_editor.length = 0;
        s_editor.dirty = TRUE;
        s_editor.mode = EDITOR_APPEND_MODE;
    } else if (strncmp(line, ":d ", 3) == 0) {
        uint32_t lineNo = (uint32_t)strtoul(line + 3, NULL, 10);
        if (editor_delete_line(lineNo) != 0) {
            Shell_Printf("vim: line %lu not found\r\n", (unsigned long)lineNo);
        }
    } else if (strcmp(line, ":w") == 0) {
        (void)editor_save();
    } else if (strcmp(line, ":wq") == 0) {
        if (editor_save() == 0) {
            editor_exit();
            return;
        }
    } else if (strcmp(line, ":q!") == 0) {
        editor_exit();
        return;
    } else if (strcmp(line, ":q") == 0) {
        if (s_editor.dirty) {
            Shell_Print("vim: unsaved changes (use :q! or :wq)\r\n");
        } else {
            editor_exit();
            return;
        }
    } else if (line[0] != '\0') {
        Shell_Print("vim: unknown command\r\n");
    }
    editor_prompt();
}

static void editor_input(uint8_t byte, void *userData)
{
    (void)userData;

    if (byte == '\n' && s_editor.skipLf) {
        s_editor.skipLf = FALSE;
        return;
    }
    if (byte == '\r' || byte == '\n') {
        Shell_Print("\r\n");
        s_editor.skipLf = (byte == '\r') ? TRUE : FALSE;
        editor_process_line();
        return;
    }
    s_editor.skipLf = FALSE;

    if (byte == 0x03u) {
        Shell_Print("^C\r\n");
        editor_exit();
        return;
    }
    if (byte == 0x1Bu && s_editor.mode == EDITOR_APPEND_MODE) {
        s_editor.lineLen = 0;
        s_editor.mode = EDITOR_COMMAND_MODE;
        Shell_Print("\r\n");
        editor_prompt();
        return;
    }
    if (byte == '\b' || byte == 0x7Fu) {
        if (s_editor.lineLen > 0) {
            s_editor.lineLen--;
            Shell_Print("\b \b");
        }
        return;
    }
    if (byte >= 0x20u && s_editor.lineLen < (uint16_t)(sizeof(s_editor.line) - 1u)) {
        s_editor.line[s_editor.lineLen++] = (char)byte;
        Shell_WriteRaw(&byte, 1);
    }
}

static int cmd_touch(int argc, char *argv[])
{
    int i;

    if (argc < 1) {
        Shell_Print("Usage: touch <file> [file...]\r\n");
        return -1;
    }
    for (i = 0; i < argc; i++) {
        int ret = SdFs_Touch(argv[i]);
        if (ret != 0) {
            Shell_Printf("touch: %s: create failed (%d)\r\n", argv[i], ret);
            return ret;
        }
    }
    return 0;
}

static const ShellOpt_t touch_opts[] = {
    OPT("", "", "<file> [file...]", "Create filesystem file", cmd_touch),
    OPT_END()
};

DEFINE_MODULE(touch, "Create filesystem file", MOD_CAT_SYSTEM, touch_opts);

static int cmd_mkdir(int argc, char *argv[])
{
    int i;

    if (argc < 1) {
        Shell_Print("Usage: mkdir <directory> [directory...]\r\n");
        return -1;
    }
    for (i = 0; i < argc; i++) {
        int ret = SdFs_Mkdir(argv[i]);
        if (ret != 0) {
            Shell_Printf("mkdir: %s: create failed (%d)\r\n", argv[i], ret);
            return ret;
        }
    }
    return 0;
}

static const ShellOpt_t mkdir_opts[] = {
    OPT("", "", "<directory> [directory...]", "Create filesystem directory", cmd_mkdir),
    OPT_END()
};

DEFINE_MODULE(mkdir, "Create filesystem directory", MOD_CAT_SYSTEM, mkdir_opts);

static int cmd_rm(int argc, char *argv[])
{
    int i;

    if (argc < 1) {
        Shell_Print("Usage: rm <path> [path...]\r\n");
        return -1;
    }
    for (i = 0; i < argc; i++) {
        int ret = SdFs_Remove(argv[i]);
        if (ret != 0) {
            Shell_Printf("rm: %s: remove failed (%d)\r\n", argv[i], ret);
            return ret;
        }
    }
    return 0;
}

static const ShellOpt_t rm_opts[] = {
    OPT("", "", "<path> [path...]", "Remove file or empty directory", cmd_rm),
    OPT_END()
};

DEFINE_MODULE(rm, "Remove file or empty directory", MOD_CAT_SYSTEM, rm_opts);

static int cmd_vim(int argc, char *argv[])
{
    FsNode_t *node;
    uint32_t offset = 0;

    if (argc < 1) {
        Shell_Print("Usage: vim <file>\r\n");
        return -1;
    }

    node = DrvFs_FindNode(argv[0]);
    if (!node) {
        int ret = SdFs_Touch(argv[0]);
        if (ret != 0) {
            Shell_Printf("vim: %s: create failed (%d)\r\n", argv[0], ret);
            return ret;
        }
        node = DrvFs_FindNode(argv[0]);
    }
    if (!node || node->type != FS_NODE_FILE) {
        Shell_Printf("vim: %s: not a filesystem file\r\n", argv[0]);
        return -1;
    }
    if (node->fileSize >= EDITOR_BUFFER_SIZE) {
        Shell_Printf("vim: file too large (max %u bytes)\r\n", EDITOR_BUFFER_SIZE - 1u);
        return -1;
    }

    memset(&s_editor, 0, sizeof(s_editor));
    strncpy(s_editor.path, argv[0], sizeof(s_editor.path) - 1);
    while (offset < node->fileSize) {
        uint16_t chunk = (uint16_t)(node->fileSize - offset);
        int n;
        if (chunk > 128u) chunk = 128u;
        n = DrvFs_ReadFile(node, (char *)&s_editor.data[offset], chunk, offset);
        if (n <= 0) {
            Shell_Printf("vim: read failed at %lu\r\n", (unsigned long)offset);
            return -1;
        }
        offset += (uint32_t)n;
    }
    s_editor.length = offset;

    if (!Shell_BeginInputMode(editor_input, &s_editor)) {
        Shell_Print("vim: shell input is busy\r\n");
        return -1;
    }

    editor_print();
    Shell_Print("Commands: :p :a i :c :d N :w :q :q! :wq\r\n");
    editor_prompt();
    return 0;
}

static const ShellOpt_t vim_opts[] = {
    OPT("", "", "<file>", "Edit filesystem text file", cmd_vim),
    OPT_END()
};

DEFINE_MODULE(vim, "Edit filesystem text file", MOD_CAT_SYSTEM, vim_opts);

/*============================================================================
 * recv module - Chunked file receiver for wireless/serial upload
 *===========================================================================*/

#define RECV_DECODE_MAX  64u

static int recv_b64_value(char ch)
{
    if (ch >= 'A' && ch <= 'Z') return ch - 'A';
    if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
    if (ch >= '0' && ch <= '9') return ch - '0' + 52;
    if (ch == '+') return 62;
    if (ch == '/') return 63;
    return -1;
}

static int recv_b64_decode(const char *src, uint8_t *out, uint16_t outMax,
                           uint16_t *outLen)
{
    uint16_t written = 0u;

    if (!src || !out || !outLen) return -1;
    while (*src) {
        int v[4];
        uint8_t pad = 0u;
        uint8_t i;

        for (i = 0u; i < 4u; i++) {
            char ch = *src++;
            if (ch == '\0') return -2;
            if (ch == '=') {
                v[i] = 0;
                pad++;
            } else {
                if (pad > 0u) return -3;
                v[i] = recv_b64_value(ch);
                if (v[i] < 0) return -4;
            }
        }

        if (pad > 2u || written + 3u - pad > outMax) return -5;
        out[written++] = (uint8_t)((v[0] << 2) | (v[1] >> 4));
        if (pad < 2u) {
            out[written++] = (uint8_t)((v[1] << 4) | (v[2] >> 2));
        }
        if (pad < 1u) {
            out[written++] = (uint8_t)((v[2] << 6) | v[3]);
        }
        if (pad > 0u && *src != '\0') return -6;
    }

    *outLen = written;
    return 0;
}

static int recv_clear_path(const char *path)
{
    int ret = SdFs_WriteFile(path, NULL, 0u);
    if (ret != 0) {
        Shell_Printf("recv: clear %s failed (%d)\r\n", path, ret);
        return ret;
    }
    Shell_Printf("OK clear %s\r\n", path);
    return 0;
}

static int recv_write_block(const char *path, const char *offsetText,
                            const char *base64Text)
{
    uint8_t data[RECV_DECODE_MAX];
    uint16_t dataLen = 0u;
    uint32_t offset;
    char *end;
    int ret;

    offset = (uint32_t)strtoul(offsetText, &end, 10);
    if (end == offsetText || *end != '\0') {
        Shell_Print("recv: invalid offset\r\n");
        return -4;
    }
    ret = recv_b64_decode(base64Text, data, sizeof(data), &dataLen);
    if (ret != 0) {
        Shell_Printf("recv: invalid base64 (%d)\r\n", ret);
        return -5;
    }
    ret = SdFs_WriteFileAt(path, data, dataLen, offset);
    if (ret != 0) {
        Shell_Printf("recv: write %s @%lu failed (%d)\r\n",
                     path, (unsigned long)offset, ret);
        return ret;
    }
    Shell_Printf("OK block %lu %u\r\n", (unsigned long)offset,
                 (unsigned int)dataLen);
    return 0;
}

static int cmd_recv(int argc, char *argv[])
{
    if (argc < 2) {
        Shell_Print("Usage:\r\n");
        Shell_Print("  recv -c <path>\r\n");
        Shell_Print("  recv -b <path> <offset> <base64>\r\n");
        return -1;
    }

    if (strcmp(argv[0], "-c") == 0 || strcmp(argv[0], "clear") == 0) {
        return recv_clear_path(argv[1]);
    }

    if ((strcmp(argv[0], "-b") == 0 || strcmp(argv[0], "block") == 0) &&
        argc == 4) {
        return recv_write_block(argv[1], argv[2], argv[3]);
    }
    Shell_Print("recv: unknown option\r\n");
    return -2;
}

static int cmd_recv_clear(int argc, char *argv[])
{
    if (argc != 1) {
        Shell_Print("Usage: recv -c <path>\r\n");
        return -1;
    }
    return recv_clear_path(argv[0]);
}

static int cmd_recv_block(int argc, char *argv[])
{
    if (argc != 3) {
        Shell_Print("Usage: recv -b <path> <offset> <base64>\r\n");
        return -1;
    }
    return recv_write_block(argv[0], argv[1], argv[2]);
}

static const ShellOpt_t recv_opts[] = {
    OPT("", "", "-c <path> | -b <path> <offset> <base64>",
        "Receive file chunks over shell", cmd_recv),
    OPT("c", "clear", "<path>", "Create/truncate destination file", cmd_recv_clear),
    OPT("b", "block", "<path> <offset> <base64>", "Write one base64 chunk", cmd_recv_block),
    OPT_END()
};

DEFINE_MODULE(recv, "Receive file chunks", MOD_CAT_SYSTEM, recv_opts);

/*============================================================================
 * run module - Execute UTF-8 command script
 *===========================================================================*/

#if 0 /* Moved to command_parser/command_parser.c (cooperative implementation). */
#define SCRIPT_READ_BUF_SIZE    64u
#define SCRIPT_MAX_DEPTH        2u

typedef struct {
    uint32_t codepoint;
    uint32_t minCodepoint;
    uint8_t  remaining;
} Utf8Parser_t;

static uint8_t s_scriptDepth = 0;

static void utf8_parser_reset(Utf8Parser_t *parser)
{
    parser->codepoint = 0;
    parser->minCodepoint = 0;
    parser->remaining = 0;
}

static int utf8_parser_feed(Utf8Parser_t *parser, uint8_t byte)
{
    if (parser->remaining == 0) {
        if (byte < 0x80u) {
            return 1;
        }
        if (byte >= 0xC2u && byte <= 0xDFu) {
            parser->codepoint = (uint32_t)(byte & 0x1Fu);
            parser->minCodepoint = 0x80u;
            parser->remaining = 1;
            return 0;
        }
        if (byte >= 0xE0u && byte <= 0xEFu) {
            parser->codepoint = (uint32_t)(byte & 0x0Fu);
            parser->minCodepoint = 0x800u;
            parser->remaining = 2;
            return 0;
        }
        if (byte >= 0xF0u && byte <= 0xF4u) {
            parser->codepoint = (uint32_t)(byte & 0x07u);
            parser->minCodepoint = 0x10000u;
            parser->remaining = 3;
            return 0;
        }
        return -1;
    }

    if ((byte & 0xC0u) != 0x80u) {
        return -1;
    }

    parser->codepoint = (parser->codepoint << 6) | (uint32_t)(byte & 0x3Fu);
    parser->remaining--;
    if (parser->remaining != 0) {
        return 0;
    }

    if (parser->codepoint < parser->minCodepoint) {
        return -1;
    }
    if (parser->codepoint >= 0xD800u && parser->codepoint <= 0xDFFFu) {
        return -1;
    }
    if (parser->codepoint > 0x10FFFFu) {
        return -1;
    }

    return 1;
}

static int script_execute_line(char *line, uint16_t *lineLen, uint32_t lineNo)
{
    char *cmd;
    char *end;
    int ret;

    line[*lineLen] = '\0';
    cmd = line;
    while (*cmd == ' ' || *cmd == '\t') {
        cmd++;
    }

    end = cmd + strlen(cmd);
    while (end > cmd && (end[-1] == ' ' || end[-1] == '\t')) {
        end--;
    }
    *end = '\0';

    *lineLen = 0;
    if (cmd[0] == '\0') {
        return 0;
    }

    ret = Shell_ExecuteLine(cmd);
    if (ret != 0) {
        Shell_Printf("run: line %lu failed (%d): %s\r\n",
                     (unsigned long)lineNo, ret, cmd);
        return ret;
    }
    return 0;
}

static int cmd_run(int argc, char *argv[])
{
    FsNode_t *node;
    Utf8Parser_t parser;
    uint8_t buf[SCRIPT_READ_BUF_SIZE];
    char line[SHELL_CMD_MAX_LEN];
    uint32_t offset = 0;
    uint32_t lineNo = 1;
    uint16_t lineLen = 0;
    bool skipLf = FALSE;
    int ret = 0;

    if (argc < 1) {
        Shell_Print("run: missing script file\r\n");
        Shell_Print("Usage: run <utf8-file>\r\n");
        return -1;
    }

    if (s_scriptDepth >= SCRIPT_MAX_DEPTH) {
        Shell_Print("run: nested script limit reached\r\n");
        return -2;
    }

    node = DrvFs_FindNode(argv[0]);
    if (!node) {
        Shell_Printf("run: %s: No such file\r\n", argv[0]);
        return -3;
    }
    if (node->type != FS_NODE_FILE) {
        Shell_Printf("run: %s: Not a file\r\n", argv[0]);
        return -4;
    }

    utf8_parser_reset(&parser);
    s_scriptDepth++;

    while (offset < node->fileSize) {
        uint16_t toRead = (uint16_t)sizeof(buf);
        int n;
        int i;
        int start = 0;

        if ((node->fileSize - offset) < toRead) {
            toRead = (uint16_t)(node->fileSize - offset);
        }

        n = DrvFs_ReadFile(node, (char *)buf, toRead, offset);
        if (n < 0) {
            Shell_Printf("run: read error at offset %lu\r\n", (unsigned long)offset);
            ret = -5;
            break;
        }
        if (n == 0) {
            break;
        }

        if (offset == 0 && n >= 3 &&
            buf[0] == 0xEFu && buf[1] == 0xBBu && buf[2] == 0xBFu) {
            start = 3;
        }

        for (i = start; i < n; i++) {
            uint8_t byte = buf[i];
            int utf8 = utf8_parser_feed(&parser, byte);

            if (utf8 < 0) {
                Shell_Printf("run: invalid UTF-8 at line %lu, offset %lu\r\n",
                             (unsigned long)lineNo,
                             (unsigned long)(offset + (uint32_t)i));
                ret = -6;
                break;
            }

            if (byte == '\r' || byte == '\n') {
                if (byte == '\n' && skipLf) {
                    skipLf = FALSE;
                    continue;
                }

                ret = script_execute_line(line, &lineLen, lineNo);
                if (ret != 0) {
                    break;
                }
                lineNo++;
                skipLf = (byte == '\r') ? TRUE : FALSE;
                continue;
            }

            skipLf = FALSE;
            if (lineLen >= (uint16_t)(sizeof(line) - 1)) {
                Shell_Printf("run: line %lu too long\r\n", (unsigned long)lineNo);
                ret = -7;
                break;
            }
            line[lineLen++] = (char)byte;
        }

        if (ret != 0) {
            break;
        }
        offset += (uint32_t)n;
    }

    if (ret == 0 && parser.remaining != 0) {
        Shell_Printf("run: truncated UTF-8 sequence at line %lu\r\n",
                     (unsigned long)lineNo);
        ret = -8;
    }

    if (ret == 0 && lineLen > 0) {
        ret = script_execute_line(line, &lineLen, lineNo);
    }

    s_scriptDepth--;

    if (ret == 0) {
        Shell_Printf("run: %s done\r\n", argv[0]);
    }
    return ret;
}

static const ShellOpt_t run_opts[] = {
    OPT("", "", "<utf8-file>", "Execute UTF-8 command script", cmd_run),
    OPT_END()
};

DEFINE_MODULE(run, "Execute command script", MOD_CAT_SYSTEM, run_opts);
#endif

/*
 * echo命令 - 写入参数值
 * 用法: echo <value> > <parameter>
 * 简化用法: echo <parameter> <value>
 */
static int cmd_echo(int argc, char *argv[])
{
    FsNode_t *node;
    const char *value;
    const char *path;
    int ret;
    int redirect_idx = -1;
    int i;

    if (argc < 2) {
        Shell_Print("echo: missing operand\r\n");
        Shell_Print("Usage: echo <parameter> <value>\r\n");
        Shell_Print("   or: echo <value> > <parameter>\r\n");
        Shell_Print("Example: echo threshold -20\r\n");
        Shell_Print("     or: echo -20 > threshold\r\n");
        return -1;
    }

    /* 检查是否有重定向符号 > */
    for (i = 0; i < argc; i++) {
        if (strcmp(argv[i], ">") == 0) {
            redirect_idx = i;
            break;
        }
    }

    if (redirect_idx > 0 && redirect_idx < argc - 1) {
        /* 格式: echo <value> > <parameter> */
        value = argv[0];  /* 第一个参数是值 */
        path = argv[redirect_idx + 1];  /* > 后面是路径 */
    } else {
        /* 简化格式: echo <parameter> <value> */
        path = argv[0];   /* 第一个参数是路径 */
        value = argv[1];  /* 第二个参数是值 */
    }

    /* 查找节点 */
    node = DrvFs_FindNode(path);
    if (!node) {
        Shell_Printf("echo: %s: No such file or directory\r\n", path);
        return -1;
    }

    if (node->type != FS_NODE_PARAM) {
        Shell_Printf("echo: %s: Not a parameter file\r\n", path);
        return -1;
    }

    /* 写入参数 */
    ret = banux_write(path, value, (uint32_t)strlen(value));
    if (ret < 0) {
        if (ret == BANUX_IO_ERR_NOT_SUPPORTED) {
            Shell_Printf("echo: %s: Read-only parameter\r\n", path);
        } else {
            Shell_Printf("echo: %s: Write error\r\n", path);
        }
        return -1;
    }

    Shell_Printf("OK\r\n");
    return 0;
}

static const ShellOpt_t echo_opts[] = {
    OPT("", "", "<param> <value>", "Write parameter value", cmd_echo),
    OPT_END()
};

DEFINE_MODULE(echo, "Write to file", MOD_CAT_SYSTEM, echo_opts);

/*----------------------------------------------------------------------------
 * 递归打印VFS树结构
 *----------------------------------------------------------------------------*/
static void print_vfs_tree(VfsNode_t *node, int depth, int isLast)
{
    char prefix[32] = "";
    int i;
    int childCount;

    for (i = 0; i < depth; i++) {
        strcat(prefix, (i == depth - 1 && isLast) ? "   " : "|  ");
    }
    if (depth > 0) {
        Shell_Print(prefix);
        Shell_Print("+-- ");
    }
    Shell_Print(node->name);
    if (node->type == VFS_NODE_DIR) {
        /* 动态目录 (如 /sd 挂载点) 先懒加载子节点再显示 */
        Vfs_RefreshDir(node);
        Shell_Print("/");
    } else if (node->type == VFS_NODE_FILE) {
        Shell_Printf("  (%lu bytes)", (unsigned long)node->fileSize);
    }
    Shell_Print("\r\n");

    /* 目录/设备节点继续递归 (FILE/PARAM/CMD 为叶子) */
    childCount = node->childCount;
    if ((node->type == VFS_NODE_DIR || node->type == VFS_NODE_DEV) && childCount > 0) {
        for (i = 0; i < childCount; i++) {
            print_vfs_tree(node->children[i], depth + 1, i == childCount - 1);
        }
    }
}

static int cmd_tree(int argc, char *argv[])
{
    (void)argc; (void)argv;
    VfsNode_t *root = Vfs_GetRoot();
    if (!root) {
        Shell_Print("VFS not initialized!\r\n");
        return -1;
    }
    print_vfs_tree(root, 0, 1);
    Shell_Print("\r\n");
    return 0;
}

static const ShellOpt_t tree_opts[] = {
    OPT("", "", NULL, "Display driver tree", cmd_tree),
    OPT_END()
};

DEFINE_MODULE(tree, "Display directory tree", MOD_CAT_SYSTEM, tree_opts);

static int cmd_drivers(int argc, char *argv[])
{
    int count;
    DrvDevice_t **devices;
    int i;

    (void)argc; (void)argv;

    devices = DrvDevice_GetList(&count);

    Shell_Print("\r\nRegistered Drivers:\r\n");
    Shell_Print("+----------+----------+-----------+\r\n");
    Shell_Print("| Name     | Bus      | Status    |\r\n");
    Shell_Print("+----------+----------+-----------+\r\n");

    for (i = 0; i < count; i++) {
        const char *bus_name;

        switch (devices[i]->bus) {
            case DRV_BUS_SPI:   bus_name = "SPI";   break;
            case DRV_BUS_I2C:   bus_name = "I2C";   break;
            case DRV_BUS_I2S:   bus_name = "I2S";   break;
            case DRV_BUS_SDIO:  bus_name = "SDIO";  break;
            case DRV_BUS_GPIO:  bus_name = "GPIO";  break;
            case DRV_BUS_UART:  bus_name = "UART";  break;
            case DRV_BUS_POWER: bus_name = "POWER"; break;
            case DRV_BUS_USB:   bus_name = "USB";   break;
            default:            bus_name = "UNKNOWN"; break;
        }

        Shell_Printf("| %-8s | %-8s | %-9s |\r\n",
                     devices[i]->name,
                     bus_name,
                     devices[i]->isRegistered ? "OK" : "FAIL");
    }

    Shell_Print("+----------+----------+-----------+\r\n");
    Shell_Printf("Total: %d drivers\r\n\r\n", count);
    return 0;
}

static const ShellOpt_t drivers_opts[] = {
    OPT("", "", NULL, "List all registered drivers", cmd_drivers),
    OPT_END()
};

DEFINE_MODULE(drivers, "List device drivers", MOD_CAT_SYSTEM, drivers_opts);

/*============================================================================
 * boot 模块 - 重启到 Bootloader 烧录模式
 *   boot  通过 app_bl 写 enter_boot 魔数到配置区后复位，进入 Bootloader
 *===========================================================================*/
static int boot_enter(int argc, char *argv[])
{
    (void)argc; (void)argv;

    Shell_Printf("[BOOT] Writing enter_boot flag and rebooting to bootloader ...\r\n");

    /* Delay to ensure message is sent */
    {
        volatile uint32_t delay;
        for (delay = 0; delay < 200000; delay++) { ; }
    }

    app_bl_enter_boot();   /* 成功时内部复位，不返回 */
    return 0;  /* Never reached */
}

static const ShellOpt_t boot_opts[] = {
    OPT("", "", NULL, "Reboot into bootloader for firmware upgrade", boot_enter),
    OPT_END()
};

DEFINE_MODULE(boot, "Reboot to Bootloader", MOD_CAT_SYSTEM, boot_opts);

/*============================================================================
 * Module registration
 *===========================================================================*/
void Shell_RegisterAllModules(void)
{
    REGISTER_MODULE(sys);
    REGISTER_MODULE(banux);
#if BG_EVENT_EN
    REGISTER_MODULE(event);
#endif

#if VFS_EN
    /* 文件系统导航命令 */
    REGISTER_MODULE(ls);
    REGISTER_MODULE(pwd);
    REGISTER_MODULE(cd);
    REGISTER_MODULE(cat);
    REGISTER_MODULE(touch);
    REGISTER_MODULE(mkdir);
    REGISTER_MODULE(rm);
    REGISTER_MODULE(vim);
    REGISTER_MODULE(recv);
    CommandParser_RegisterCommands();
    REGISTER_MODULE(echo);    /* 写入参数值 */
    REGISTER_MODULE(tree);
#endif /* VFS_EN */

    REGISTER_MODULE(drivers);
    REGISTER_MODULE(boot);
}
