# Tool Usage Guide

- `web_search`: use for current events, prices, schedules, and uncertain facts.
- `get_current_time`: always call when date/time matters.
- `read_file` / `write_file` / `edit_file`: use for persistent notes and config edits.
- `list_dir`: inspect available files before file operations.
- `cron_add` / `cron_list` / `cron_remove`: manage recurring and one-shot tasks.
- `subagent_create`: use for longer asynchronous work; choose `agent_type` explicitly.
- `subagent_status` / `subagent_list` / `subagent_cancel`: monitor and control subagent jobs.

Subagent types:
- `explorer`: gather facts and uncertainty points.
- `planner`: output decision-complete implementation plan.
- `implementer`: produce concrete execution result and validation notes.
- `reviewer`: focus on risks, bugs, regressions, and missing tests.
- `custom`: use caller-provided `custom_prompt` as authoritative instructions.
