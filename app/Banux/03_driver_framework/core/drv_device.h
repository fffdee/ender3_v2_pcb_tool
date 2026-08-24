/**
 *****************************************************************************
 * @file     drv_device.h
 * @author   BG Card Team
 * @version  V1.0.0
 * @date     02-January-2026
 * @brief    椹卞姩璁惧娉ㄥ唽妗嗘灦 - 绫籐inux椹卞姩妯″瀷
 *****************************************************************************
 * @attention
 *
 * 鏈ā鍧楀疄鐜伴┍鍔ㄨ澶囩殑缁熶竴娉ㄥ唽绠＄悊锛� * 1. 椹卞姩鎶借薄灞傦細瀹氫箟鏍囧噯椹卞姩鎺ュ彛锛坕nit/open/close/read/write/ioctl锛� * 2. 璁惧娉ㄥ唽锛氬皢椹卞姩娉ㄥ唽鍒拌澶囨枃浠剁郴缁� * 3. 鍙傛暟鑷姩娉ㄥ唽锛氭牴鎹弬鏁板畾涔夎嚜鍔ㄥ垱寤哄弬鏁拌妭鐐� * 4. 鎬荤嚎绫诲瀷鍒嗙被锛歋PI/I2C/I2S/SDIO
 *
 * 浣跨敤绀轰緥锛� *   // 1. 瀹氫箟璁惧鍙傛暟
 *   static const FsParamDef_t st7735_params[] = {
 *       FS_PARAM_DEF("name",   "椹卞姩鍚嶇О", get_name, NULL),
 *       FS_PARAM_DEF("width",  "LCD瀹藉害",  get_width, set_width),
 *       FS_PARAM_DEF("height", "LCD楂樺害",  get_height, set_height),
 *       FS_PARAM_END()
 *   };
 *
 *   // 2. 瀹氫箟椹卞姩缁撴瀯
 *   static const DrvDevice_t st7735_drv = {
 *       .name = "st7735",
 *       .bus = DRV_BUS_SPI,
 *       .init = st7735_drv_init,
 *       .params = st7735_params,
 *   };
 *
 *   // 3. 娉ㄥ唽椹卞姩
 *   DrvDevice_Register(&st7735_drv);
 *
 *****************************************************************************
 */

#ifndef __DRV_DEVICE_H__
#define __DRV_DEVICE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "type.h"
#include "drv_fs.h"

/*******************************************************************************
 * 鎬荤嚎绫诲瀷瀹氫箟
 ******************************************************************************/
typedef enum {
    DRV_BUS_SPI = 0,        /* SPI鎬荤嚎 */
    DRV_BUS_I2C,            /* I2C鎬荤嚎 */
    DRV_BUS_I2S,            /* I2S鎬荤嚎 */
    DRV_BUS_SDIO,           /* SDIO鎬荤嚎 */
    DRV_BUS_GPIO,           /* GPIO鐩存帴鎺у埗 */
    DRV_BUS_UART,           /* UART鎬荤嚎 */
    DRV_BUS_POWER,          /* 鐢垫簮绠＄悊鎬荤嚎 */
    DRV_BUS_USB,            /* USB鎬荤嚎 */
    DRV_BUS_MAX
} DrvBusType_t;

/*******************************************************************************
 * 椹卞姩鎿嶄綔鎺ュ彛绫诲瀷瀹氫箟
 ******************************************************************************/
/**
 * @brief  椹卞姩鍒濆鍖� * @param  priv: 璁惧绉佹湁鏁版嵁
 * @return 0鎴愬姛锛屽叾浠栧け璐� */
typedef int (*DrvInit_t)(void *priv);

/**
 * @brief  椹卞姩鍘诲垵濮嬪寲
 * @param  priv: 璁惧绉佹湁鏁版嵁
 * @return 0鎴愬姛
 */
typedef int (*DrvDeinit_t)(void *priv);

/**
 * @brief  鎵撳紑璁惧
 * @param  priv: 璁惧绉佹湁鏁版嵁
 * @return 0鎴愬姛FsParamDef_t
 */
