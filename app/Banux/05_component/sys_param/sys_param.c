/**
 * sys_param.c - System parameter storage module implementation
 *
 * This file implements the functions declared in sys_param.h for managing
 * system parameters in internal Flash, including initialization, save, load,
 * default restore, and shell command support.
 *
 * Flash API (SDK provided):
 *   - SpiFlashRead(addr, buf, len, timeout) - Read from flash
 *   - SpiFlashWrite(addr, buf, len, timeout) - Write to flash
 *   - SpiFlashErase(SECTOR_ERASE, sector_num, 1) - Erase sector
 *   - SpiFlashIOCtrl(IOCTL_FLASH_UNPROTECT, "\x35\xBA\x69", 3) - Unprotect
 */

#include "sys_param.h"
#include "spi_flash.h"  /* Flash API */
#include "bg_shell.h"   /* Shell_Printf for print functions */
#include <string.h>
#include <stdio.h>
#include "audio_setting.h"
#include "shell_cmd_effect.h"  /* Effect parameter access */
#include "effect_graph.h"      /* For EffectGraph_FindNodeById */
#include "ctrlvars.h"          /* For ControlVariablesContext and EQUnit */
/* Debug output */
#ifdef CFG_APP_CONFIG
#include "debug.h"
#define PARAM_DBG(...)  DBG(__VA_ARGS__)
#else
#define PARAM_DBG(...)
#endif

/* Global system parameters */
SysParam_t g_sys_param;
static uint8_t g_param_modified = 0;
static uint8_t g_param_initialized = 0;

/* Flash configuration from param_def.h */
#define FLASH_ADDR      SYS_PARAM_FLASH_ADDR
#define FLASH_SECTOR    SYS_PARAM_SECTOR_NUM
#define FLASH_TIMEOUT   SYS_PARAM_FLASH_TIMEOUT

/**
 * @brief Load default graph into audio_chain
 * Loads the default 14-node 13-edge topology:
 *   ADC0(Guitar) -+
 *   ADC1(Mic)   -+-> ADC_Mixer -> Expander -> DRC -> EQ -> Reverb -+
 *                                                                     |
 *   USB_In      -+                                                   |
 *   BT_In       -+-> USB_BT_Mixer -> USB_BT_EQ ------------------+
 *                                                                  |
 *   Reverb -----+                                                 |
 *   USB_BT_EQ --+-> Final_Mixer --> DAC0_Out
 *                                 \-> USB_Out
 */
static void LoadDefaultGraphConfig(void)
{
    /* 清空音频链配置。
     * 注意：graph_count 保留为 0（memset 后默认），表示不使用任何保存的图。
     * ChainGraph_AutoApplyOnStartup 检查 graph_count==0 时会直接返回，
     * 系统继续使用由 EffectGraphConfig_LoadPreset(GRAPH_PRESET_DEFAULT) 加载的
     * 硬编码默认图（含正确端口号的 ADC_Mixer 4路输入和 Looper 连接）。
     *
     * 历史问题：旧版本在此处构建 graph_count=1 的"默认"保存图，
     * 但 ChainGraph_ApplyToEffectGraph 丢失了所有边的端口号（强制 dst_port=0），
     * 导致 EQ_GUITAR_L/R + EQ_MIC_L/R 全部映射到 ADC_Mixer port 0，
     * 声道路由混乱，引起严重的音频失真。 */
    memset(&g_sys_param.audio_chain, 0, sizeof(SysParam_AudioChain_t));
    g_sys_param.audio_chain.output_mode = 0;
    /* graph_count = 0 (already from memset): no saved graphs, use hardcoded DEFAULT preset */
}

/* Standard CRC32 implementation (polynomial 0xEDB88320) */
static uint32_t calc_crc32(const void *data, size_t len) {
    const uint8_t *buf = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFF;
    size_t i;
    int j;
    for (i = 0; i < len; ++i) {
        crc ^= buf[i];
        for (j = 0; j < 8; ++j) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320;
            else
                crc >>= 1;
        }
    }
    return crc ^ 0xFFFFFFFF;
}

/**
 * @brief Unlock flash protection for write/erase operations
 */
static void flash_unprotect(void) {
    SpiFlashIOCtrl(IOCTL_FLASH_UNPROTECT, "\x35\xBA\x69", 3);
}

/**
 * @brief Load parameters from flash
 * @param param Pointer to parameter structure to fill
 * @return 0 on success, -1 on error
 */
