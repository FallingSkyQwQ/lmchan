# lmchan TODO / Roadmap

> 状态以当前代码为准，按优先级维护。

## 已完成（近期）

- [x] 全项目命名迁移为 `lmchan`
- [x] provider 支持 `openai_compatible`
- [x] 自定义 API 端点（`set_api_base` + `set_api_path`）
- [x] 默认人格改为「凉面酱」并支持文件覆盖
- [x] `AGENTS.md` / `TOOLS.md` 接入系统提示词
- [x] 会话自动压缩归档
- [x] 心跳间隔运行时配置
- [x] Cron 模板命令（每日摘要 / 周期提醒）
- [x] Telegram / WebSocket 入口禁用，主通道迁移为 Feishu 长连接
- [x] Feishu CLI 配置项（app_id/app_secret/ws_url/group_mode）

## P0（核心能力）

- [ ] Agent 主动写记忆策略优化
  - 现状：具备文件工具，但写记忆行为仍偏被动
  - 目标：稳定沉淀用户画像、偏好、长期事实

- [ ] LLM 异常恢复与重试策略
  - 增强网关波动、超时、限流场景下的可恢复性

## P1（重要增强）

- [ ] Feishu 富媒体消息（图片/语音/文件）
- [ ] Feishu 事件类型扩展（非文本事件、卡片回调）
- [ ] 会话元数据持久化（created_at / updated_at / source）
- [ ] 更细粒度权限控制（按 chat/type 维度）
- [ ] 提示词与工具策略可热更新

## P2（进阶能力）

- [ ] 语音转写链路
- [ ] 子代理/后台任务执行模型
- [ ] 更丰富的 scheduler（cron expression）
- [ ] 多通道再接入（在 Feishu 稳定后按需增加）

## 文档维护规则

- 代码行为变更时，同步更新：
  - `README_CN.md`
  - `docs/ARCHITECTURE.md`
  - 本文件 `docs/TODO.md`
- 以“可运行事实”为准，避免保留历史描述。
