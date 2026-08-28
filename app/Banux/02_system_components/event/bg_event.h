/**
 * @file    bg_event.h
 * @brief   事件发布-订阅系统 (类 ROS Topic 模型)
 * @author  BanGO
 * @date    2026-04-01
 *
 * 架构定位: 02_system_components/event/
 *   向下对接 driver_framework (事件发布者),
 *   向上对接 03_application_components / 04_application (事件订阅者)。
 *
 * 核心概念:
 *   - Topic (话题): 一个 uint16_t ID, 定义在 bg_event_topics.h
 *   - Publisher (发布者): 调用 BG_Event_Publish() 发送事件
 *   - Subscriber (订阅者): 使用 BG_EVT_SUB() 在文件作用域声明即自动注册
 *
 * 设计原则:
 *   1. 编译期静态注册: BG_EVT_SUB() 宏在文件任意位置声明,
 *      编译器将订阅条目放入 bg_evt_sub 链接段 (无点前缀, 合法C标识符),
 *      GCC ld 自动提供 __start_bg_evt_sub / __stop_bg_evt_sub 边界符号,
 *      BG_Event_Init() 自动遍历该段完成注册, 零手动调用
 *   2. 同步分发: Publish() 直接调用所有订阅者回调 (适合单线程/主循环)
 *   3. 通配符: 可订阅 BG_EVT_TOPIC_ANY 接收所有事件
 *   4. 轻量级: ~200 字节 RAM (32 订阅槽)
 *
 * 使用示例:
 *   // === 发布端 (驱动层) ===
 *   BG_EventBtnData_t data = { .btn_id = 0, .duration_ms = 50 };
 *   BG_EVT_PUB_DATA(EVT_BTN_CLICK, &data, sizeof(data));
 *
 *   // === 订阅端 (应用层, 文件作用域) ===
 *   static void my_btn_handler(BG_EventTopic_t t, const void *d, uint8_t s) {
 *       const BG_EventBtnData_t *btn = (const BG_EventBtnData_t *)d;
 *       DBG("btn %d clicked\n", btn->btn_id);
 *   }
 *   BG_EVT_SUB(EVT_BTN_CLICK, my_btn_handler);   // 声明即注册!
 */

#ifndef __BG_EVENT_H__
#define __BG_EVENT_H__

#include <stdint.h>
#include "banux_config.h"      /* BG_EVENT_EN */
#include "bg_event_topics.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================
 * 配置
 * ============================================ */

/** 最大订阅槽数 (每个占 6 字节, 32 槽 = 192 字节) */
#ifndef BG_EVENT_MAX_SUBSCRIBERS
#define BG_EVENT_MAX_SUBSCRIBERS    32
#endif

/** 通配符话题: 订阅此话题将接收所有事件 */
#define BG_EVT_TOPIC_ANY            0xFFFF

/* ============================================
 * 类型定义
 * ============================================ */

/**
 * @brief 事件回调函数类型
 * @param topic  触发的话题 ID
 * @param data   事件数据指针 (由发布者定义, 可为 NULL)
 * @param size   数据大小 (字节)
 */
typedef void (*BG_EventCallback_t)(BG_EventTopic_t topic, const void *data, uint8_t size);

/**
 * @brief 编译期静态订阅条目 (存储在 Flash .bg_evt_sub 段)
 *
 * 由 BG_EVT_SUB() 宏自动生成, 用户无需直接使用。
 * BG_Event_Init() 遍历链接段, 将所有条目注册到运行时订阅表。
 */
typedef struct {
    uint16_t            topic;      /**< 订阅话题 ID */
    BG_EventCallback_t  callback;   /**< 回调函数指针 */
    const char         *name;       /**< 静态订阅者名称 */
} BG_EventStaticSub_t;

typedef struct {
    BG_EventTopic_t topic;
    BG_EventCallback_t callback;
    const char *name;
} BG_EventSubscriptionInfo_t;

/* ============================================
 * API
 * ============================================ */

#if BG_EVENT_EN

/**
 * @brief 初始化事件系统 (自动加载所有 BG_EVT_SUB 静态订阅)
 * @note  在 main() 中、所有模块 Init 之前调用
 */
void BG_Event_Init(void);

/**
 * @brief 运行时动态订阅话题
 * @param topic     要订阅的话题 ID, 或 BG_EVT_TOPIC_ANY
 * @param callback  回调函数
 * @return 0=成功, -1=订阅表已满, -2=参数无效
 *
 * @note 大多数场景推荐使用 BG_EVT_SUB() 文件作用域宏 (编译期注册)。
 *       此函数仅用于需要运行时动态增减订阅的场景。
 */
int BG_Event_Subscribe(BG_EventTopic_t topic, BG_EventCallback_t callback);

/** Register a named subscriber. Name must remain valid for the subscription lifetime. */
int BG_Event_SubscribeNamed(BG_EventTopic_t topic, BG_EventCallback_t callback,
                            const char *name);

/**
 * @brief 取消订阅
 * @param topic     话题 ID
 * @param callback  之前注册的回调
 * @return 0=成功, -1=未找到
 */
