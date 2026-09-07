#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gcode.h"
#include "banux_config.h"
#include "banux_component.h"
#include "banux_io.h"
#include "bg_event.h"
#include "bg_shell.h"
#include "drv_stepper.h"
#include "app_bl.h"   /* app_bl_poll：阻塞执行期主动泵 UART 环形缓冲到 Shell 镜像 */

#define GCODE_LINE_MAX       96u
#define GCODE_DEFAULT_FEED   1200000L
#define GCODE_GROUP_PATH     "/driver/gpio/stepper_group"
#define GCODE_AMAX_STEPS_PER_S2 40000u  /* 联动加速度 steps/s^2（梯形加减速，先统一默认，实测后按轴调） */

/* 执行期控制字符（上位机按钮发送）：阻塞期间 shell 不解析命令，只按原始字节识别。
 * 三者均非 ENTER_BOOT 帧头(0xAA)、也非 "boot" 文本，不会误触发 app_bl 升级复位。 */
#define GCODE_CTRL_STOP     ((uint8_t)'!')  /* 停止（中止执行、停脉冲、断使能） */
#define GCODE_CTRL_PAUSE    ((uint8_t)'%')  /* 暂停（停在段边界，喷嘴已抬到安全 Z） */
#define GCODE_CTRL_RESUME   ((uint8_t)'~')  /* 恢复 */
#define GCODE_ACT_STOP      1
#define GCODE_ACT_PAUSE     2
#define GCODE_ACT_RESUME    4

typedef struct {
    int codeType;
    int code;
    int32_t value[DRV_STEPPER_COUNT];
    uint8_t hasAxis[DRV_STEPPER_COUNT];
    int32_t feed;
    uint8_t hasFeed;
} ParsedGcode_t;

static GcodeState_t s_state;
static const int32_t s_stepsPerMm[DRV_STEPPER_COUNT] = {
    GCODE_X_STEPS_PER_MM, GCODE_Y_STEPS_PER_MM,
    GCODE_Z_STEPS_PER_MM, GCODE_E_STEPS_PER_MM
};
static const char *const s_axisPaths[DRV_STEPPER_COUNT] = {
    "/driver/gpio/stepper_x", "/driver/gpio/stepper_y",
    "/driver/gpio/stepper_z", "/driver/gpio/stepper_e"
};
static uint32_t s_segIdx = 0u;

/* 泵入 UART 环形缓冲并读取原始字节，返回检测到的控制动作位（STOP/PAUSE/RESUME）。
 * 关键：Gcode_ExecuteFile 是阻塞执行，期间主循环 Banux_Process 停摆、app_bl_poll 不再
 * 被调用，上位机发来的控制字符会滞留在 usart.c 环形缓冲里；而 Shell_RecvRaw 读的是
 * app_bl 的镜像缓冲（shell1/shell3），镜像只有在 app_bl_poll 搬运后才有数据 —— 这正是
 * “急停不管用”的根因。故此处每个段标记都先主动 app_bl_poll 把字节搬进镜像再读。 */
static int gcode_poll_control(void)
{
    uint8_t rbuf[16];
    uint16_t got;
    uint16_t i;
    int action = 0;

    app_bl_poll();
    got = Shell_RecvRaw(rbuf, sizeof(rbuf));
    for (i = 0u; i < got; i++) {
        if (rbuf[i] == GCODE_CTRL_STOP)         action |= GCODE_ACT_STOP;
        else if (rbuf[i] == GCODE_CTRL_PAUSE)   action |= GCODE_ACT_PAUSE;
        else if (rbuf[i] == GCODE_CTRL_RESUME)  action |= GCODE_ACT_RESUME;
    }
    return action;
}

/* 停脉冲并断使能（停止/中止共用）。 */
static void gcode_halt(void)
{
    (void)banux_ioctl(GCODE_GROUP_PATH, DRV_STEPPER_IOCTL_STOP, NULL);
    s_state.motorsEnabled = 0u;
}

static int32_t div_round64(int64_t numerator, int32_t denominator)
{
    if (numerator >= 0) return (int32_t)((numerator + denominator / 2) / denominator);
    return (int32_t)((numerator - denominator / 2) / denominator);
}