static int flash_load(SysParam_t *param) {
    int ret;
    uint32_t stored_crc, calc_crc_val;

    PARAM_DBG("[PARAM] Loading from flash addr 0x%08lX...\n", (unsigned long)FLASH_ADDR);

    /* Read entire parameter block from flash */
    ret = SpiFlashRead(FLASH_ADDR, (uint8_t*)param, sizeof(SysParam_t), FLASH_TIMEOUT);
    if (ret != FLASH_NONE_ERR) {
        PARAM_DBG("[PARAM] Flash read error: %d\n", ret);
        return -1;
    }

    /* Check magic number */
    if (param->magic != SYS_PARAM_MAGIC) {
        PARAM_DBG("[PARAM] Invalid magic: 0x%08lX (expected 0x%08lX)\n",
                  (unsigned long)param->magic, (unsigned long)SYS_PARAM_MAGIC);
        return -1;
    }

    /* Verify CRC (calculate on data after crc32 field) */
    stored_crc = param->crc32;
    param->crc32 = 0;  /* Zero out for calculation */
    calc_crc_val = calc_crc32(param, sizeof(SysParam_t));
    param->crc32 = stored_crc;  /* Restore */

    if (stored_crc != calc_crc_val) {
        PARAM_DBG("[PARAM] CRC mismatch: stored=0x%08lX calc=0x%08lX\n",
                  (unsigned long)stored_crc, (unsigned long)calc_crc_val);
        return -1;
    }

    PARAM_DBG("[PARAM] Load success, write_count=%lu\n", (unsigned long)param->write_count);
    return 0;
}

/**
 * @brief Save parameters to flash
 * @param param Pointer to parameter structure to save
 * @return 0 on success, -1 on error
 */
static int flash_save(SysParam_t *param) {
    int ret;

    PARAM_DBG("[PARAM] Saving to flash addr 0x%08lX...\n", (unsigned long)FLASH_ADDR);

    /* Update header fields */
    param->magic = SYS_PARAM_MAGIC;
    param->version = SYS_PARAM_VERSION;
    param->size = sizeof(SysParam_t);
    param->write_count++;

    /* Calculate CRC (with crc32 field zeroed) */
    param->crc32 = 0;
    param->crc32 = calc_crc32(param, sizeof(SysParam_t));

    /* Unlock flash */
    flash_unprotect();

    /* Erase sector */
    PARAM_DBG("[PARAM] Erasing sector %d...\n", FLASH_SECTOR);
    SpiFlashErase(SECTOR_ERASE, FLASH_SECTOR, 1);

    /* Write data */
    PARAM_DBG("[PARAM] Writing %d bytes...\n", (int)sizeof(SysParam_t));
    ret = SpiFlashWrite(FLASH_ADDR, (uint8_t*)param, sizeof(SysParam_t), FLASH_TIMEOUT);
    if (ret != FLASH_NONE_ERR) {
        PARAM_DBG("[PARAM] Flash write error: %d\n", ret);
        return -1;
    }

    PARAM_DBG("[PARAM] Save success, write_count=%lu, CRC=0x%08lX\n",
              (unsigned long)param->write_count, (unsigned long)param->crc32);
    return 0;
}

SysParam_Status_t SysParam_Init(void) {
    PARAM_DBG("[PARAM] Initializing system parameters...\n");

    if (flash_load(&g_sys_param) != 0 || g_sys_param.magic != SYS_PARAM_MAGIC) {
        PARAM_DBG("[PARAM] Load failed or invalid, loading defaults\n");
        SysParam_LoadDefault();
        // 保存默认参数到flash作为出厂参数
        if (SysParam_Save() == SYSPARAM_OK) {
            PARAM_DBG("[PARAM] Default parameters saved to flash as factory settings\n");
        } else {
            PARAM_DBG("[PARAM] Failed to save default parameters to flash\n");
        }
        g_param_modified = 1;
        g_param_initialized = 1;
        return SYSPARAM_ERR_MAGIC;
    }

    /* Check version compatibility */
    if (g_sys_param.version != SYS_PARAM_VERSION) {
        PARAM_DBG("[PARAM] Version mismatch: stored=0x%04X expected=0x%04X\n",
                  g_sys_param.version, SYS_PARAM_VERSION);
        /* Could implement migration here, for now just reload defaults */
        SysParam_LoadDefault();
        // 保存默认参数到flash作为出厂参数
        if (SysParam_Save() == SYSPARAM_OK) {
            PARAM_DBG("[PARAM] Default parameters saved to flash as factory settings\n");
        } else {
            PARAM_DBG("[PARAM] Failed to save default parameters to flash\n");
        }
        g_param_modified = 1;
        g_param_initialized = 1;
        return SYSPARAM_ERR_VERSION;
    }

    g_param_modified = 0;
    g_param_initialized = 1;
    PARAM_DBG("[PARAM] Init complete, loaded %lu bytes\n", (unsigned long)sizeof(SysParam_t));

    	AudioSetting_SetMic1VolumePercent(g_sys_param.volume.mic1_volume);


    	AudioSetting_SetMic2VolumePercent(g_sys_param.volume.mic2_volume);



    	AudioSetting_SetGuitar1VolumePercent( g_sys_param.volume.guitar1_volume );


    	AudioSetting_SetGuitar2VolumePercent( g_sys_param.volume.guitar2_volume );


    return SYSPARAM_OK;
}

