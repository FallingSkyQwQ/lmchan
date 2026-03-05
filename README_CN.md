# lmchan: ESP32-S3 口袋 AI 助手

> 纯 C + ESP-IDF + FreeRTOS。没有 Linux，没有 Node.js。

## 项目简介

lmchan 把一块 ESP32-S3 开发板变成常驻在线的 AI 助手：

- 飞书（Feishu）长连接对话（私聊 + 群聊）
- 群聊仅 @ 触发（可配置）
- 本地持久化记忆（SPIFFS）
- 工具调用（搜索、时间、文件、定时任务）
- 定时/心跳任务
- 运行时配置（NVS）

默认人格是「凉面酱」猫娘（可通过 `SOUL.md` 自定义）。

## 硬件与依赖

- ESP32-S3（建议 16MB Flash + 8MB PSRAM）
- USB 数据线（不是仅充电线）
- ESP-IDF v5.5+

## 快速开始

### 1) 克隆并设置目标

```bash
git clone https://github.com/memovai/lmchan.git
cd lmchan
idf.py set-target esp32s3
```

### 2) 配置密钥

```bash
cp main/lmchan_secrets.h.example main/lmchan_secrets.h
```

编辑 `main/lmchan_secrets.h`：

```c
#define LMCHAN_SECRET_WIFI_SSID       "你的WiFi名"
#define LMCHAN_SECRET_WIFI_PASS       "你的WiFi密码"
#define LMCHAN_SECRET_FS_APP_ID       "cli_xxx"
#define LMCHAN_SECRET_FS_APP_SECRET   "xxx"
#define LMCHAN_SECRET_FS_WS_URL       "https://open.feishu.cn/callback/ws/endpoint"
#define LMCHAN_SECRET_FS_GROUP_MODE   "mention"     // mention | all

#define LMCHAN_SECRET_API_KEY         "sk-..."
#define LMCHAN_SECRET_MODEL_PROVIDER  "anthropic"   // anthropic | openai | openai_compatible
#define LMCHAN_SECRET_MODEL           ""

// OpenAI 兼容网关（仅 provider=openai_compatible 时使用）
#define LMCHAN_SECRET_API_BASE_URL    ""
#define LMCHAN_SECRET_API_PATH        "/v1/chat/completions"

// 可选
#define LMCHAN_SECRET_SEARCH_KEY      ""
#define LMCHAN_SECRET_PROXY_HOST      ""
#define LMCHAN_SECRET_PROXY_PORT      ""
#define LMCHAN_SECRET_PROXY_TYPE      ""             // http | socks5
```

### 3) 构建与烧录

```bash
idf.py fullclean && idf.py build
idf.py -p PORT flash monitor
```

Windows 串口一般是 `COMx`（如 `COM6`）。

## 运行时配置（串口 CLI）

提示符为 `lmchan>`。

### 常用配置命令

```text
set_wifi <ssid> <password>
set_fs_app_id <app_id>
set_fs_app_secret <app_secret>
set_fs_ws_url <ws_url>
set_fs_group_mode <mention|all>
fs_send_image <chat_id> <spiffs_path>
fs_send_file <chat_id> <spiffs_path> [file_type]
fs_send_audio <chat_id> <spiffs_path>
set_api_key <key>
set_model_provider <anthropic|openai|openai_compatible>
set_model <model>
set_api_base <https://...>
set_api_path </v1/...>
set_proxy <host> <port> [http|socks5]
clear_proxy
set_search_key <key>
config_show
config_reset
```

### 运维命令

```text
wifi_status
wifi_scan
memory_read
memory_write "..."
session_list
session_clear <chat_id>
heap_info
heartbeat_trigger
set_heartbeat_interval <minutes>
cron_start
cron_template_daily_digest <HH:MM> <chat_id>
cron_template_reminder <seconds> <message> <chat_id>
tool_exec <name> '{...json...}'
restart
```

## 已实现功能

- Feishu 长连接收发（私聊 + 群聊）
- Feishu 非文本消息支持（image/audio/file）与卡片回调事件接入
- 群聊 `@` 触发控制（`mention|all`）
- LLM Provider：Anthropic / OpenAI / OpenAI-compatible
- 自定义 API 端点：`base_url + api_path`
- ReAct 工具调用循环
- 工具：`web_search`、`get_current_time`、`read_file`、`write_file`、`edit_file`、`list_dir`、`cron_add/list/remove`、`subagent_create/status/list/cancel`
- 本地记忆：`SOUL.md`、`USER.md`、`MEMORY.md`、每日笔记、会话历史
- 会话自动压缩归档（summary）
- Cron 与 Heartbeat
- 子代理后台任务执行模型（单 worker 串行，完成后自动回传原会话）
- OTA 管理
- 入站消息自动“思考中”反应，回复发送后自动取消（接口不支持时降级为占位消息）
- 默认灭灯；处理请求时低亮度彩虹呼吸动效（可在 `lmchan_config.h` 调 LED 引脚/亮度）

## 存储结构（SPIFFS）

- `/spiffs/config/SOUL.md`
- `/spiffs/config/USER.md`
- `/spiffs/config/AGENTS.md`
- `/spiffs/config/TOOLS.md`
- `/spiffs/memory/MEMORY.md`
- `/spiffs/memory/YYYY-MM-DD.md`
- `/spiffs/s_XXXXXXXX.jsonl`（会话）
- `/spiffs/s_XXXXXXXX_sum.md`（压缩摘要）
- `/spiffs/cron.json`
- `/spiffs/subagent_jobs.json`
- `/spiffs/HEARTBEAT.md`

## 兼容性说明

- Telegram 代码仍在仓库中，但默认不参与编译，启动入口已禁用。
- WebSocket 网关代码仍在仓库中，但默认不参与编译，启动入口已禁用。

## 文档

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)
- [docs/TODO.md](docs/TODO.md)

## 贡献者

感谢所有为 MimiClaw 做出贡献的开发者。

<a href="https://github.com/memovai/mimiclaw/graphs/contributors">
  <img src="https://contrib.rocks/image?repo=memovai/mimiclaw" alt="MimiClaw contributors" />
</a>

## 许可证

MIT