static int parse_fixed(const char **cursor, int32_t *value)
{
    const char *p = *cursor;
    int sign = 1;
    int digits = 0;
    int decimals = 0;
    int32_t whole = 0;
    int32_t fraction = 0;

    if (*p == '+' || *p == '-') {
        if (*p++ == '-') sign = -1;
    }
    while (*p >= '0' && *p <= '9') {
        digits++;
        if (whole > 200000L) return -1;
        whole = whole * 10 + (*p++ - '0');
    }
    if (whole > 200000L) return -1;
    if (*p == '.') {
        p++;
        while (*p >= '0' && *p <= '9') {
            if (decimals < 3) fraction = fraction * 10 + (*p - '0');
            decimals++;
            digits++;
            p++;
        }
    }
    if (!digits) return -1;
    while (decimals < 3) {
        fraction *= 10;
        decimals++;
    }
    *value = sign * (whole * 1000L + fraction);
    *cursor = p;
    return 0;
}

static int verify_checksum(const char *line)
{
    const char *star = strchr(line, '*');
    const char *p;
    unsigned int checksum = 0u;
    int supplied = 0;

    if (!star) return 0;
    for (p = line; p < star; p++) checksum ^= (uint8_t)*p;
    p = star + 1;
    if (!isdigit((unsigned char)*p)) return -1;
    while (isdigit((unsigned char)*p)) supplied = supplied * 10 + (*p++ - '0');
    return checksum == (unsigned int)supplied ? 0 : -1;
}

static int parse_line(const char *line, ParsedGcode_t *parsed)
{
    const char *p = line;

    memset(parsed, 0, sizeof(*parsed));
    parsed->codeType = 0;
    if (verify_checksum(line) != 0) return GCODE_ERR_CHECKSUM;

    while (*p) {
        int letter;
        int32_t value;
        while (*p == ' ' || *p == '\t') p++;
        if (!*p || *p == ';' || *p == '*') break;
        if (*p == '(') {
            while (*p && *p != ')') p++;
            if (*p == ')') p++;
            continue;
        }
        if (!isalpha((unsigned char)*p)) return GCODE_ERR_INVALID;
        letter = toupper((unsigned char)*p++);
        if (parse_fixed(&p, &value) != 0) return GCODE_ERR_INVALID;
        switch (letter) {
            case 'N': break;
            case 'G':
            case 'M':
                if (parsed->codeType) return GCODE_ERR_INVALID;
                if ((value % 1000L) != 0) return GCODE_ERR_UNSUPPORTED;
                parsed->codeType = letter;
                parsed->code = (int)(value / 1000L);
                break;
            case 'X': parsed->hasAxis[0] = 1u; parsed->value[0] = value; break;
            case 'Y': parsed->hasAxis[1] = 1u; parsed->value[1] = value; break;
            case 'Z': parsed->hasAxis[2] = 1u; parsed->value[2] = value; break;
            case 'E': parsed->hasAxis[3] = 1u; parsed->value[3] = value; break;
            case 'F': parsed->hasFeed = 1u; parsed->feed = value; break;
            default: return GCODE_ERR_UNKNOWN_WORD;
        }
    }
    return GCODE_OK;
}

static int refresh_position(void)
{
    uint32_t axis;
    for (axis = 0u; axis < DRV_STEPPER_COUNT; axis++) {
        DrvStepperStatus_t status;
        if (banux_read(s_axisPaths[axis], &status, sizeof(status)) < 0) return -1;
        s_state.positionMilliMm[axis] = div_round64((int64_t)status.position * 1000L,
                                                    s_stepsPerMm[axis]);
    }
    return 0;
}