SysParam_Status_t SysParam_Save(void) {
    if (!g_param_initialized) {
        PARAM_DBG("[PARAM] Error: not initialized\n");
        return SYSPARAM_ERR_NOT_INIT;
    }

    if (flash_save(&g_sys_param) == 0) {
        g_param_modified = 0;
        PARAM_DBG("[PARAM] Parameters saved successfully\n");
        return SYSPARAM_OK;
    }
    PARAM_DBG("[PARAM] Save failed!\n");
    return SYSPARAM_ERR_FLASH;
}

SysParam_t* SysParam_Get(void) {
    return &g_sys_param;
}

SysParam_Status_t SysParam_LoadDefault(void) {
    PARAM_DBG("[PARAM] Loading default parameters...\n");
    memset(&g_sys_param, 0, sizeof(SysParam_t));

    /* Header */
    g_sys_param.magic = SYS_PARAM_MAGIC;
    g_sys_param.version = SYS_PARAM_VERSION;
    g_sys_param.size = sizeof(SysParam_t);
    g_sys_param.write_count = 0;

    /* System defaults */
    g_sys_param.system.current_boot_status = NORMAL_BOOT;
    g_sys_param.system.boot_count = 0;
    g_sys_param.system.lp_enable = 1;       /* 默认启用自动低功耗 */
    g_sys_param.system.lp_timeout_min = 5;   /* 默认5分钟空闲超时 */

    /* Volume defaults */
    g_sys_param.volume.guitar1_volume = 80;
    g_sys_param.volume.guitar2_volume = 80;
    g_sys_param.volume.mic1_volume = 80;
    g_sys_param.volume.mic2_volume = 80;
    g_sys_param.volume.output_volume = 80;
    g_sys_param.volume.bt_max_volume = 100;   /* BT music max at wheel full scale */
    g_sys_param.volume.usb_max_volume = 100;  /* USB music max at wheel full scale */
    g_sys_param.volume.usb_out_volume = 100;  /* USB output to PC volume */
    g_sys_param.volume.usb_out_mute = 0;      /* USB output not muted */

    /* Looper defaults */
    g_sys_param.looper.loop_count = 4;
    g_sys_param.looper.overdub_mode = 0;
    g_sys_param.looper.quantize = 0;
    g_sys_param.looper.click_volume = 50;
    g_sys_param.looper.tempo = 120;
    g_sys_param.looper.time_signature = 0;  /* 4/4 */
    g_sys_param.looper.fade_time = 10;      /* 100ms */
    g_sys_param.looper.max_loop_time = 60000; /* 60 seconds */
    /* 默认假设Flash未初始化，首次开机触发全片擦除 */
    g_sys_param.looper.flash_status = LOOPER_FLASH_STATUS_USED;
    g_sys_param.looper.segment_volume[0] = 0xFF;  /* 0xFF = 未初始化，开机默认 100% */
    g_sys_param.looper.segment_volume[1] = 0xFF;
    g_sys_param.looper.segment_volume[2] = 0xFF;
    g_sys_param.looper.segment_volume[3] = 0xFF;
    /* 默认录制源：LINE_L (2 = LOOP_REC_SRC_LINEIN_L) */
    g_sys_param.looper.segment_rec_source[0] = 2;
    g_sys_param.looper.segment_rec_source[1] = 2;
    g_sys_param.looper.segment_rec_source[2] = 2;
    g_sys_param.looper.segment_rec_source[3] = 2;
    
    /* 新增：存储抽象层性能参数默认值 */
    g_sys_param.looper.storage_type = 0;        /* 默认 NOR Flash */
    g_sys_param.looper.write_speed_kbps = 0;    /* 未测试 */
    g_sys_param.looper.read_speed_kbps = 0;     /* 未测试 */
    g_sys_param.looper.max_concurrent_tracks = 1; /* 默认1段 */
    g_sys_param.looper.bandwidth_tested = 0;    /* 未测试 */
    g_sys_param.looper.support_overdub = 0;     /* 默认不支持叠录 */
    /* 导出默认设置 */
    g_sys_param.looper.export_mono_mix = 0;     /* 默认关闭声道平衡 */
    g_sys_param.looper.export_gain_pct = 100;   /* 默认 100% = 原始电平 */

    /* Bluetooth defaults */
    g_sys_param.bluetooth.enabled = 1;
    g_sys_param.bluetooth.discoverable = 1;
    g_sys_param.bluetooth.auto_connect = 1;
    g_sys_param.bluetooth.a2dp_volume = 80;
    strcpy(g_sys_param.bluetooth.device_name, "BanBox");
    memset(g_sys_param.bluetooth.paired_addr, 0, 6);

    /* LCD defaults */
    g_sys_param.lcd.contrast = 50;
    g_sys_param.lcd.color_scheme = 0;
    g_sys_param.lcd.screen_saver = 0;
    g_sys_param.lcd.bg_color = 0x0000;  /* Black */

    /* Chain manager defaults */
    memset(&g_sys_param.chain_manager, 0, sizeof(BG_ParamChainManager_t));
    strcpy(g_sys_param.chain_manager.chains[0].name, "ChainA");
    strcpy(g_sys_param.chain_manager.chains[1].name, "ChainB");
    g_sys_param.chain_manager.active_chain = 0;

    /* Audio chain defaults - load from effect_graph_config.h */
    LoadDefaultGraphConfig();

    g_param_modified = 1;
    PARAM_DBG("[PARAM] Defaults loaded\n");
    return SYSPARAM_OK;
}

