# lmchan Architecture

ESP32-S3 固件架构（C / FreeRTOS / ESP-IDF）。

## 总体结构

- Inbound：Feishu 长连接输入消息
- Agent Loop：构建上下文、调用 LLM、执行工具、生成回复
- Outbound：路由回复到 Feishu / system
- Storage：SPIFFS（配置、记忆、会话、计划任务）
- Runtime Config：NVS（覆盖 build-time 默认配置）

## 关键模块

- `main/lmchan.c`：启动编排
- `main/feishu/feishu_client.c`：Feishu 长连接、事件接收、消息发送
- `main/llm/llm_proxy.c`：LLM 请求适配（anthropic/openai/openai_compatible）
- `main/agent/agent_loop.c`：ReAct 循环
- `main/agent/context_builder.c`：系统提示词拼接（AGENTS/SOUL/USER/TOOLS/记忆）
- `main/tools/*`：工具注册与执行
- `main/cron/cron_service.c`：定时任务
- `main/heartbeat/heartbeat.c`：心跳任务检查
- `main/memory/*`：长期记忆与会话管理
- `main/cli/serial_cli.c`：串口 REPL

## 消息总线

`main/bus/message_bus.h`:

```c
typedef struct {
    char channel[16];
    char chat_id[96];
    char *content;
} lmchan_msg_t;
```

- Inbound queue：channel -> agent
- Outbound queue：agent -> dispatch

## LLM Provider 与端点

支持：

- `anthropic`
- `openai`
- `openai_compatible`

当 provider=`openai_compatible` 时：

- 基础地址：`LMCHAN_SECRET_API_BASE_URL` 或 NVS `api_base_url`
- 路径：`LMCHAN_SECRET_API_PATH` 或 NVS `api_path`
- 协议：OpenAI Chat Completions 兼容格式

## 配置优先级

优先级（高 -> 低）：

1. NVS 运行时配置（CLI 写入）
2. `lmchan_secrets.h` 编译时默认值
3. `lmchan_config.h` 内置默认值

涉及 namespace：

- `wifi_config`
- `fs_config`
- `llm_config`
- `proxy_config`
- `search_config`
- `heartbeat_config`

## Feishu 细节

- 使用 WebSocket 长连接接收事件
- 事件类型：当前处理 `im.message.receive_v1` 文本消息
- 群聊行为：默认仅在有 @ 时入队（`set_fs_group_mode mention`）
- 发送接口：`/open-apis/im/v1/messages?receive_id_type=chat_id`
- token：运行时请求 `tenant_access_token/internal`

## 工具列表

由 `tool_registry` 注册：

- `web_search`
- `get_current_time`
- `read_file`
- `write_file`
- `edit_file`
- `list_dir`
- `cron_add`
- `cron_list`
- `cron_remove`

## 记忆与会话

- 长期记忆：`/spiffs/memory/MEMORY.md`
- 每日记忆：`/spiffs/memory/YYYY-MM-DD.md`
- 会话：`/spiffs/sessions/session_<chat_id>.jsonl`
- 自动压缩：超阈值后归档到 `session_<chat_id>_summary.md`

## 任务系统

- Cron：持久化在 `/spiffs/cron.json`
- Heartbeat：读取 `/spiffs/HEARTBEAT.md`
- 心跳间隔可通过 CLI 动态调整并持久化

## 启动流程

1. NVS / event loop / SPIFFS 初始化
2. 子系统初始化（bus, wifi, feishu, llm, tools, cron, heartbeat, agent）
3. 启动串口 CLI
4. WiFi 连接成功后启动：agent、feishu、cron、heartbeat

## FreeRTOS 任务（概览）

- `fs_ws`（Core0）
- `agent_loop`（Core1）
- `outbound`（Core0）

## 已禁用入口

- Telegram：代码保留，默认不编译，入口已注释禁用
- WebSocket 网关：代码保留，默认不编译，入口已注释禁用

## Flash 分区（默认）

参考 `partitions.csv`：

- `ota_0` / `ota_1`
- `spiffs`（约 12MB）
- `nvs`
- `otadata`
- `coredump`
