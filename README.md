# goosecode

A local AI coding agent written in C, inspired by the Claude Code architecture. Uses the OpenAI API (compatible with Ollama, vLLM, LM Studio, OpenRouter, and any OpenAI-compatible endpoint).

## Features

- **19 tools**: bash, file read/write/edit, glob/grep search, web fetch/search, todo management, skills, sub-agents, notebook editing, REPL execution, and more
- **10 slash commands**: /help, /model, /session, /compact, /permissions, /clear, /cost, /tools, /exit
- **SSE streaming** responses from the API
- **Session persistence** with save/load/resume
- **Context compaction** when conversations get long
- **Permission system** with 5 modes (read-only → allow-all)
- **Multi-source config**: env vars, project settings, user settings
- **Zero TUI dependencies** — works in any terminal

## Quick Start

### Build

```bash
make
```

### Run

```bash
# Interactive REPL
./goosecode

# Single-turn query
./goosecode "explain the architecture of this project"

# With a local Ollama instance
export OPENAI_BASE_URL=http://localhost:11434/v1
export OPENAI_MODEL=llama3
./goosecode

# With a specific model
./goosecode --model gpt-4o-mini "write a fibonacci function in C"
```

## Configuration

### Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `OPENAI_API_KEY` | (required) | Your API key |
| `OPENAI_BASE_URL` | `https://api.openai.com/v1` | API endpoint |
| `OPENAI_MODEL` | `gpt-4o` | Model to use |
| `GOOSECODE_PERMS` | `prompt` | Default permission mode |
| `GOOSECODE_MAX_TURNS` | `16` | Max tool-use iterations |

### Permission Modes

| Mode | Description |
|------|-------------|
| `read-only` | Only read-only tools allowed |
| `workspace-write` | Read + file write tools |
| `danger-full-access` | Almost all tools allowed |
| `prompt` | Ask before each tool use (default) |
| `allow` | All tools auto-approved |

### Settings Files

- `~/.goosecode/settings.json` — User-level settings
- `.goosecode/settings.json` — Project-level settings

## Tools

| Tool | Mode | Description |
|------|------|-------------|
| `bash` | workspace-write | Execute shell commands |
| `read_file` | read-only | Read file contents |
| `write_file` | workspace-write | Create/overwrite files |
| `edit_file` | workspace-write | String replacement edits |
| `glob_search` | read-only | Find files by pattern |
| `grep_search` | read-only | Search file contents |
| `web_fetch` | read-only | Fetch URL content |
| `web_search` | read-only | Web search via DuckDuckGo |
| `todo_write` | workspace-write | Manage todo list |
| `skill` | read-only | Load skill files |
| `agent` | danger-full-access | Spawn sub-agents |
| `tool_search` | read-only | List available tools |
| `notebook_edit` | workspace-write | Edit Jupyter notebooks |
| `sleep` | read-only | Wait for duration |
| `send_message` | read-only | Send message to user |
| `config` | read-only | View/modify config |
| `structured_output` | read-only | Format structured output |
| `repl` | danger-full-access | Execute code in REPL |
| `powershell` | workspace-write | Execute PowerShell commands |

## Dependencies

- **libcurl** — HTTP client
- **cJSON** — JSON parsing
- **ncurses** — Terminal I/O
- **gcc** (C11) — Compiler

Install on Debian/Ubuntu:
```bash
sudo apt install libcurl4-openssl-dev libcjson-dev libncurses-dev
```

Install on macOS:
```bash
brew install curl cJSON ncurses
```

## Architecture

```
src/
├── main.c              # Entry point, CLI parsing
├── agent.c/h           # Agent loop: stream → parse → execute → repeat
├── api.c/h             # OpenAI API client (SSE streaming)
├── config.c/h          # Multi-source configuration
├── session.c/h         # Session persistence and compaction
├── compact.c/h         # Context compaction
├── permissions.c/h     # Permission checking
├── prompt.c/h          # System prompt assembly
├── tools/              # 19 tool implementations
├── commands/           # 10 slash commands
└── util/               # strbuf, http, sse, json, terminal, markdown
```

## License

MIT