/**
 * @brief Apply flash parameters to audio system (gCtrlVars)
 *        This syncs stored volume/EQ/effect settings to the runtime audio variables
 *        Called after SysParam_Init() and CtrlVarsInit() to override defaults with saved values
 * @return SYSPARAM_OK on success
 */
SysParam_Status_t SysParam_ApplyToAudio(void) {
    int i;
    extern ControlVariablesContext gCtrlVars;
    
    if (!g_param_initialized) {
        PARAM_DBG("[PARAM] Error: not initialized, cannot apply to audio\n");
        return SYSPARAM_ERR_NOT_INIT;
    }
    
    PARAM_DBG("[PARAM] Applying saved parameters to audio system...\n");
    
    /* Apply volume settings */
    AudioSetting_SetMic1VolumePercent(g_sys_param.volume.mic1_volume);
    AudioSetting_SetMic2VolumePercent(g_sys_param.volume.mic2_volume);
    AudioSetting_SetGuitar1VolumePercent(g_sys_param.volume.guitar1_volume);
    AudioSetting_SetGuitar2VolumePercent(g_sys_param.volume.guitar2_volume);
    
    PARAM_DBG("[PARAM] Applied volumes: mic1=%d mic2=%d guitar1=%d guitar2=%d\n",
              g_sys_param.volume.mic1_volume,
              g_sys_param.volume.mic2_volume,
              g_sys_param.volume.guitar1_volume,
              g_sys_param.volume.guitar2_volume);
    
    /* Apply audio chain node settings to effect graph */
    /* Iterate through nodes in the default graph and apply saved settings */
    if (g_sys_param.audio_chain.graph_count > 0) {
        EffectGraph_t *graph = &g_sys_param.audio_chain.graphs[0];
        for (i = 0; i < graph->node_count; i++) {
            uint8_t nid = graph->node_ids[i];
            if (nid < MAX_GRAPH_NODES) {
                GraphNode_t *gnode = &g_sys_param.audio_chain.node_pool[nid];
                EffectNode_t *efx_node = EffectGraph_FindNodeById(nid);
                if (efx_node) {
                    efx_node->enabled = gnode->enabled;
                    efx_node->bypass = !gnode->enabled;
                    /* Apply effect-specific parameters from gnode->params[] */
                }
            }
        }
        PARAM_DBG("[PARAM] Applied audio chain with %d nodes\n", graph->node_count);
    }
    
    PARAM_DBG("[PARAM] Parameters applied to audio system\n");

    /* Apply system settings */
    {
        extern void LowPower_SetEnabled(uint8_t);
        extern void LowPower_SetTimeoutMin(uint8_t);
        LowPower_SetEnabled(g_sys_param.system.lp_enable);
        LowPower_SetTimeoutMin(g_sys_param.system.lp_timeout_min);
        PARAM_DBG("[PARAM] Auto-LP: %s, timeout=%d min\n",
                  g_sys_param.system.lp_enable ? "enabled" : "disabled",
                  g_sys_param.system.lp_timeout_min);
    }

    /* Apply USB output volume (device->PC) */
    {
        extern void BG_AudioIO_SetUsbOutVolume(uint8_t vol, uint8_t mute);
        BG_AudioIO_SetUsbOutVolume(g_sys_param.volume.usb_out_volume,
                                   g_sys_param.volume.usb_out_mute);
    }

    return SYSPARAM_OK;
}

bool SysParam_IsModified(void) {
    return g_param_modified ? true : false;
}

uint32_t SysParam_GetWriteCount(void) {
    return g_sys_param.write_count;
}

/**
 * @brief Save a specific module's parameters
 * @param module Module name: "system", "audio", "looper", "bt", "lcd", "all"
 * @return Status code
 */