static int execute_move(const ParsedGcode_t *parsed)
{
    DrvStepperMoveCommand_t command;
    int32_t target[DRV_STEPPER_COUNT];
    int32_t maxDistance = 0;
    uint32_t maxSteps = 0u;
    uint32_t axis;
    uint64_t durationUs;

    memset(&command, 0, sizeof(command));
    if (parsed->hasFeed) {
        if (parsed->feed <= 0) return GCODE_ERR_INVALID;
        s_state.feedMilliMmPerMin = parsed->feed;
    }
    for (axis = 0u; axis < DRV_STEPPER_COUNT; axis++) {
        int32_t delta;
        uint32_t count;
        target[axis] = parsed->hasAxis[axis]
                     ? (s_state.absoluteMode ? parsed->value[axis]
                                             : s_state.positionMilliMm[axis] + parsed->value[axis])
                     : s_state.positionMilliMm[axis];
        delta = target[axis] - s_state.positionMilliMm[axis];
        command.steps[axis] = div_round64((int64_t)delta * s_stepsPerMm[axis], 1000L);
        count = (uint32_t)(command.steps[axis] < 0
                        ? -command.steps[axis] : command.steps[axis]);
        if (count > maxSteps) maxSteps = count;
        if (delta < 0) delta = -delta;
        if (delta > maxDistance) maxDistance = delta;
    }
    if (!maxSteps) return GCODE_OK;
    /* 由 feed 与主轴位移估算主轴最大速度(steps/s)：durationUs = maxDistance*60e6/feed(milli)，
     * vMax = maxSteps*1e6/durationUs。加速度用统一默认；驱动侧 stepper_move_group 据此做
     * 梯形加减速多轴联动（不再受 5 秒时长上限约束，长移动不会被拒为 -5）。 */
    durationUs = ((uint64_t)(uint32_t)maxDistance * 60000000ULL) /
                 (uint32_t)s_state.feedMilliMmPerMin;
    if (durationUs == 0u) durationUs = 1u;
    command.vMaxStepsPerSec = (uint32_t)(((uint64_t)maxSteps * 1000000ULL) / durationUs);
    command.aMaxStepsPerSec2 = GCODE_AMAX_STEPS_PER_S2;
    command.pulseUs = 0u;
    if (banux_write(GCODE_GROUP_PATH, &command, sizeof(command)) < 0) {
        return GCODE_ERR_DRIVER;
    }
    return refresh_position() == 0 ? GCODE_OK : GCODE_ERR_DRIVER;
}

static int execute_parsed(const ParsedGcode_t *parsed)
{
    uint32_t axis;
    int enabled;

    if (!parsed->codeType) return GCODE_OK;
    if (parsed->codeType == 'G') {
        switch (parsed->code) {
            case 0:
            case 1: return execute_move(parsed);
            case 90: s_state.absoluteMode = 1u; return GCODE_OK;
            case 91: s_state.absoluteMode = 0u; return GCODE_OK;
            case 92:
                for (axis = 0u; axis < DRV_STEPPER_COUNT; axis++) {
                    if (parsed->hasAxis[axis]) {
                        int32_t steps = div_round64((int64_t)parsed->value[axis] *
                                                    s_stepsPerMm[axis], 1000L);
                        if (banux_ioctl(s_axisPaths[axis],
                                        DRV_STEPPER_IOCTL_POSITION, &steps) != 0) {
                            return GCODE_ERR_DRIVER;
                        }
                    }
                }
                return refresh_position() == 0 ? GCODE_OK : GCODE_ERR_DRIVER;
            default: return GCODE_ERR_UNSUPPORTED;
        }
    }
    switch (parsed->code) {
        case 17:
            enabled = 1;
            if (banux_ioctl(GCODE_GROUP_PATH, DRV_STEPPER_IOCTL_ENABLE, &enabled) != 0)
                return GCODE_ERR_DRIVER;
            s_state.motorsEnabled = 1u;
            return GCODE_OK;
        case 18:
        case 84:
            enabled = 0;
            if (banux_ioctl(GCODE_GROUP_PATH, DRV_STEPPER_IOCTL_ENABLE, &enabled) != 0)
                return GCODE_ERR_DRIVER;
            s_state.motorsEnabled = 0u;
            return GCODE_OK;
        case 114:
            return refresh_position() == 0 ? GCODE_OK : GCODE_ERR_DRIVER;
        default: return GCODE_ERR_UNSUPPORTED;
    }
}

