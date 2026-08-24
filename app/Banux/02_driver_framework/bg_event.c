/**
 ******************************************************************************
 * @file    bg_event.c
 * @brief   事件发布-订阅系统实现
 *
 * 实现细节:
 *   - 编译期注册: BG_EVT_SUB() 将 {topic, callback} 放入 .bg_evt_sub 链接段
 *   - 运行时订阅: Subscribe/Unsubscribe 支持动态增减
 *   - 静态订阅表: 固定大小数组, 无 malloc
 *   - 同步分发: Publish() 遍历表直接调用回调
 *   - 通配符: topic == BG_EVT_TOPIC_ANY 收到所有事件
 *   - 重入保护: 防止回调中 Publish 导致无限递归
 ******************************************************************************
 */
#include "bg_event.h"
#include <string.h>

#if BG_EVENT_EN

/*===========================================================================
 * 链接段符号 (由 GCC ld 自动生成)
 * 段名为合法C标识符 -> 链接器自动提供 __start_/__stop_ 符号
 * weak: 段为空时不报链接错误
 *===========================================================================*/
extern const BG_EventStaticSub_t __start_bg_evt_sub __attribute__((weak));
extern const BG_EventStaticSub_t __stop_bg_evt_sub  __attribute__((weak));

/*===========================================================================
 * 内部数据结构
 *===========================================================================*/
typedef struct {
    uint16_t            topic;
    BG_EventCallback_t  callback;
} BG_EventSubscriber_t;

static BG_EventSubscriber_t s_subscribers[BG_EVENT_MAX_SUBSCRIBERS];
static uint8_t  s_sub_count = 0;
static uint8_t  s_initialized = 0;
static volatile uint8_t s_publishing = 0;

/*===========================================================================
 * API 实现
 *===========================================================================*/

void BG_Event_Init(void)
{
    const BG_EventStaticSub_t *p;

    memset(s_subscribers, 0, sizeof(s_subscribers));
    s_sub_count = 0;
    s_publishing = 0;
    s_initialized = 1;

    /* 遍历 .bg_evt_sub 链接段, 自动注册所有静态条目 */
    if (&__start_bg_evt_sub != NULL) {
        for (p = &__start_bg_evt_sub; p < &__stop_bg_evt_sub; p++) {
            if (p->callback != (BG_EventCallback_t)0) {
                BG_Event_Subscribe(p->topic, p->callback);
            }
        }
    }
}

int BG_Event_Subscribe(BG_EventTopic_t topic, BG_EventCallback_t callback)
{
    uint8_t i;

    if (!callback) return -2;

    /* 检查重复 */
    for (i = 0; i < s_sub_count; i++) {
        if (s_subscribers[i].callback == callback &&
            s_subscribers[i].topic == (uint16_t)topic)
            return 0;
    }

    /* 复用空闲槽 */
    for (i = 0; i < s_sub_count; i++) {
        if (s_subscribers[i].callback == NULL) {
            s_subscribers[i].topic = (uint16_t)topic;
            s_subscribers[i].callback = callback;
            return 0;
        }
    }

    if (s_sub_count >= BG_EVENT_MAX_SUBSCRIBERS) return -1;

    s_subscribers[s_sub_count].topic = (uint16_t)topic;
    s_subscribers[s_sub_count].callback = callback;
    s_sub_count++;
    return 0;
}

int BG_Event_Unsubscribe(BG_EventTopic_t topic, BG_EventCallback_t callback)
{
    uint8_t i;
    if (!callback) return -1;

    for (i = 0; i < s_sub_count; i++) {
        if (s_subscribers[i].callback == callback &&
            s_subscribers[i].topic == (uint16_t)topic) {
            s_subscribers[i].callback = NULL;
            s_subscribers[i].topic = 0;
            return 0;
        }
    }
    return -1;
}

int BG_Event_Publish(BG_EventTopic_t topic, const void *data, uint8_t size)
{
    uint8_t i;
    int notified = 0;
    uint16_t t = (uint16_t)topic;

    if (!s_initialized) return 0;

    /* 重入保护 */
    if (s_publishing >= BG_EVENT_MAX_REENTRY) return 0;
    s_publishing++;

    for (i = 0; i < s_sub_count; i++) {
        BG_EventCallback_t cb = s_subscribers[i].callback;
        if (cb == NULL) continue;

        /* 精确匹配 或 通配符 */
        if (s_subscribers[i].topic == t ||
            s_subscribers[i].topic == BG_EVT_TOPIC_ANY) {
            cb(topic, data, size);
            notified++;
        }
    }

    s_publishing--;
    return notified;
}

uint8_t BG_Event_GetSubscriberCount(void)
{
    return s_sub_count;
}

#endif /* BG_EVENT_EN */
