#ifndef BANUX_MOTION_CONTROL_H
#define BANUX_MOTION_CONTROL_H

#include <stdint.h>
#include "drv_stepper.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 梯形速度规划运动控制。
 *
 * 给定目标步数、最大速度(steps/s)与加速度(steps/s^2)，按"加速-匀速-减速"
 * 三段生成速度曲线，并以"逐拍变间隔"方式驱动步进电机，避免启停丢步。
 *
 * 注意：调用前必须使能步进驱动（echo enable 1 或 gcode M17），
 * 否则 DrvStepper_Step 会因 s_enabled==0 直接返回失败。
 */
int MotionControl_Move(DrvStepperAxis_t axis, int32_t steps,
                       double vMaxStepsPerSec, double aMaxStepsPerSec2);

/* 回零：沿 HOME_DIR_<axis> 方向移动到 min 限位触发处，二次逼近后把该轴 position 置 0。
 * 调用前须使能（echo enable 1）；E 轴无限位返回 -1。
 * 返回 0=成功，-1=该轴无限位/非法轴，-2=未使能，-4=最大行程内未找到限位。 */
int MotionControl_Home(DrvStepperAxis_t axis);

/* 作为 Banux 应用组件初始化：注册 shell 命令 `motion` 与 `home`。 */
int MotionControl_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* BANUX_MOTION_CONTROL_H */
