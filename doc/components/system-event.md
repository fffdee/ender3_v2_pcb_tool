# 消息订阅系统组件

## 1. 定位与配置

消息组件名 `event_bus`，开关 `BG_EVENT_EN`。Topic 统一定义在 `bg_event_topics.h`，运行时最多 `BG_EVENT_MAX_SUBSCRIBERS=32` 个订阅，发布重入深度受 `BG_EVENT_MAX_REENTRY=4` 限制。

当前 ARMCC 5 工程不能使用 GCC 链接段自动遍历，因此 `BG_EVT_SUB()` 在 AC5 下展开为空。当前产品应在组件初始化函数中调用 `BG_Event_SubscribeNamed()` 进行运行时注册。

## 2. 运行逻辑

1. `BG_Event_Init()` 清空订阅表和发布深度；支持链接段的平台还会装载静态订阅。
2. `BG_Event_SubscribeNamed()` 查重后把 topic、回调和静态名称写入固定槽位。
3. `BG_Event_Publish()` 同步遍历订阅表，立即调用 topic 相同或 `BG_EVT_TOPIC_ANY` 的回调。
4. 发布期间的数据指针只在调用栈内有效；回调需要长期使用时必须复制。
5. `BG_Event_Unsubscribe()` 清除匹配项；`event -t` 通过诊断 API按 topic 输出订阅树。

## 3. 调用链

```text
Gcode_ExecuteLine
  -> BG_EVT_PUB_DATA(EVT_GCODE_COMMAND, line, size)
     -> BG_Event_Publish
        -> 遍历 s_subscribers
        -> subscriber.callback(topic, data, size)

Shell: event -t
  -> BG_Event_GetSubscriberCount
  -> BG_Event_GetSubscription(index)
  -> BG_Event_GetTopicName(topic)
```

## 4. 使用方法

运行时命名订阅：

```c
static void on_error(BG_EventTopic_t topic, const void *data, uint8_t size)
{
    int error;
    if (size != sizeof(error)) return;
    memcpy(&error, data, sizeof(error));
}

int Demo_Init(void)
{
    return BG_Event_SubscribeNamed(EVT_GCODE_ERROR, on_error,
                                   "demo.gcode_error");
}
```

发布：

```c
BG_EVT_PUB(EVT_GCODE_STOP);
BG_EVT_PUB_DATA(EVT_GCODE_ERROR, &error, sizeof(error));
```

调试：

```text
event -t
```

名称字符串必须具有静态生命周期。回调中不要执行长耗时操作；系统是同步分发，慢回调会直接拖慢发布者和主循环。

## 5. 诊断

- 订阅失败：检查参数、重复项和 32 槽上限。
- AC5 下 `BG_EVT_SUB()` 无效果：改用 `BG_Event_SubscribeNamed()`。
- 递归发布被拒绝：检查事件回调相互发布形成的环。
- 订阅树没有名称：使用 Named API，普通 `Subscribe` 名称固定为 `runtime`。