int Gcode_ExecuteLine(const char *line)
{
    ParsedGcode_t parsed;
    int result;

    if (!line) return GCODE_ERR_INVALID;
    BG_EVT_PUB_DATA(EVT_GCODE_COMMAND, line,
                    strlen(line) > 255u ? 255u : (uint8_t)strlen(line));
    result = parse_line(line, &parsed);
    if (result == GCODE_OK) result = execute_parsed(&parsed);
    if (result == GCODE_OK) BG_EVT_PUB(EVT_GCODE_COMPLETE);
    else BG_EVT_PUB_DATA(EVT_GCODE_ERROR, &result, sizeof(result));
    return result;
}

int Gcode_ExecuteFile(const char *path, int reportProgress)
{
    char chunk[64];
    char line[GCODE_LINE_MAX];
    uint32_t offset = 0u;
    uint32_t lineNumber = 1u;
    uint16_t lineLength = 0u;
    int count;
    int i;

    if (!path) return GCODE_ERR_INVALID;
    if (reportProgress) {
        s_segIdx = 0u;
        (void)refresh_position();
        Shell_Printf("@PG 0 %ld %ld %ld\r\n", (long)s_state.positionMilliMm[0],
                     (long)s_state.positionMilliMm[1], (long)s_state.positionMilliMm[2]);
    }
    for (;;) {
        count = banux_read_at(path, chunk, sizeof(chunk), offset);
        if (count < 0) return GCODE_ERR_DRIVER;
        if (count == 0) break;
        offset += (uint32_t)count;
        for (i = 0; i < count; i++) {
            unsigned char c = (unsigned char)chunk[i];
            if (offset - (uint32_t)count + (uint32_t)i < 3u &&
                ((offset - (uint32_t)count + (uint32_t)i == 0u && c == 0xEFu) ||
                 (offset - (uint32_t)count + (uint32_t)i == 1u && c == 0xBBu) ||
                 (offset - (uint32_t)count + (uint32_t)i == 2u && c == 0xBFu))) {
                continue;
            }
            if (c == '\r') continue;
            if (c == '\n') {
                if (lineLength) {
                    int result;
                    line[lineLength] = '\0';
                    /* 段标记 ;@SEG：此处上一段运动已阻塞走完、位置已刷新，坐标为真实平面位置，
                     * 且喷嘴已抬到安全 Z —— 是检测停止/暂停的最安全时机。先泵入并轮询控制字符，
                     * 再回报进度（走 shell 输出行，串口直连与无线透传都通用）。 */
                    if (reportProgress && strncmp(line, ";@SEG", 5) == 0) {
                        int action = gcode_poll_control();
                        if (action & GCODE_ACT_STOP) {
                            gcode_halt();
                            Shell_Print("gcode: aborted\r\n");
                            return GCODE_ERR_ABORTED;
                        }
                        if (action & GCODE_ACT_PAUSE) {
                            /* 暂停：保持电机使能以锁定位置，持续泵入轮询直到恢复('~')或停止('!')。
                             * 期间不回报 @PG；上位机侧相应挂起无响应看门狗。 */
                            Shell_Print("gcode: paused\r\n");
                            for (;;) {
                                int held = gcode_poll_control();
                                if (held & GCODE_ACT_STOP) {
                                    gcode_halt();
                                    Shell_Print("gcode: aborted\r\n");
                                    return GCODE_ERR_ABORTED;
                                }
                                if (held & GCODE_ACT_RESUME) {
                                    Shell_Print("gcode: resumed\r\n");
                                    break;
                                }
                                HAL_Delay(20);
                            }
                        }
                        s_segIdx++;
                        Shell_Printf("@PG %lu %ld %ld %ld\r\n", (unsigned long)s_segIdx,
                                     (long)s_state.positionMilliMm[0],
                                     (long)s_state.positionMilliMm[1],
                                     (long)s_state.positionMilliMm[2]);
                    }
                    result = Gcode_ExecuteLine(line);
                    if (result != GCODE_OK) {
                        Shell_Printf("gcode: line %lu failed (%d)\r\n",
                                     (unsigned long)lineNumber, result);
                        return result;
                    }
                    lineLength = 0u;
                }
                lineNumber++;
            } else {
                if (lineLength + 1u >= sizeof(line)) return GCODE_ERR_LINE_TOO_LONG;
                line[lineLength++] = (char)c;
            }
        }
    }
    if (lineLength) {
        int result;
        line[lineLength] = '\0';
        result = Gcode_ExecuteLine(line);
        if (result != GCODE_OK) return result;
    }
    return GCODE_OK;
}