int BG_Event_Unsubscribe(BG_EventTopic_t topic, BG_EventCallback_t callback);

/**
 * @brief 发布事件 (同步: 立即调用所有匹配的订阅者)
 * @param topic  话题 ID
 * @param data   事件数据 (可为 NULL)
 * @param size   数据大小
 * @return 被通知的订阅者数量
 */
int BG_Event_Publish(BG_EventTopic_t topic, const void *data, uint8_t size);

/**
 * @brief 查询当前已使用的订阅槽数
 * @return 已使用的槽数
 */
uint8_t BG_Event_GetSubscriberCount(void);

/** Read the Nth active subscription for diagnostics. */
int BG_Event_GetSubscription(uint8_t index, BG_EventSubscriptionInfo_t *info);

/** Return a stable symbolic topic name, or "UNKNOWN". */
const char *BG_Event_GetTopicName(BG_EventTopic_t topic);

/* ============================================
 * 编译期静态注册宏 (核心)
 * ============================================ */

/* 内部: 拼接唯一变量名 */
#define _BG_EVT_CAT2(a, b)     a##b
#define _BG_EVT_CAT(a, b)      _BG_EVT_CAT2(a, b)

/**
 * @brief 静态订阅宏 — 在文件作用域声明, 编译即注册
 *
 * 原理:
 *   将 {topic, callback} 条目放入 bg_evt_sub 链接段 (Flash 只读区),
 *   段名为合法C标识符, GCC ld 自动生成 __start/__stop 边界符号,
 *   BG_Event_Init() 启动时遍历该段, 自动调用 Subscribe 注册。
 *   __attribute__((used)) 防止编译器优化移除,
 *   引用 __start_bg_evt_sub 使 --gc-sections 保留该段。
 *
 * 用法 (文件作用域, 任何 .c 文件):
 *   static void on_click(BG_EventTopic_t t, const void *d, uint8_t s) { ... }
 *   BG_EVT_SUB(EVT_BTN_CLICK, on_click);
 *
 *   static void on_ble(BG_EventTopic_t t, const void *d, uint8_t s) { ... }
 *   BG_EVT_SUB(EVT_BLE_DATA_RECEIVED, on_ble);
 */
#if defined(__ARMCC_VERSION) && (__ARMCC_VERSION < 6000000)
/*
 * AC5 (ARMCC V5) 兼容说明:
 *   bg_evt_sub 静态段的 __start_/__stop_ 边界符号由 GCC ld 自动生成,
 *   armcc V5 链接器不提供该机制, 静态段无法被遍历注册。
 *   因此 AC5 下禁用编译期静态注册, 所有订阅一律使用
 *   BG_Event_Subscribe() 运行时注册 (推荐集中到 Banux_begin() 之前)。
 */
#define BG_EVT_SUB(topic, cb)
#else
#define BG_EVT_SUB(topic, cb)                                           \
    static const BG_EventStaticSub_t                                    \
    __attribute__((used, section("bg_evt_sub")))                        \
    _BG_EVT_CAT(_bg_evt_, __COUNTER__) = {                              \
        (uint16_t)(topic), (cb), #cb                                    \
    }
#endif

/* ============================================
 * 发布便捷宏
 * ============================================ */

/**
 * @brief 快捷发布宏 — 无数据事件
 *
 * 用法:
 *   BG_EVT_PUB(EVT_SYS_POWER_OFF);
 */
#define BG_EVT_PUB(topic)       BG_Event_Publish((BG_EventTopic_t)(topic), NULL, 0)

/**
 * @brief 快捷发布宏 — 带数据
 *
 * 用法:
 *   BG_EventBtnData_t d = { .btn_id = 2 };
 *   BG_EVT_PUB_DATA(EVT_BTN_CLICK, &d, sizeof(d));
 */
#define BG_EVT_PUB_DATA(topic, pdata, sz)  \
    BG_Event_Publish((BG_EventTopic_t)(topic), (pdata), (uint8_t)(sz))

#else /* !BG_EVENT_EN */

/* BG_EVENT_EN=0 时所有 API 替换为空操作，调用方无需修改 */
#define BG_Event_Init()                         ((void)0)
#define BG_Event_Subscribe(topic, cb)           (0)
#define BG_Event_SubscribeNamed(topic, cb, name) (0)
#define BG_Event_Unsubscribe(topic, cb)         (0)
#define BG_Event_Publish(topic, data, size)     (0)
#define BG_Event_GetSubscriberCount()           (0)
#define BG_Event_GetSubscription(index, info)   (-1)
#define BG_Event_GetTopicName(topic)             ("DISABLED")
#define BG_EVT_SUB(topic, cb)                   /* disabled */
#define BG_EVT_PUB(topic)                       ((void)0)
#define BG_EVT_PUB_DATA(topic, pdata, sz)       ((void)0)

#endif /* BG_EVENT_EN */

#ifdef __cplusplus
}
#endif

#endif /* __BG_EVENT_H__ */