SysParam_Status_t SysParam_SaveModule(const char *module) {
    /* For simple implementation, just save entire parameter block */
    /* This could be optimized to save only specific module areas */
    (void)module;
    PARAM_DBG("[PARAM] Saving module: %s\n", module ? module : "all");
    return SysParam_Save();
}

/**
 * @brief Print detailed effect parameters for a node
 * @param node Pointer to the graph node
 * @param node_id Node ID for finding live parameters
 */
static void SysParam_PrintEffectParams(GraphNode_t *node, uint8_t node_id) {
    // 从effect_graph获取实时参数，而不是sys_param的静态存储
    EffectNode_t *live_node = EffectGraph_FindNodeById(node_id);

    if (live_node != NULL) {
        // 使用实时参数
        switch (live_node->type) {
            case EFFECT_NODE_TYPE_EFFECT_DRC:
                Shell_Printf(" [threshold=%d(0-96) ratio=%d(1-20) attack=%d(0-100) release=%d(0-100)]",
                            live_node->params.drc.threshold,
                            live_node->params.drc.ratio,
                            live_node->params.drc.attack,
                            live_node->params.drc.release);
                break;

            case EFFECT_NODE_TYPE_EFFECT_REVERB:
                Shell_Printf(" [room=%d(0-100) damp=%d(0-100) wet=%d(0-100)]",
                            live_node->params.reverb.room_size,
                            live_node->params.reverb.damping,
                            live_node->params.reverb.wet_dry);
                break;

            case EFFECT_NODE_TYPE_EFFECT_EQ:
                {
                    int band;
                    int active_count = live_node->params.eq.band_count;

                    Shell_Printf(" [%d bands", active_count);

                    /* 显示启用的频段详情（最多显示3个，避免输出过长） */
                    int shown = 0;
                    for (band = 0; band < active_count && shown < 3; band++) {
                        if (live_node->params.eq.band_enables[band]) {
                            int16_t gain = live_node->params.eq.band_gains[band];
                            uint32_t freq = live_node->params.eq.band_f0[band];

                            if (shown > 0) Shell_Printf(",");
                            Shell_Printf(" b%d:%+ddB@%luHz", band, gain / 256, (unsigned long)freq);
                            shown++;
                        }
                    }

                    if (active_count > shown) {
                        Shell_Printf(", +%d more", active_count - shown);
                    }

                    Shell_Printf("] *runtime");
                }
                break;

            default:
                /* For other effects, show hex values */
                Shell_Printf(" [");
                {
                    int p;
                    for (p = 0; p < 11 && live_node->params.raw[p] != 0; p++) {
                        if (p > 0) Shell_Printf(" ");
                        Shell_Printf("%02X", live_node->params.raw[p]);
                    }
                }
                Shell_Printf("]");
                break;
        }
    } else {
        // 如果找不到实时节点，使用静态存储
        switch (node->subtype) {
            case EFFECT_TYPE_COMPRESSOR:  /* DRC */
                Shell_Printf(" [threshold=%d(0-96) ratio=%d(1-20) attack=%d(0-100) release=%d(0-100)]",
                            node->params[0], node->params[1], node->params[2], node->params[3]);
                break;

            case EFFECT_TYPE_REVERB:
                Shell_Printf(" [room=%d(0-100) damp=%d(0-100) wet=%d(0-100)]",
                            node->params[0], node->params[1], node->params[2]);
                break;

            case EFFECT_TYPE_EQ:
                {
                    int band;
                    int active_count = 0;

                    /* 统计启用的频段数 */
                    for (band = 0; band < 10; band++) {
                        if (band + 80 < 88 && node->params[80 + band]) {
                            active_count++;
                        }
                    }

                    Shell_Printf(" [%d bands", active_count);

                    /* 显示启用的频段详情（最多显示3个，避免输出过长） */
                    int shown = 0;
                    for (band = 0; band < 10 && shown < 3; band++) {
                        if (band + 80 < 88 && node->params[80 + band]) {
                            int8_t gain = (int8_t)node->params[band];
                            uint32_t freq = 0;

                            /* 提取频率（4字节，小端） */
                            if (10 + band * 4 + 3 < 88) {
                                freq = node->params[10 + band * 4] |
                                       (node->params[10 + band * 4 + 1] << 8) |
                                       (node->params[10 + band * 4 + 2] << 16) |
                                       (node->params[10 + band * 4 + 3] << 24);
                            }

                            if (shown > 0) Shell_Printf(",");
                            Shell_Printf(" b%d:%+ddB@%luHz", band, gain, (unsigned long)freq);
                            shown++;
                        }
                    }

                    if (active_count > shown) {
                        Shell_Printf(", +%d more", active_count - shown);
                    }

                    Shell_Printf("] *static");
                }
                break;

            default:
                /* For other effects, show hex values */
                Shell_Printf(" [");
                {
                    int p;
                    for (p = 0; p < 11 && node->params[p] != 0; p++) {
                        if (p > 0) Shell_Printf(" ");
                        Shell_Printf("%02X", node->params[p]);
                    }
                }
                Shell_Printf("]");
                break;
        }
    }
}