void Gcode_GetState(GcodeState_t *state)
{
    if (state) *state = s_state;
}

static void print_state(void)
{
    GcodeState_t state;
    char value[5][20];
    uint32_t i;
    Gcode_GetState(&state);
    for (i = 0u; i < 5u; i++) {
        int32_t milli = i < 4u ? state.positionMilliMm[i]
                               : state.feedMilliMmPerMin;
        uint32_t magnitude = (uint32_t)(milli < 0 ? -(int64_t)milli : milli);
        snprintf(value[i], sizeof(value[i]), "%s%lu.%03lu",
                 milli < 0 ? "-" : "", (unsigned long)(magnitude / 1000u),
                 (unsigned long)(magnitude % 1000u));
    }
    Shell_Printf("X:%s Y:%s Z:%s E:%s F:%s mode=%s motors=%s\r\n",
                 value[0], value[1], value[2], value[3], value[4],
                 state.absoluteMode ? "absolute" : "relative",
                 state.motorsEnabled ? "on" : "off");
}

static int shell_gcode_line(int argc, char *argv[])
{
    char line[GCODE_LINE_MAX];
    int i;
    int result;
    size_t length = 0u;

    if (argc <= 0) return GCODE_ERR_INVALID;
    line[0] = '\0';
    for (i = 0; i < argc; i++) {
        size_t part = strlen(argv[i]);
        if (length + part + (i ? 1u : 0u) >= sizeof(line)) return GCODE_ERR_LINE_TOO_LONG;
        if (i) line[length++] = ' ';
        memcpy(&line[length], argv[i], part);
        length += part;
        line[length] = '\0';
    }
    result = Gcode_ExecuteLine(line);
    if (result == GCODE_OK) {
        if ((line[0] == 'M' || line[0] == 'm') && atoi(&line[1]) == 114) print_state();
        else Shell_Print("ok\r\n");
    } else {
        Shell_Printf("gcode: rejected (%d)\r\n", result);
    }
    return result;
}

static int shell_gcode_file(int argc, char *argv[])
{
    int result;
    int reportProgress = 0;
    int i;
    if (argc < 1) return GCODE_ERR_INVALID;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0) reportProgress = 1;
    }
    result = Gcode_ExecuteFile(argv[0], reportProgress);
    if (result == GCODE_OK) Shell_Print("gcode: file complete\r\n");
    return result;
}

static int shell_gcode_status(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    print_state();
    return 0;
}

static const ShellOpt_t s_gcodeOptions[] = {
    OPT("", "", "<line>", "Execute one G-code line", shell_gcode_line),
    OPT("f", "file", "<path>", "Execute a G-code file", shell_gcode_file),
    OPT("s", "status", NULL, "Show G-code state", shell_gcode_status),
    OPT_END()
};

static const ShellModule_t s_gcodeModule = {
    "gcode", "Parse and execute G-code", MOD_CAT_SYSTEM,
    s_gcodeOptions, OPT_COUNT(s_gcodeOptions)
};

static void on_gcode_stop(BG_EventTopic_t topic, const void *data, uint8_t size)
{
    (void)topic;
    (void)data;
    (void)size;
    (void)banux_ioctl(GCODE_GROUP_PATH, DRV_STEPPER_IOCTL_STOP, NULL);
    s_state.motorsEnabled = 0u;
}

int Gcode_Init(void)
{
    memset(&s_state, 0, sizeof(s_state));
    s_state.absoluteMode = 1u;
    s_state.feedMilliMmPerMin = GCODE_DEFAULT_FEED;
    if (refresh_position() != 0) return -1;
    if (!Shell_RegisterModule(&s_gcodeModule)) return -1;
    if (BG_Event_SubscribeNamed(EVT_GCODE_STOP, on_gcode_stop,
                                "gcode.emergency_stop") != 0) return -2;
    return 0;
}

BANUX_COMPONENT_DEFINE_EX(g_banux_component_gcode,
                          "gcode", "1.0.0",
                          BANUX_COMPONENT_APPLICATION, BANUX_GCODE_EN,
                          "G-code parser and stepper adapter",
                          Gcode_Init, NULL);