typedef int (*DrvOpen_t)(void *priv);

/**
 * @brief  鍏抽棴璁惧
 * @param  priv: 璁惧绉佹湁鏁版嵁
 * @return 0鎴愬姛
 */
typedef int (*DrvClose_t)(void *priv);

/**
 * @brief  璇诲彇璁惧鏁版嵁
 * @param  priv: 璁惧绉佹湁鏁版嵁
 * @param  buf: 鏁版嵁缂撳啿鍖� * @param  len: 闀垮害
 * @return 瀹為檯璇诲彇闀垮害锛�1閿欒
 */
typedef int (*DrvRead_t)(void *priv, uint8_t *buf, uint32_t len);

/**
 * @brief  鍐欏叆璁惧鏁版嵁
 * @param  priv: 璁惧绉佹湁鏁版嵁
 * @param  buf: 鏁版嵁缂撳啿鍖� * @param  len: 闀垮害
 * @return 瀹為檯鍐欏叆闀垮害锛�1閿欒
 */
typedef int (*DrvWrite_t)(void *priv, const uint8_t *buf, uint32_t len);

/**
 * @brief  璁惧鎺у埗
 * @param  priv: 璁惧绉佹湁鏁版嵁
 * @param  cmd: 鎺у埗鍛戒护
 * @param  arg: 鍙傛暟
 * @return 0鎴愬姛锛屽叾浠栧け璐� */
typedef int (*DrvIoctl_t)(void *priv, uint32_t cmd, void *arg);

/*******************************************************************************
 * 椹卞姩璁惧缁撴瀯
 ******************************************************************************/
typedef struct DrvDevice {
    /* 鍩烘湰淇℃伅 */
    const char         *name;           /* 璁惧鍚嶇О */
    const char         *desc;           /* 璁惧鎻忚堪 */
    DrvBusType_t        bus;            /* 鎬荤嚎绫诲瀷 */
    
    /* 椹卞姩鎿嶄綔鎺ュ彛 */
    DrvInit_t           init;           /* 鍒濆鍖栧嚱鏁�*/
    DrvDeinit_t         deinit;         /* 鍘诲垵濮嬪寲鍑芥暟 */
    DrvOpen_t           open;           /* 鎵撳紑璁惧 */
    DrvClose_t          close;          /* 鍏抽棴璁惧 */
    DrvRead_t           read;           /* 璇诲彇鏁版嵁 */
    DrvWrite_t          write;          /* 鍐欏叆鏁版嵁 */
    DrvIoctl_t          ioctl;          /* 璁惧鎺у埗 */
    
    /* 鍙傛暟瀹氫箟鍒楄〃 */
    const FsParamDef_t *params;         /* 鍙傛暟鏁扮粍锛圢ULL缁撳熬锛�*/
    
    /* 绉佹湁鏁版嵁 */
    void               *privData;       /* 璁惧绉佹湁鏁版嵁 */
    
    /* 杩愯鏃剁姸鎬侊紙鐢辩郴缁熺鐞嗭級 */
    FsNode_t           *fsNode;         /* 鏂囦欢绯荤粺鑺傜偣 */
    bool                isRegistered;   /* 鏄惁宸叉敞鍐�*/
    bool                isOpened;       /* 鏄惁宸叉墦寮�*/
} DrvDevice_t;

/*******************************************************************************
 * 椹卞姩娉ㄥ唽淇℃伅锛堝唴閮ㄤ娇鐢級
 ******************************************************************************/
#define DRV_DEVICE_MAX      16         /* 最大注册设备数 (增加以支持NAND/PSRAM/SD) */

/*******************************************************************************
 * 鍏叡API
 ******************************************************************************/

/**
 * @brief  鍒濆鍖栭┍鍔ㄧ鐞嗙郴缁� * @return 0鎴愬姛
 * @note   浼氳嚜鍔ㄨ皟鐢�DrvFs_Init()
 */
