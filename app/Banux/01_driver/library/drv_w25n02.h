/**
 *****************************************************************************
 * @file     drv_w25n02.h
 * @author   BG Card Team
 * @version  V1.0.0
 * @date     03-April-2026
 * @brief    W25N02 NAND Flash 驱动框架适配层头文件
 *
 * 注册后将在 VFS 创建:
 *   /driver/spi/w25n02/
 *   ├── name          (驱动名称)
 *   ├── capacity      (Flash 容量, MB)
 *   ├── page_size     (页大小, bytes)
 *   ├── block_size    (块大小, KB)
 *   ├── block_count   (块总数)
 *   ├── bad_blocks    (坏块数量)
 *   ├── status        (初始化状态)
 *   ├── device_id     (JEDEC ID)
 *   └── scan_bbt      (写入 "start" 触发坏块扫描)
 *****************************************************************************
 */

#ifndef __DRV_W25N02_H__
#define __DRV_W25N02_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "type.h"

/**
 * @brief 注册 W25N02 NAND Flash 驱动到驱动框架
 * @retval 0-成功, <0-失败
 */
int W25n02_DrvRegister(void);

#ifdef __cplusplus
}
#endif

#endif /* __DRV_W25N02_H__ */
