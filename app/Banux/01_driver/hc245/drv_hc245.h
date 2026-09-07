/**
 * @file    drv_hc245.h
 * @brief   74HC245 八位总线收发器（用作并行 IO 扩展）驱动接口。
 *
 * 板卡 (Ender-3 V2 / Creality 4.2.2) 上 U10 = 74HC245：
 *   - A 侧 (pin2..pin9 = A0..A7) 为 MCU 数据输入总线；
 *   - B 侧 (pin18..pin11 = B0..B7) 为输出，驱动外部负载（如散热风扇）；
 *   - DIR (pin1) 经 R63 10K 上拉 +5V（默认 A->B）；OE (pin19) 低有效使能。
 * 本驱动把 B0..B7 建模为一个 8 位输出影子寄存器，仅对“已接 MCU A 侧线”
 * 的位真正写 GPIO，其余位仅记录影子值（no-connect）。
 */
#ifndef __DRV_HC245_H__
#define __DRV_HC245_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "type.h"

#define DRV_HC245_BITS 8u

/** 驱动状态（read 返回）。 */
typedef struct {
    uint32_t value;      /* 当前 8 位输出影子值 (bit0..bit7 = B0..B7) */
    uint32_t wiredMask;  /* 已接 MCU A 侧线的位掩码 */
    uint32_t fanBit;     /* 配置的散热风扇输出位 */
    uint32_t fanOn;      /* 1=风扇输出当前置位 */
} DrvHc245Status_t;

/** 驱动写命令：按 mask 更新 value 中的位。 */
typedef struct {
    uint32_t value;
    uint32_t mask;
} DrvHc245Command_t;

#define DRV_HC245_IOCTL_SET_VALUE   1u  /* arg: DrvHc245Command_t* */
#define DRV_HC245_IOCTL_GET_VALUE   2u  /* arg: uint32_t* (out) */
#define DRV_HC245_IOCTL_SET_BIT     3u  /* arg: DrvHc245BitCmd_t* */
#define DRV_HC245_IOCTL_CLEAR_BIT   4u  /* arg: uint32_t* (bit index) */
#define DRV_HC245_IOCTL_FAN         5u  /* arg: int* (0/1) */

typedef struct {
    uint32_t bit;
    uint32_t on;
} DrvHc245BitCmd_t;

/** 注册 hc245 设备到驱动框架。 */
int DrvHc245_Register(void);

/** 按 mask 更新输出位。仅 wired 位真正写 GPIO。返回 0 成功。 */
int DrvHc245_SetValue(uint32_t value, uint32_t mask);

/** 读取 8 位输出影子值。 */
uint32_t DrvHc245_GetValue(void);

/** 置/清单个输出位 (bit < DRV_HC245_BITS)。返回 0 成功。 */
int DrvHc245_SetBit(uint32_t bit, int on);

/** 读取单个输出位当前影子值 (0/1)，位无效返回 -1。 */
int DrvHc245_GetBit(uint32_t bit);

/** 控制散热风扇（= HC245_FAN_BIT 位）。on 非 0=开。 */
int DrvHc245_Fan(int on);

#ifdef __cplusplus
}
#endif

#endif /* __DRV_HC245_H__ */
