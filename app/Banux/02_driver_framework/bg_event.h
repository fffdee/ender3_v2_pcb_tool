/**
 ******************************************************************************
 * @file    bg_event.h
 * @brief   事件发布-订阅系统 (类 ROS Topic 模型)
 *
 * 核心概念:
 *   - Topic (话题): uint16_t ID, 定义在 bg_event_topics.h
 *   - Publisher (发布者): 调用 BG_Event_Publish() 发送事件
 *   - Subscriber (订阅者): 使用 BG_EVT_SUB() 在文件作用域声明即自动注册
 *
 * 设计原则:
 *   1. 编译期静态注册: BG_EVT_SUB() 宏声明即注册, 零手动调用
 *   2. 同步分发: Publish() 直接调用所有订阅者回调
 *   3. 通配符: 可订阅 BG_EVT_TOPIC_ANY 接收所有事件
 *   4. 轻量级: ~200字节 RAM (32订阅槽)
 *
 * 使用示例:
 *   // 发布端 (驱动层)
 *   BG_EventBtnData_t data = { .btn_id = 0, .duration_ms = 50 };
 *   BG_EVT_PUB_DATA(EVT_BTN_CLICK, &data, sizeof(data));
 *
 *   // 订阅端 (应用层, 文件作用域)
 *   static void my_btn_handler(BG_EventTopic_t t, const void *d, uint8_t s) {
 *       const BG_EventBtnData_t *btn = (const BG_EventBtnData_t *)d;
 *   }
 *   BG_EVT_SUB(EVT_BTN_CLICK, my_btn_handler);   // 声明即注册!
 ******************************************************************************
 */
#ifndef __BG_EVENT_H__
#define __BG_EVENT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "banux_config.h"
#include "bg_event_topics.h"

/** 通配符话题: 订阅此话题将接收所有事件 */
#define BG_EVT_TOPIC_ANY            0xFFFF

/*===========================================================================
 * 类型定义
 *===========================================================================*/

/**
 * @brief 事件回调函数类型
 * @param topic  触发的话题 ID
 * @param data   事件数据指针 (可为 NULL)
 * @param size   数据大小 (字节)
 */
typedef void (*BG_EventCallback_t)(BG_EventTopic_t topic, const void *data, uint8_t size);

/**
 * @brief 编译期静态订阅条目 (存储在 .bg_evt_sub 链接段)
 */
typedef struct {
    uint16_t            topic;
    BG_EventCallback_t  callback;
} BG_EventStaticSub_t;

/*===========================================================================
 * API
 *===========================================================================*/

#if BG_EVENT_EN

/**
 * @brief 初始化事件系统 (自动加载所有 BG_EVT_SUB 静态订阅)
 * @note  在 main() 中、所有模块 Init 之前调用
 */
void BG_Event_Init(void);

/**
 * @brief 运行时动态订阅
 * @return 0=成功, -1=表满, -2=参数无效
 */
int BG_Event_Subscribe(BG_EventTopic_t topic, BG_EventCallback_t callback);

/**
 * @brief 取消订阅
 */
int BG_Event_Unsubscribe(BG_EventTopic_t topic, BG_EventCallback_t callback);

/**
 * @brief 发布事件 (同步: 立即调用所有匹配的订阅者)
 * @return 被通知的订阅者数量
 */
int BG_Event_Publish(BG_EventTopic_t topic, const void *data, uint8_t size);

/**
 * @brief 查询当前已使用的订阅槽数
 */
uint8_t BG_Event_GetSubscriberCount(void);

/*===========================================================================
 * 编译期静态注册宏 (核心)
 *===========================================================================*/

/* 内部: 拼接唯一变量名 */
#define _BG_EVT_CAT2(a, b)     a##b
#define _BG_EVT_CAT(a, b)      _BG_EVT_CAT2(a, b)

/**
 * @brief 静态订阅宏 - 在文件作用域声明, 编译即注册
 *
 * 原理: 将 {topic, callback} 放入 bg_evt_sub 链接段,
 *   GCC ld 自动生成 __start_bg_evt_sub / __stop_bg_evt_sub,
 *   BG_Event_Init() 启动时遍历该段自动注册。
 *
 * 用法:
 *   static void on_click(BG_EventTopic_t t, const void *d, uint8_t s) { ... }
 *   BG_EVT_SUB(EVT_BTN_CLICK, on_click);
 *
 * @note 需要GCC链接器支持 section 属性。对于不支持链接段的编译器,
 *       请使用 BG_Event_Subscribe() 运行时注册。
 */
#define BG_EVT_SUB(topic, cb)                                           \
    static const BG_EventStaticSub_t                                    \
    __attribute__((used, section("bg_evt_sub")))                        \
    _BG_EVT_CAT(_bg_evt_, __COUNTER__) = {                              \
        (uint16_t)(topic), (cb)                                         \
    }

/*===========================================================================
 * 发布便捷宏
 *===========================================================================*/

/** 快捷发布宏 - 无数据事件 */
#define BG_EVT_PUB(topic)       BG_Event_Publish((BG_EventTopic_t)(topic), NULL, 0)

/** 快捷发布宏 - 带数据 */
#define BG_EVT_PUB_DATA(topic, pdata, sz)  \
    BG_Event_Publish((BG_EventTopic_t)(topic), (pdata), (uint8_t)(sz))

#else /* !BG_EVENT_EN */

/* BG_EVENT_EN=0 时所有 API 替换为空操作 */
#define BG_Event_Init()                         ((void)0)
#define BG_Event_Subscribe(topic, cb)           (0)
#define BG_Event_Unsubscribe(topic, cb)         (0)
#define BG_Event_Publish(topic, data, size)     (0)
#define BG_Event_GetSubscriberCount()           (0)
#define BG_EVT_SUB(topic, cb)                   /* disabled */
#define BG_EVT_PUB(topic)                       ((void)0)
#define BG_EVT_PUB_DATA(topic, pdata, sz)       ((void)0)

#endif /* BG_EVENT_EN */

#ifdef __cplusplus
}
#endif

#endif /* __BG_EVENT_H__ */
