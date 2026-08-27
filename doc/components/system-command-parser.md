# 命令解析器系统组件

## 1. 定位与配置

命令解析器组件名 `command_parser`，开关 `COMMAND_PARSER_EN`。它顺序执行 UTF-8 文本中的 Shell 命令，并通过 1 ms 定时驱动实现非阻塞 `delay`。最多嵌套 2 层脚本，每次从文件读取 64 字节，单行受 `SHELL_CMD_MAX_LEN` 限制。

## 2. 运行逻辑

1. `run <path>` 调用 `CommandParser_Start()`，确认路径是 VFS 文件节点，记录文件大小，并跳过可选 UTF-8 BOM。
2. `CommandParser_Process()` 每次主循环增量读取字节，同时用状态机严格验证 UTF-8，包括过长编码、代理区和截断序列。
3. 遇到 CR、LF 或 CRLF 后裁掉行首尾空白，通过 `Shell_ExecuteLine()`执行一条命令。
4. 命令成功后下一次调度继续下一行；陌生命令或非零返回值立即中止整个脚本并报告行号。
5. 脚本中的 `delay N` 设置 `DrvTimer1ms_Now()+N` 截止时间；处理函数在到期前直接返回，不阻塞 UART、Shell 和其他调度项。
6. 脚本内执行另一个 `run` 可嵌套；交互状态下已有脚本运行时禁止启动第二个无关脚本。

## 3. 调用链

```text
Shell: run /flash/job.txt
  -> cmd_run -> CommandParser_Start
     -> DrvFs_FindNode -> DrvFs_ReadFile(BOM)

Banux_Process
  -> BanuxScheduler_Process
     -> CommandParser_Process
        -> DrvTimer1ms_Expired（若等待）
        -> DrvFs_ReadFile（64 字节块）
        -> utf8_feed
        -> execute_line
        -> Shell_ExecuteLine
        -> 对应命令回调
```

## 4. 使用方法

脚本 `/flash/job.txt`：

```text
sys -v
delay 500
gcode G90
gcode G0 X10 Y10 F1200
delay 100
gcode M114
```

执行：

```text
run /flash/job.txt
```

代码调用：

```c
if (!CommandParser_IsRunning()) {
    CommandParser_Start("/sd/startup.txt");
}
```

`delay` 只能出现在正在执行的脚本命令上下文中；交互式输入 `delay 1000` 会返回错误。文件可使用 UTF-8 BOM，换行支持 LF、CR 和 CRLF。

## 5. 失败与约束

- 非法 UTF-8、行过长、文件读失败、未知命令和命令返回非零都会中止。
- 延时上限是 `0x7FFFFFFF` ms，并使用无符号差值处理 1 ms tick 回绕。
- 脚本执行是协作式的；`CommandParser_Process()` 必须持续被调度。
- “非阻塞”只覆盖解析器等待；某条 Shell 命令内部若阻塞，解析器无法替它异步化。

