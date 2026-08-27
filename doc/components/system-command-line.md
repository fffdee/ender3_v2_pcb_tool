# 命令行系统组件

## 1. 定位与配置

命令行组件名为 `shell`，负责传输抽象、行编辑、参数解析、模块注册和命令执行。当前描述符固定启用。限制为命令行 128 字节、最多 15 个参数、40 个模块、输出格式化缓冲 256 字节。

当前产品使用 `ShellIO_UartAll_Get()`：输出同时发送到 UART1 2 Mbaud 和 UART3 115200，输入先取 UART1，再取 UART3。串口原始数据先由 `app_bl_poll()` 嗅探升级帧，再从镜像缓冲交给 Shell，避免两个消费者抢同一环形缓冲。

## 2. 运行逻辑

1. `Shell_Init()` 清空命令模块表、输入缓冲和历史，注册内置 `help` 模块。
2. `Shell_SetIO()` 注入 `send/recv/available` 三个传输回调。
3. `Shell_RegisterAllModules()` 注册文件、系统、驱动、事件、脚本等命令；应用组件可在随后初始化时继续注册模块。
4. `Shell_Process()` 非阻塞读取字符，处理退格、回车、方向键历史和 Tab 补全。
5. 收到完整行后，`Shell_ExecuteLine()` 分词，按模块名和短/长选项选择回调。
6. 回调返回非零时打印错误；脚本调用同一执行入口，因此交互命令和脚本语义一致。

## 3. 调用链

```text
Banux_Init
  -> Shell_Init
  -> Shell_SetIO(ShellIO_UartAll_Get())
  -> Shell_RegisterAllModules
  -> 应用组件 Gcode_Init -> Shell_RegisterModule(gcode)

Banux_Process
  -> BanuxScheduler_Process
     -> Shell_Process
        -> IO.available -> IO.recv
        -> Shell_ProcessChar
        -> Shell_Execute
        -> Shell_ExecuteLine
        -> module option callback
```

## 4. 使用方法

用户命令：

```text
help -a
help -m sys
history
clear
ls /driver
sys -v
banux -i
```

注册模块：

```c
static int cmd_status(int argc, char *argv[]) { return 0; }
static const ShellOpt_t opts[] = {
    OPT("s", "status", NULL, "Show status", cmd_status),
    OPT_END()
};
static const ShellModule_t module = {
    "demo", "Demo commands", MOD_CAT_SYSTEM, opts, OPT_COUNT(opts)
};

void Demo_Init(void)
{
    Shell_RegisterModule(&module);
}
```

自定义传输只需提供 `ShellIO_t`，然后在 Shell 初始化后调用 `Shell_SetIO()`。不要在 `Shell_Init()` 之前注册模块，因为初始化会清空模块表。

## 5. 诊断

- 没有提示符：检查 `Shell_SetIO()`、调度器是否持续调用 `Shell_Process()`。
- 命令显示 unknown：确认模块已注册，使用 `help -a` 查看实际模块表。
- 两串口输入异常：确认 `app_bl_poll()` 在 Shell 之前运行且镜像缓冲未溢出。
- 命令被截断：检查 128 字节行长和 15 参数限制。