void SysParam_Print(void) {
    Shell_Printf("=== System Parameters ===\n");
    Shell_Printf("Magic:       0x%08lX\n", (unsigned long)g_sys_param.magic);
    Shell_Printf("Version:     0x%04X\n", g_sys_param.version);
    Shell_Printf("Size:        %u bytes\n", g_sys_param.size);
    Shell_Printf("CRC32:       0x%08lX\n", (unsigned long)g_sys_param.crc32);
    Shell_Printf("WriteCount:  %lu\n", (unsigned long)g_sys_param.write_count);
    Shell_Printf("Modified:    %s\n", g_param_modified ? "Yes" : "No");
    Shell_Printf("\n--- System ---\n");
    Shell_Printf("  BootStatus: %d\n", g_sys_param.system.current_boot_status);
    Shell_Printf("  BootCount:  %d\n", g_sys_param.system.boot_count);
    Shell_Printf("\n--- Volume ---\n");
    Shell_Printf("  Guitar1:    %d\n", g_sys_param.volume.guitar1_volume);
    Shell_Printf("  Guitar2:    %d\n", g_sys_param.volume.guitar2_volume);
    Shell_Printf("  Mic1:       %d\n", g_sys_param.volume.mic1_volume);
    Shell_Printf("  Mic2:       %d\n", g_sys_param.volume.mic2_volume);
    Shell_Printf("  Output:     %d\n", g_sys_param.volume.output_volume);
    Shell_Printf("\n--- Looper ---\n");
    Shell_Printf("  LoopCount:  %d\n", g_sys_param.looper.loop_count);
    Shell_Printf("  Overdub:    %d\n", g_sys_param.looper.overdub_mode);
    Shell_Printf("  Quantize:   %d\n", g_sys_param.looper.quantize);
    Shell_Printf("  ClickVol:   %d\n", g_sys_param.looper.click_volume);
    Shell_Printf("  Tempo:      %d BPM\n", g_sys_param.looper.tempo);
    Shell_Printf("\n--- Bluetooth ---\n");
    Shell_Printf("  Enabled:    %d\n", g_sys_param.bluetooth.enabled);
    Shell_Printf("  DevName:    %s\n", g_sys_param.bluetooth.device_name);
    Shell_Printf("  A2DP Vol:   %d\n", g_sys_param.bluetooth.a2dp_volume);
    Shell_Printf("\n--- LCD ---\n");
    Shell_Printf("  Contrast:   %d\n", g_sys_param.lcd.contrast);
    Shell_Printf("  ColorScheme:%d\n", g_sys_param.lcd.color_scheme);
    Shell_Printf("  BgColor:    0x%04X\n", g_sys_param.lcd.bg_color);
    Shell_Printf("\n--- Audio Chain (Graph) ---\n");
    Shell_Printf("  OutputMode: %d\n", g_sys_param.audio_chain.output_mode);
    Shell_Printf("  GraphCount: %d\n", g_sys_param.audio_chain.graph_count);
    Shell_Printf("  HP Graph:   %d\n", g_sys_param.audio_chain.active_graph_hp);
    Shell_Printf("  SPK Graph:  %d\n", g_sys_param.audio_chain.active_graph_spk);
    Shell_Printf("  NodePool:   %d/%d used\n",
                __builtin_popcount(g_sys_param.audio_chain.node_used_mask),
                MAX_GRAPH_NODES);

    /* Print effect node pool details */
    Shell_Printf("\n--- Effect Node Pool ---\n");
    {
        int i, p;
        for (i = 0; i < MAX_GRAPH_NODES; i++) {
            GraphNode_t *node = &g_sys_param.audio_chain.node_pool[i];
            if (node->node_type != 0) {  /* Check if node is used */
                const char *type_str = "UNK";
                const char *subtype_str = "UNK";

                switch (node->node_type) {
                    case NODE_TYPE_SOURCE: type_str = "SRC"; break;
                    case NODE_TYPE_EFFECT: type_str = "FX"; break;
                    case NODE_TYPE_MIXER: type_str = "MIX"; break;
                    case NODE_TYPE_OUTPUT: type_str = "OUT"; break;
                }

                if (node->node_type == NODE_TYPE_SOURCE) {
                    switch (node->subtype) {
                        case SOURCE_TYPE_GUITAR: subtype_str = "GUITAR"; break;
                        case SOURCE_TYPE_MIC: subtype_str = "MIC"; break;
                        case SOURCE_TYPE_USB: subtype_str = "USB"; break;
                        case SOURCE_TYPE_BT: subtype_str = "BT"; break;
                    }
                } else if (node->node_type == NODE_TYPE_EFFECT) {
                    switch (node->subtype) {
                        case EFFECT_TYPE_COMPRESSOR: subtype_str = "COMP"; break;
                        case EFFECT_TYPE_EQ: subtype_str = "EQ"; break;
                        case EFFECT_TYPE_REVERB: subtype_str = "REV"; break;
                        case EFFECT_TYPE_DELAY: subtype_str = "DLY"; break;
                        default: subtype_str = "FX"; break;
                    }
                } else if (node->node_type == NODE_TYPE_OUTPUT) {
                    switch (node->subtype) {
                        case OUTPUT_TYPE_HEADPHONE: subtype_str = "HP"; break;
                        case OUTPUT_TYPE_SPEAKER: subtype_str = "SPK"; break;
                        case OUTPUT_TYPE_LINE_OUT: subtype_str = "LINE"; break;
                    }
                }

                Shell_Printf("  [%2d] %s-%s %s Vol:%d",
                            i, type_str, subtype_str,
                            node->enabled ? "ON" : "OFF", node->volume);

                if (node->node_type == NODE_TYPE_EFFECT) {
                    Shell_Printf(" P:%d", node->preset);
                    SysParam_PrintEffectParams(node, (uint8_t)i);
                }
                Shell_Printf("\n");
            }
        }
    }
    
    /* Print runtime effect graph status (实时运行状态) */
    Shell_Printf("\n--- Runtime Effect Graph ---\n");
    {
        extern ControlVariablesContext gCtrlVars;
        
        /* 打印mic_out_eq_unit状态 */
        Shell_Printf("  mic_out_eq: %s, %d bands\n", 
                    gCtrlVars.mic_out_eq_unit.enable ? "ON" : "OFF",
                    gCtrlVars.mic_out_eq_unit.filter_count);
        if (gCtrlVars.mic_out_eq_unit.enable && gCtrlVars.mic_out_eq_unit.filter_count > 0) {
            int i;
            for (i = 0; i < gCtrlVars.mic_out_eq_unit.filter_count && i < 3; i++) {
                if (gCtrlVars.mic_out_eq_unit.eq_params[i].enable) {
                    Shell_Printf("    b%d: %+ddB @ %luHz\n", i,
                                gCtrlVars.mic_out_eq_unit.eq_params[i].gain / 256,
                                (unsigned long)gCtrlVars.mic_out_eq_unit.eq_params[i].f0);
                }
            }
        }
        
        /* 打印music_out_eq_unit状态 */
        Shell_Printf("  music_out_eq: %s, %d bands\n",
                    gCtrlVars.music_out_eq_unit.enable ? "ON" : "OFF",
                    gCtrlVars.music_out_eq_unit.filter_count);
        if (gCtrlVars.music_out_eq_unit.enable && gCtrlVars.music_out_eq_unit.filter_count > 0) {
            int i;
            for (i = 0; i < gCtrlVars.music_out_eq_unit.filter_count && i < 3; i++) {
                if (gCtrlVars.music_out_eq_unit.eq_params[i].enable) {
                    Shell_Printf("    b%d: %+ddB @ %luHz\n", i,
                                gCtrlVars.music_out_eq_unit.eq_params[i].gain / 256,
                                (unsigned long)gCtrlVars.music_out_eq_unit.eq_params[i].f0);
                }
            }
        }
    }
}

