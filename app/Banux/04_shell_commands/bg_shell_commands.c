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
#include "app_bl.h"     /* APP 侧 boot 联动：app_bl_enter_boot() */

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
    Shell_Print("System rebooting...\r\n");
    NVIC_SystemReset();
    return 0;
}

static const ShellOpt_t sys_opts[] = {
    OPT("i", "info",    NULL,   "Show system info",     sys_info),
    OPT("m", "mem",     NULL,   "Show memory status",   sys_mem),
    OPT("b", "reboot",  NULL,   "Reboot system",        sys_reboot),
    OPT_END()
};

DEFINE_MODULE(sys, "System information", MOD_CAT_SYSTEM, sys_opts);

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

    ret = DrvFs_ReadParam(node, buf, sizeof(buf));
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
    ret = DrvFs_WriteParam(node, value);
    if (ret < 0) {
        if (ret == -2) {
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

#if VFS_EN
    /* 文件系统导航命令 */
    REGISTER_MODULE(ls);
    REGISTER_MODULE(pwd);
    REGISTER_MODULE(cd);
    REGISTER_MODULE(cat);
    REGISTER_MODULE(echo);    /* 写入参数值 */
    REGISTER_MODULE(tree);
#endif /* VFS_EN */

    REGISTER_MODULE(drivers);
    REGISTER_MODULE(boot);
}
