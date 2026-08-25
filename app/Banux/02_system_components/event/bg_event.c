/**
 * @file    bg_event.c
 * @brief   事件发布-订阅系统实现
 * @author  BanGO
 * @date    2026-04-01
 *
 * 实现细节:
 *   - 编译期注册: BG_EVT_SUB() 宏将 {topic, callback} 放入 .bg_evt_sub 链接段,
 *     Init 时遍历该段自动调用 Subscribe
 *   - 运行时订阅: Subscribe/Unsubscribe API 支持动态增减
 *   - 静态订阅表: 固定大小数组, 无 malloc
 *   - 同步分发: Publish() 遍历表, 直接调用匹配的回调
 *   - 通配符: topic == BG_EVT_TOPIC_ANY 的订阅者收到所有事件
 *   - O(N) 分发: N = 订阅槽数, 嵌入式场景下 N <= 32, 开销极低
 */

#include "bg_event.h"
#include <string.h>

#if BG_EVENT_EN  /* 整个文件受 BG_EVENT_EN 控制 */

/* ============================================
 * 链接段符号 (由 GCC ld 自动生成)
 * bg_evt_sub 段包含所有 BG_EVT_SUB() 宏生成的静态条目
 * 段名无点前缀 = 合法C标识符 → 链接器自动提供 __start_/__stop_ 符号
 * 引用这些符号同时保障 --gc-sections 不会回收该段
 * AC5 (armcc V5) 无此机制, 静态注册已禁用, 不引用这些符号
 * ============================================ */
#if !defined(__ARMCC_VERSION) || (__ARMCC_VERSION >= 6000000)
/* weak: 段为空或 ld 不支持自动符号时不报链接错误, Init 中会做空指针检查 */
extern const BG_EventStaticSub_t __start_bg_evt_sub __attribute__((weak));
extern const BG_EventStaticSub_t __stop_bg_evt_sub  __attribute__((weak));
#endif

/* ============================================
 * 内部数据结构
 * ============================================ */

typedef struct {
    uint16_t            topic;      /* 订阅的话题 (或 BG_EVT_TOPIC_ANY) */
    BG_EventCallback_t  callback;   /* 回调函数, NULL = 空槽 */
} BG_EventSubscriber_t;

/* 订阅表 (静态分配) */
static BG_EventSubscriber_t s_subscribers[BG_EVENT_MAX_SUBSCRIBERS];
static uint8_t s_sub_count = 0;
static uint8_t s_initialized = 0;

/* 重入保护: 防止回调中 Publish 导致无限递归 */
static volatile uint8_t s_publishing = 0;
#define BG_EVENT_MAX_REENTRY  4

/* ============================================
 * API 实现
 * ============================================ */

void BG_Event_Init(void)
{
    memset(s_subscribers, 0, sizeof(s_subscribers));
    s_sub_count = 0;
    s_publishing = 0;
    s_initialized = 1;

#if defined(__ARMCC_VERSION) && (__ARMCC_VERSION < 6000000)
    /* AC5: 无 __start_/__stop_ 段符号, BG_EVT_SUB 静态注册已禁用,
     * 全部订阅经 BG_Event_Subscribe() 运行时注册, 这里无需遍历 */
    (void)0;
#else
    /* 遍历 .bg_evt_sub 链接段, 自动注册所有 BG_EVT_SUB() 静态条目
     * &__start_bg_evt_sub 为 NULL 时 (weak 符号未解析 / 段为空) 跳过 */
    {
        const BG_EventStaticSub_t *p;

        if (&__start_bg_evt_sub != NULL) {
            for (p = &__start_bg_evt_sub; p < &__stop_bg_evt_sub; p++) {
                if (p->callback != (BG_EventCallback_t)0) {
                    BG_Event_Subscribe(p->topic, p->callback);
                }
            }
        }
    }
#endif
}

int BG_Event_Subscribe(BG_EventTopic_t topic, BG_EventCallback_t callback)
{
    uint8_t i;

    if (!callback) return -2;

    /* 检查重复订阅 */
    for (i = 0; i < s_sub_count; i++) {
        if (s_subscribers[i].callback == callback &&
            s_subscribers[i].topic == (uint16_t)topic) {
            return 0;  /* 已订阅, 视为成功 */
        }
    }

    /* 尝试使用空闲槽 (被 Unsubscribe 释放的) */
    for (i = 0; i < s_sub_count; i++) {
        if (s_subscribers[i].callback == NULL) {
            s_subscribers[i].topic = (uint16_t)topic;
            s_subscribers[i].callback = callback;
            return 0;
        }
    }

    /* 追加新条目 */
    if (s_sub_count >= BG_EVENT_MAX_SUBSCRIBERS) {
        return -1;  /* 满 */
    }

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
    return -1;  /* 未找到 */
}

int BG_Event_Publish(BG_EventTopic_t topic, const void *data, uint8_t size)
{
    uint8_t i;
    int notified = 0;
    uint16_t t = (uint16_t)topic;

    if (!s_initialized) return 0;

    /* 重入保护 */
    if (s_publishing >= BG_EVENT_MAX_REENTRY) {
        return 0;
    }
    s_publishing++;

    for (i = 0; i < s_sub_count; i++) {
        BG_EventCallback_t cb = s_subscribers[i].callback;
        if (cb == NULL) continue;

        /* 匹配: 精确话题 或 通配符 */
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