void SysParam_PrintModule(const char *module) {
    if (!module) {
        SysParam_Print();
        return;
    }
    Shell_Printf("=== Module: %s ===\n", module);
    if (strcmp(module, "system") == 0 || strcmp(module, "sys") == 0) {
        Shell_Printf("  BootStatus: %d\n", g_sys_param.system.current_boot_status);
        Shell_Printf("  BootCount:  %d\n", g_sys_param.system.boot_count);
    }
    else if (strcmp(module, "audio") == 0 || strcmp(module, "vol") == 0) {
        Shell_Printf("  Guitar1:    %d\n", g_sys_param.volume.guitar1_volume);
        Shell_Printf("  Guitar2:    %d\n", g_sys_param.volume.guitar2_volume);
        Shell_Printf("  Mic1:       %d\n", g_sys_param.volume.mic1_volume);
        Shell_Printf("  Mic2:       %d\n", g_sys_param.volume.mic2_volume);
        Shell_Printf("  Output:     %d\n", g_sys_param.volume.output_volume);
        Shell_Printf("  BT Max:     %d\n", g_sys_param.volume.bt_max_volume);
        Shell_Printf("  USB Max:    %d\n", g_sys_param.volume.usb_max_volume);
        Shell_Printf("  USB Out:    %d\n", g_sys_param.volume.usb_out_volume);
        Shell_Printf("  USB OutMute:%d\n", g_sys_param.volume.usb_out_mute);
    }
    else if (strcmp(module, "looper") == 0) {
        Shell_Printf("  LoopCount:  %d\n", g_sys_param.looper.loop_count);
        Shell_Printf("  Overdub:    %d\n", g_sys_param.looper.overdub_mode);
        Shell_Printf("  Quantize:   %d\n", g_sys_param.looper.quantize);
        Shell_Printf("  ClickVol:   %d\n", g_sys_param.looper.click_volume);
        Shell_Printf("  Tempo:      %d BPM\n", g_sys_param.looper.tempo);
        Shell_Printf("  TimeSig:    %d\n", g_sys_param.looper.time_signature);
        Shell_Printf("  FadeTime:   %d\n", g_sys_param.looper.fade_time);
        Shell_Printf("  MaxTime:    %lu ms\n", (unsigned long)g_sys_param.looper.max_loop_time);
    }
    else if (strcmp(module, "bt") == 0 || strcmp(module, "bluetooth") == 0) {
        Shell_Printf("  Enabled:    %d\n", g_sys_param.bluetooth.enabled);
        Shell_Printf("  Discover:   %d\n", g_sys_param.bluetooth.discoverable);
        Shell_Printf("  AutoConn:   %d\n", g_sys_param.bluetooth.auto_connect);
        Shell_Printf("  A2DP Vol:   %d\n", g_sys_param.bluetooth.a2dp_volume);
        Shell_Printf("  DevName:    %s\n", g_sys_param.bluetooth.device_name);
    }
    else if (strcmp(module, "lcd") == 0) {
        Shell_Printf("  Contrast:   %d\n", g_sys_param.lcd.contrast);
        Shell_Printf("  ColorScheme:%d\n", g_sys_param.lcd.color_scheme);
        Shell_Printf("  ScreenSaver:%d\n", g_sys_param.lcd.screen_saver);
        Shell_Printf("  BgColor:    0x%04X\n", g_sys_param.lcd.bg_color);
    }
    else if (strcmp(module, "chain") == 0 || strcmp(module, "audiochain") == 0 || strcmp(module, "graph") == 0) {
        Shell_Printf("  OutputMode: %d (0:Auto 1:HP 2:SPK)\n", g_sys_param.audio_chain.output_mode);
        Shell_Printf("  GraphCount: %d\n", g_sys_param.audio_chain.graph_count);
        Shell_Printf("  HP Graph:   %d\n", g_sys_param.audio_chain.active_graph_hp);
        Shell_Printf("  SPK Graph:  %d\n", g_sys_param.audio_chain.active_graph_spk);
        Shell_Printf("  NodePool:   %d/%d used\n",
                    __builtin_popcount(g_sys_param.audio_chain.node_used_mask),
                    MAX_GRAPH_NODES);

        /* List all graphs */
        int g, i, j;
        for (g = 0; g < g_sys_param.audio_chain.graph_count; g++) {
            EffectGraph_t *graph = &g_sys_param.audio_chain.graphs[g];
            Shell_Printf("  [Graph %d: %s]\n", g, graph->name);
            Shell_Printf("    Nodes: %d, Edges: %d\n", graph->node_count, graph->edge_count);

            /* List nodes */
            for (i = 0; i < graph->node_count; i++) {
                uint8_t nid = graph->node_ids[i];
                GraphNode_t *node = &g_sys_param.audio_chain.node_pool[nid];
                const char *type_str[] = {"SRC", "FX", "MIX", "OUT"};
                Shell_Printf("      N%d: %s subtype=%d vol=%d %s\n",
                            nid, type_str[node->node_type], node->subtype,
                            node->volume, node->enabled ? "ON" : "OFF");
            }

            /* List edges */
            if (graph->edge_count > 0) {
                Shell_Printf("    Edges:\n");
                for (j = 0; j < graph->edge_count; j++) {
                    Shell_Printf("      N%d -> N%d\n",
                                graph->edges[j].from_node, graph->edges[j].to_node);
                }
            }
        }
    }
    else {
        Shell_Printf("Unknown module: %s\n", module);
        Shell_Printf("Available: system, audio, looper, bt, lcd, chain\n");
    }
}

void SysParam_MarkModified(void) {
    g_param_modified = 1;
}

void SysParam_RegisterShellCommands(void) {
    /* Shell commands are registered via REGISTER_MODULE macro */
    PARAM_DBG("[PARAM] Shell commands ready\n");
}

/**
 * @brief Print detailed effect parameters for a node
 * @param node Pointer to the graph node
 */


int SysParam_ShellCmd(int argc, char *argv[]) {
    /* This is called by shell framework */
    (void)argc; (void)argv;
    return 0;
}

