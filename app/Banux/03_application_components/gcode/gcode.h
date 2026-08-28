#ifndef BANUX_GCODE_H
#define BANUX_GCODE_H

#include <stdint.h>

typedef enum {
    GCODE_OK = 0,
    GCODE_ERR_INVALID = -1,
    GCODE_ERR_CHECKSUM = -2,
    GCODE_ERR_UNKNOWN_WORD = -3,
    GCODE_ERR_UNSUPPORTED = -4,
    GCODE_ERR_DRIVER = -5,
    GCODE_ERR_LINE_TOO_LONG = -6
} GcodeResult_t;

typedef struct {
    int32_t positionMilliMm[4];
    int32_t feedMilliMmPerMin;
    uint8_t absoluteMode;
    uint8_t motorsEnabled;
} GcodeState_t;

int Gcode_Init(void);
int Gcode_ExecuteLine(const char *line);
int Gcode_ExecuteFile(const char *path);
void Gcode_GetState(GcodeState_t *state);

#endif /* BANUX_GCODE_H */
