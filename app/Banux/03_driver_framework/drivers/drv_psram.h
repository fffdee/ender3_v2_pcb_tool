/**
 *****************************************************************************
 * @file     drv_psram.h
 * @author   BG Card Team
 * @version  V1.0.0
 * @date     03-April-2026
 * @brief    ESP-PSRAM64H 驱动框架适配层头文件
 *
 * 注册后将在 VFS 创建:
 *   /driver/spi/psram/
 *   ├── name          (驱动名称: ESP-PSRAM64H)
 *   ├── capacity      (PSRAM 容量, MB)
 *   ├── page_size     (页边界, bytes)
 *   ├── status        (初始化状态)
 *   └── device_id     (Electronic ID)
 *****************************************************************************
 */

#ifndef __DRV_PSRAM_H__
#define __DRV_PSRAM_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "type.h"

/**
 * @brief 注册 ESP-PSRAM64H 驱动到驱动框架
 * @retval 0-成功, <0-失败
 */
int Psram_DrvRegister(void);

#ifdef __cplusplus
}
#endif

#endif /* __DRV_PSRAM_H__ */
