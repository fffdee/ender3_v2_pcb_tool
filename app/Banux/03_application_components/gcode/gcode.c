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

#define GCODE_LINE_MAX       96u
#define GCODE_DEFAULT_FEED   1200000L
#define GCODE_GROUP_PATH     "/driver/gpio/stepper_group"

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
    uint64_t pulseUs;

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
    durationUs = ((uint64_t)(uint32_t)maxDistance * 60000000ULL) /
                 (uint32_t)s_state.feedMilliMmPerMin;
    pulseUs = durationUs / (2u * maxSteps);
    if (pulseUs < 2u) pulseUs = 2u;
    if (pulseUs > 100000u) pulseUs = 100000u;
    command.pulseUs = (uint32_t)pulseUs;
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

int Gcode_ExecuteFile(const char *path)
{
    char chunk[64];
    char line[GCODE_LINE_MAX];
    uint32_t offset = 0u;
    uint32_t lineNumber = 1u;
    uint16_t lineLength = 0u;
    int count;
    int i;

    if (!path) return GCODE_ERR_INVALID;
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
    if (argc != 1) return GCODE_ERR_INVALID;
    result = Gcode_ExecuteFile(argv[0]);
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