int DrvDevice_Init(void);

/**
 * @brief  娉ㄥ唽椹卞姩璁惧
 * @param  dev: 椹卞姩璁惧缁撴瀯鎸囬拡
 * @return 0鎴愬姛锛屽叾浠栧け璐� * @note   浼氳嚜鍔ㄥ湪瀵瑰簲鎬荤嚎鐩綍涓嬪垱寤鸿澶囪妭鐐瑰拰鍙傛暟鑺傜偣
 */
int DrvDevice_Register(DrvDevice_t *dev);

/**
 * @brief  娉ㄩ攢椹卞姩璁惧
 * @param  dev: 椹卞姩璁惧缁撴瀯鎸囬拡
 * @return 0鎴愬姛
 */
int DrvDevice_Unregister(DrvDevice_t *dev);

/**
 * @brief  鏍规嵁鍚嶇О鏌ユ壘璁惧
 * @param  name: 璁惧鍚嶇О
 * @return 璁惧鎸囬拡锛孨ULL鏈壘鍒� */
DrvDevice_t* DrvDevice_Find(const char *name);

/**
 * @brief  鏍规嵁璺緞鏌ユ壘璁惧
 * @param  path: 璁惧璺緞锛堝 "/driver/spi/st7735"锛� * @return 璁惧鎸囬拡锛孨ULL鏈壘鍒� */
DrvDevice_t* DrvDevice_FindByPath(const char *path);

/**
 * @brief  鑾峰彇鎬荤嚎绫诲瀷瀵瑰簲鐨勭洰褰曡妭鐐� * @param  bus: 鎬荤嚎绫诲瀷
 * @return 鐩綍鑺傜偣鎸囬拡
 */
FsNode_t* DrvDevice_GetBusDir(DrvBusType_t bus);

/**
 * @brief  鑾峰彇鎬荤嚎绫诲瀷鍚嶇О
 * @param  bus: 鎬荤嚎绫诲瀷
 * @return 鍚嶇О瀛楃涓� */
const char* DrvDevice_GetBusName(DrvBusType_t bus);

/**
 * @brief  鍒楀嚭鎵�湁宸叉敞鍐岀殑璁惧
 * @param  callback: 鍥炶皟鍑芥暟
 * @param  userData: 鐢ㄦ埛鏁版嵁
 */
typedef void (*DrvDeviceListCallback_t)(DrvDevice_t *dev, void *userData);
void DrvDevice_List(DrvDeviceListCallback_t callback, void *userData);

/**
 * @brief  鑾峰彇宸叉敞鍐岃澶囨暟閲� * @return 璁惧鏁伴噺
 */
int DrvDevice_GetCount(void);
/**
 * @brief  鑾峰彇璁惧鍒楄〃
 * @param  count: 杈撳嚭璁惧鏁伴噺
 * @return 璁惧鎸囬拡鏁扮粍
 */
DrvDevice_t** DrvDevice_GetList(int *count);
/*******************************************************************************
 * 渚挎嵎瀹忓畾涔� ******************************************************************************/

/* 瀹氫箟璁惧椹卞姩 */
#define DRV_DEVICE_DEF(n, d, b, i) \
    { \
        .name = n, \
        .desc = d, \
        .bus = b, \
        .init = i, \
        .deinit = NULL, \
        .open = NULL, \
        .close = NULL, \
        .read = NULL, \
        .write = NULL, \
        .ioctl = NULL, \
        .params = NULL, \
        .privData = NULL, \
        .fsNode = NULL, \
        .isRegistered = FALSE, \
        .isOpened = FALSE \
    }

/* 绠�寲鍙傛暟瀹氫箟 */
#define DRV_PARAM_RO(n, d, g)       FS_PARAM_DEF(n, d, g, NULL)
#define DRV_PARAM_RW(n, d, g, s)    FS_PARAM_DEF(n, d, g, s)

#ifdef __cplusplus
}
#endif

#endif /* __DRV_DEVICE_H__ */
