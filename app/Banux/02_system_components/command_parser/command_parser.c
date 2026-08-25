/**
 * @file command_parser.c
 * @brief Nonblocking UTF-8 command script execution with millisecond delays.
 */
#include <stdlib.h>
#include <string.h>
#include "banux_config.h"
#include "banux_component.h"
#include "bg_shell.h"
#include "drv_fs.h"
#include "drv_timer1ms.h"
#include "command_parser.h"

BANUX_COMPONENT_DEFINE(g_banux_component_command_parser,
                       "command_parser", "1.0.0",
                       BANUX_COMPONENT_SYSTEM, COMMAND_PARSER_EN,
                       "nonblocking UTF-8 command parser and executor");

#if COMMAND_PARSER_EN
#define SCRIPT_MAX_DEPTH  2u
#define SCRIPT_READ_SIZE  64u

typedef struct {
    uint32_t codepoint;
    uint32_t minCodepoint;
    uint8_t remaining;
} Utf8Parser_t;

typedef struct {
    uint32_t fileSize;
    uint32_t offset;
    uint32_t lineNo;
    uint16_t lineLen;
    uint8_t skipLf;
    uint8_t readPos;
    uint8_t readLen;
    Utf8Parser_t utf8;
    uint8_t readBuf[SCRIPT_READ_SIZE];
    char path[VFS_MAX_PATH_LEN];
    char line[SHELL_CMD_MAX_LEN];
} ScriptContext_t;

static ScriptContext_t s_scripts[SCRIPT_MAX_DEPTH];
static uint8_t s_depth;
static uint8_t s_waiting;
static uint8_t s_executing;
static uint32_t s_deadline;

static void parser_abort(void)
{
    s_depth = 0u;
    s_waiting = 0u;
    s_executing = 0u;
    Shell_ShowPrompt();
}

static void utf8_reset(Utf8Parser_t *parser)
{
    parser->codepoint = 0u;
    parser->minCodepoint = 0u;
    parser->remaining = 0u;
}

static int utf8_feed(Utf8Parser_t *parser, uint8_t byte)
{
    if (parser->remaining == 0u) {
        if (byte < 0x80u) return 1;
        if (byte >= 0xC2u && byte <= 0xDFu) {
            parser->codepoint = (uint32_t)(byte & 0x1Fu);
            parser->minCodepoint = 0x80u;
            parser->remaining = 1u;
            return 0;
        }
        if (byte >= 0xE0u && byte <= 0xEFu) {
            parser->codepoint = (uint32_t)(byte & 0x0Fu);
            parser->minCodepoint = 0x800u;
            parser->remaining = 2u;
            return 0;
        }
        if (byte >= 0xF0u && byte <= 0xF4u) {
            parser->codepoint = (uint32_t)(byte & 0x07u);
            parser->minCodepoint = 0x10000u;
            parser->remaining = 3u;
            return 0;
        }
        return -1;
    }

    if ((byte & 0xC0u) != 0x80u) return -1;
    parser->codepoint = (parser->codepoint << 6) | (uint32_t)(byte & 0x3Fu);
    parser->remaining--;
    if (parser->remaining != 0u) return 0;
    if (parser->codepoint < parser->minCodepoint ||
        (parser->codepoint >= 0xD800u && parser->codepoint <= 0xDFFFu) ||
        parser->codepoint > 0x10FFFFu) {
        return -1;
    }
    return 1;
}

static int execute_line(ScriptContext_t *script)
{
    char *command;
    char *end;
    int ret;

    script->line[script->lineLen] = '\0';
    command = script->line;
    while (*command == ' ' || *command == '\t') command++;
    end = command + strlen(command);
    while (end > command && (end[-1] == ' ' || end[-1] == '\t')) end--;
    *end = '\0';
    script->lineLen = 0u;
    if (command[0] == '\0') return 0;

    s_executing = 1u;
    ret = Shell_ExecuteLine(command);
    s_executing = 0u;
    if (ret != 0) {
        Shell_Printf("run: line %lu failed (%d): %s\r\n",
                     (unsigned long)script->lineNo, ret, command);
        parser_abort();
        return -1;
    }
    return 1;
}

static void script_complete(void)
{
    ScriptContext_t *script = &s_scripts[s_depth - 1u];
    Shell_Printf("run: %s done\r\n", script->path);
    s_depth--;
    if (s_depth == 0u) Shell_ShowPrompt();
}

static int script_read_byte(ScriptContext_t *script, uint8_t *byte)
{
    if (script->readPos >= script->readLen) {
        FsNode_t *node = DrvFs_FindNode(script->path);
        uint32_t remaining;
        uint16_t request;
        int count;
        if (!node || node->type != FS_NODE_FILE) return -1;
        script->fileSize = node->fileSize;
        if (script->offset >= script->fileSize) return -1;
        remaining = script->fileSize - script->offset;
        request = remaining < SCRIPT_READ_SIZE
                ? (uint16_t)remaining : SCRIPT_READ_SIZE;
        count = DrvFs_ReadFile(node, (char *)script->readBuf,
                               request, script->offset);
        if (count <= 0) return -1;
        script->readPos = 0u;
        script->readLen = (uint8_t)count;
    }
    *byte = script->readBuf[script->readPos++];
    script->offset++;
    return 0;
}

void CommandParser_Init(void)
{
    memset(s_scripts, 0, sizeof(s_scripts));
    s_depth = 0u;
    s_waiting = 0u;
    s_executing = 0u;
    BanuxComponent_SetState("command_parser", BANUX_COMPONENT_READY);
}

