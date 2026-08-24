/**
 ******************************************************************************
 * @file    type.h
 * @brief   Banux 框架基础类型定义 (平台支撑头文件)
 *
 * 作用:
 *   - 提供 stdint/stdbool 与 FALSE/TRUE 等基础类型, 供框架模块使用
 *   - 通过 __BANUX_TYPES_DEFINED 宏与 banux_config.h 的内置类型段互斥,
 *     避免重复定义 (先包含本文件的模块直接跳过 banux_config.h 的类型段)
 ******************************************************************************
 */
#ifndef __TYPE_H__
#define __TYPE_H__

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE 1
#endif

#ifndef NULL
#define NULL ((void *)0)
#endif

/* 标记基础类型已定义, banux_config.h 检测到后跳过内置类型段 */
#define __BANUX_TYPES_DEFINED

#endif /* __TYPE_H__ */
