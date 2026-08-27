#ifndef __DRV_STEPPER_H__
#define __DRV_STEPPER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "type.h"

typedef enum {
    DRV_STEPPER_X = 0,
    DRV_STEPPER_Y,
    DRV_STEPPER_Z,
    DRV_STEPPER_E,
    DRV_STEPPER_COUNT
} DrvStepperAxis_t;

typedef struct {
    int32_t position;
    uint32_t pulseUs;
    uint32_t direction;
    uint32_t enabled;
    uint32_t busy;
    uint32_t limitTriggered;
} DrvStepperStatus_t;

typedef struct {
    int32_t steps;
    uint32_t pulseUs;
} DrvStepperCommand_t;

typedef struct {
    int32_t steps[DRV_STEPPER_COUNT];
    uint32_t pulseUs;
} DrvStepperMoveCommand_t;

#define DRV_STEPPER_IOCTL_ENABLE       1u
#define DRV_STEPPER_IOCTL_DIRECTION    2u
#define DRV_STEPPER_IOCTL_STEP         3u
#define DRV_STEPPER_IOCTL_POSITION     4u
#define DRV_STEPPER_IOCTL_STOP         5u

int DrvStepper_Register(void);
int DrvStepper_EnableAll(int enabled);
int DrvStepper_SetDirection(DrvStepperAxis_t axis, int direction);
int DrvStepper_Step(DrvStepperAxis_t axis, uint32_t count, uint32_t pulseUs);
int32_t DrvStepper_GetPosition(DrvStepperAxis_t axis);
int DrvStepper_SetPosition(DrvStepperAxis_t axis, int32_t position);

/** Read a minimum endstop. Returns 1=triggered, 0=open, <0=unsupported. */
int DrvStepper_GetLimit(DrvStepperAxis_t axis);

#ifdef __cplusplus
}
#endif

#endif /* __DRV_STEPPER_H__ */