int CommandParser_Start(const char *path)
{
    ScriptContext_t *script;
    uint8_t bom[3];
    int count;

    if (!path || path[0] == '\0') return -1;
    if (s_depth != 0u && !s_executing) {
        Shell_Print("run: another script is already running\r\n");
        return -5;
    }
    if (s_depth >= SCRIPT_MAX_DEPTH) {
        Shell_Print("run: nested script limit reached\r\n");
        return -2;
    }

    script = &s_scripts[s_depth];
    memset(script, 0, sizeof(*script));
    {
        FsNode_t *node = DrvFs_FindNode(path);
        if (!node) {
            Shell_Printf("run: %s: No such file\r\n", path);
            return -3;
        }
        if (node->type != FS_NODE_FILE) {
            Shell_Printf("run: %s: Not a file\r\n", path);
            return -4;
        }
        script->fileSize = node->fileSize;
    }

    strncpy(script->path, path, sizeof(script->path) - 1u);
    script->lineNo = 1u;
    utf8_reset(&script->utf8);
    if (script->fileSize >= 3u) {
        FsNode_t *node = DrvFs_FindNode(path);
        count = node ? DrvFs_ReadFile(node, (char *)bom, sizeof(bom), 0u) : -1;
        if (count == 3 && bom[0] == 0xEFu && bom[1] == 0xBBu && bom[2] == 0xBFu) {
            script->offset = 3u;
        }
    }
    s_depth++;
    Shell_Printf("run: %s started\r\n", path);
    return 0;
}

int CommandParser_Delay(uint32_t milliseconds)
{
    if (s_depth == 0u || !s_executing || milliseconds > 0x7FFFFFFFu) return -1;
    s_deadline = DrvTimer1ms_Now() + milliseconds;
    s_waiting = milliseconds != 0u ? 1u : 0u;
    return 0;
}

int CommandParser_IsRunning(void)
{
    return s_depth != 0u;
}

void CommandParser_Process(void)
{
    ScriptContext_t *script;

    if (s_depth == 0u) return;
    if (s_waiting) {
        if (!DrvTimer1ms_Expired(s_deadline)) return;
        s_waiting = 0u;
    }

    script = &s_scripts[s_depth - 1u];
    while (script->offset < script->fileSize) {
        uint8_t byte;
        int result;
        if (script_read_byte(script, &byte) != 0) {
            Shell_Printf("run: read error at offset %lu\r\n",
                         (unsigned long)script->offset);
            parser_abort();
            return;
        }

        result = utf8_feed(&script->utf8, byte);
        if (result < 0) {
            Shell_Printf("run: invalid UTF-8 at line %lu, offset %lu\r\n",
                         (unsigned long)script->lineNo,
                         (unsigned long)(script->offset - 1u));
            parser_abort();
            return;
        }

        if (byte == '\r' || byte == '\n') {
            if (byte == '\n' && script->skipLf) {
                script->skipLf = 0u;
                continue;
            }
            script->skipLf = byte == '\r' ? 1u : 0u;
            result = execute_line(script);
            script->lineNo++;
            if (result != 0) return;
            continue;
        }

        script->skipLf = 0u;
        if (script->lineLen >= (uint16_t)(sizeof(script->line) - 1u)) {
            Shell_Printf("run: line %lu too long\r\n",
                         (unsigned long)script->lineNo);
            parser_abort();
            return;
        }
        script->line[script->lineLen++] = (char)byte;
    }

    if (script->utf8.remaining != 0u) {
        Shell_Printf("run: truncated UTF-8 sequence at line %lu\r\n",
                     (unsigned long)script->lineNo);
        parser_abort();
        return;
    }
    if (script->lineLen != 0u) {
        if (execute_line(script) != 0) return;
    }
    script_complete();
}

static int cmd_run(int argc, char *argv[])
{
    if (argc < 1) {
        Shell_Print("Usage: run <utf8-file>\r\n");
        return -1;
    }
    return CommandParser_Start(argv[0]);
}

static int cmd_delay(int argc, char *argv[])
{
    char *end;
    unsigned long delay;
    if (argc < 1) {
        Shell_Print("Usage: delay <milliseconds>\r\n");
        return -1;
    }
    delay = strtoul(argv[0], &end, 10);
    if (argv[0][0] == '\0' || *end != '\0' || delay > 0x7FFFFFFFul) {
        Shell_Print("delay: invalid millisecond value\r\n");
        return -2;
    }
    if (CommandParser_Delay((uint32_t)delay) != 0) {
        Shell_Print("delay: only valid while a script is running\r\n");
        return -3;
    }
    return 0;
}

static const ShellOpt_t run_opts[] = {
    OPT("", "", "<utf8-file>", "Start a UTF-8 command script", cmd_run),
    OPT_END()
};

static const ShellOpt_t delay_opts[] = {
    OPT("", "", "<milliseconds>", "Delay the next script command", cmd_delay),
    OPT_END()
};

DEFINE_MODULE(run, "Run command script asynchronously", MOD_CAT_SYSTEM, run_opts);
DEFINE_MODULE(delay, "Nonblocking script delay", MOD_CAT_SYSTEM, delay_opts);

void CommandParser_RegisterCommands(void)
{
    REGISTER_MODULE(run);
    REGISTER_MODULE(delay);
}
#else
void CommandParser_Init(void) {}
void CommandParser_RegisterCommands(void) {}
void CommandParser_Process(void) {}
int CommandParser_Start(const char *path) { (void)path; return -1; }
int CommandParser_Delay(uint32_t milliseconds) { (void)milliseconds; return -1; }
int CommandParser_IsRunning(void) { return 0; }
#endif
