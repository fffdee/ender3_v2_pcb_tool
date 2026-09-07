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
    GCODE_ERR_LINE_TOO_LONG = -6,
    GCODE_ERR_ABORTED = -7
} GcodeResult_t;

/* 平面选择(G17/G18/G19)：圆弧插补所在的平面 */
#define GCODE_PLANE_XY  0u
#define GCODE_PLANE_XZ  1u
#define GCODE_PLANE_YZ  2u

/* 模态运动模式：G0/G1 直线，G2 顺时针圆弧，G3 逆时针圆弧。
 * G 代码是模态的：单独一行 "X10 Y20" 需沿用上一次的运动模式。 */
#define GCODE_MOTION_LINEAR  0u
#define GCODE_MOTION_CW      2u
#define GCODE_MOTION_CCW     3u

typedef struct {
    int32_t positionMilliMm[4];
    int32_t feedMilliMmPerMin;
    uint8_t absoluteMode;
    uint8_t motorsEnabled;
    uint8_t unitsInch;   /* G20=1 英制(inch)，G21=0 公制(mm，默认) */
    uint8_t plane;       /* GCODE_PLANE_*，默认 XY */
    uint8_t motionMode;  /* GCODE_MOTION_*，默认直线 */
} GcodeState_t;

int Gcode_Init(void);
int Gcode_ExecuteLine(const char *line);
int Gcode_ExecuteFile(const char *path, int reportProgress);
void Gcode_GetState(GcodeState_t *state);

#endif /* BANUX_GCODE_H */
