/**
 *****************************************************************************
 * @file     drv_w25qxx.h
 * @author   BG Card Team
 * @version  V1.0.0
 * @date     02-January-2026
 * @brief    W25Qxx Flash驱动框架适配层头文件
 *****************************************************************************
 */

#ifndef __DRV_W25QXX_H__
#define __DRV_W25QXX_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "type.h"

/*******************************************************************************
 * 函数声明
 ******************************************************************************/

/**
 * @brief  注册W25Qxx驱动到驱动框架
 * @retval 0-成功, <0-失败
 * 
 * @note   注册后将在文件系统创建:
 *         /driver/spi/w25qxx/
 *         ├── name          (驱动名称)
 *         ├── capacity      (Flash容量)
 *         ├── page_size     (页大小)
 *         ├── sector_size   (扇区大小)
 *         ├── status        (初始化状态)
 *         ├── device_id     (设备ID)
 *         └── erase_chip    (全片擦除命令)
 */
int W25qxx_DrvRegister(void);

#ifdef __cplusplus
}
#endif

#endif /* __DRV_W25QXX_H__ */
