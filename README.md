# goosecode

A claude code like thingy, Vibed by GPT-5.4 with C, just for fun.

`goosecode` is a local AI coding agent written in C with an OpenAI-compatible API client, a tool loop, slash commands, sessions, subagents, MCP support, and a terminal-first workflow.

It works with:
- local OpenAI-compatible servers such as Ollama, vLLM, LM Studio, text-generation-webui proxies, or custom gateways
- hosted OpenAI-compatible providers such as OpenAI, OpenRouter, Together, Fireworks, Groq, or self-hosted proxies

## What It Can Do

- interactive REPL with multiline input, history, arrows, and slash-command completion
- one-shot prompt mode from the shell
- file editing and shell execution
- task tracking and plan mode
- resumable subagents and optional git worktrees
- MCP resource listing/reading
- LSP queries
- local git workflow commands like `/branch`, `/commit`, and `/review`

## Current Surface Area

- Tools: 29
- Slash commands: 16

Main tools include:
- `bash`
- `read_file`, `write_file`, `edit_file`
- `glob_search`, `grep_search`
- `web_fetch`, `web_search`
- `todo_write`, `task_create`, `task_get`, `task_list`, `task_update`
- `ask_user_question`
- `enter_plan_mode`, `exit_plan_mode`
- `agent`
- `list_mcp_resources`, `read_mcp_resource`
- `lsp`
- `repl`, `powershell`

Main slash commands include:
- `/help`
- `/model`
- `/session`
- `/compact`
- `/plan`
- `/config`
- `/tasks`
- `/branch`
- `/commit`
- `/review`
- `/subagents`
- `/permissions`
- `/tools`
- `/exit`

## Build

Requirements:
- `gcc` with C11 support
- `libcurl`
- `pthread` support from libc

The project vendors `cJSON`, so you do not need to install it separately.

### Debian / Ubuntu

```bash
sudo apt update
sudo apt install build-essential libcurl4-openssl-dev
```

### macOS

```bash
brew install curl
```

If Homebrew curl is not on your default compiler path, set the include/library flags yourself before building.

### Build Commands

```bash
make
make test
make clean
```

Binary output:

```bash
./goosecode
```

## Usage

### Interactive REPL

```bash
./goosecode
```

### One-shot Prompt

```bash
./goosecode "explain the architecture of this project"
```

### Common CLI Flags

```text
--model <model>
--base-url <url>
--permission <mode>
--max-turns <n>
--session <id>
--help
```

### Example Invocations

```bash
# interactive
./goosecode

# one-shot
./goosecode "write a fibonacci function in C"

# override model for one run
./goosecode --model gpt-4o-mini "summarize this repository"

# resume a saved session
./goosecode --session 1775092052_390207107

# set permissive mode for a local sandbox session
./goosecode --permission allow
```

## Connecting To Providers

`goosecode` talks to any server that exposes an OpenAI-compatible `/v1` API.

### Environment Variables

```bash
export OPENAI_BASE_URL=...
export OPENAI_MODEL=...
export OPENAI_API_KEY=...
```

`OPENAI_API_KEY` is optional for many local servers.

### Local Providers

Use this when your model server is on the same machine or LAN.

Examples:

```bash
# local gateway on port 8083
export OPENAI_BASE_URL=http://localhost:8083/v1
export OPENAI_MODEL=cyankiwi/Qwen3.5-122B-A10B-AWQ-8bit
./goosecode

# Ollama
export OPENAI_BASE_URL=http://localhost:11434/v1
export OPENAI_MODEL=llama3
./goosecode

# LM Studio or vLLM
export OPENAI_BASE_URL=http://localhost:1234/v1
export OPENAI_MODEL=your-model-name
./goosecode
```

Notes:
- many local servers ignore `OPENAI_API_KEY`
- the base URL should usually end in `/v1`
- model names must match what your server exposes

### External Providers

Use this when talking to hosted services.

Examples:

```bash
# OpenAI
export OPENAI_BASE_URL=https://api.openai.com/v1
export OPENAI_API_KEY=sk-...
export OPENAI_MODEL=gpt-4o
./goosecode

# OpenRouter
export OPENAI_BASE_URL=https://openrouter.ai/api/v1
export OPENAI_API_KEY=sk-or-...
export OPENAI_MODEL=openai/gpt-4o-mini
./goosecode

# Together / Fireworks / Groq / any compatible host
export OPENAI_BASE_URL=https://your-provider.example/v1
export OPENAI_API_KEY=...
export OPENAI_MODEL=provider-model-name
./goosecode
```

Notes:
- if requests fail, first confirm the endpoint is OpenAI-compatible
- if streaming behaves oddly, test a simple non-tool prompt first
- hosted providers usually require `OPENAI_API_KEY`

### Settings Files

Configuration is loaded from:

- `~/.goosecode/settings.json`
- `.goosecode/settings.json`

Project settings override user settings where applicable.

Example project config:

```json
{
  "base_url": "http://localhost:8083/v1",
  "model": "cyankiwi/Qwen3.5-122B-A10B-AWQ-8bit",
  "permission_mode": "allow",
  "max_turns": 64
}
```

## Permissions

Supported permission modes:

- `read-only`
- `workspace-write`
- `danger-full-access`
- `prompt`
- `allow`

Examples:

```bash
./goosecode --permission read-only
./goosecode --permission allow
```

Environment override:

```bash
export GOOSECODE_PERMS=allow
```

## Useful REPL Notes

Editor controls:

- Left / Right: move cursor
- Up / Down: history recall
- `Tab`: complete slash commands
- `Ctrl+A`: start of line
- `Ctrl+E`: end of line
- `Ctrl+J`: insert newline into the current prompt

Examples:

```text
/tasks create investigate parser failure
/plan set
1. Reproduce
2. Fix
.
/review
```

## Bash Tool Timeout

The `bash` tool supports a configurable timeout in seconds.

That matters for commands like:
- `make`
- `cargo build`
- `docker build`
- long-running test suites

Example tool call shape:

```json
{
  "command": "docker build -t app .",
  "timeout": 1800
}
```

Notes:
- default timeout: `120` seconds
- maximum timeout: `7200` seconds
- both numeric and numeric-string `timeout` values are accepted

## Sessions, Tasks, and Subagents

Examples:

```text
/session
/tasks
/tasks create add logging around API failures
/subagents
```

Stored state lives under:

- `~/.goosecode/sessions`
- `~/.goosecode/subagents`
- `~/.goosecode/worktrees`
- `~/.goosecode/todos.json`

## MCP and LSP

### MCP

Configure MCP servers in settings:

```json
{
  "mcp_servers": [
    {
      "name": "test",
      "command": "/usr/bin/python3",
      "args": ["/path/to/mcp_server.py"]
    }
  ]
}
```

Supported MCP tools today:
- `list_mcp_resources`
- `read_mcp_resource`

### LSP

Supported LSP actions today:
- `hover`
- `definition`
- `document_symbols`

The `lsp` tool can use:
- default server selection for supported file types
- explicit `server_command` and `server_args`

## Architecture

```text
src/
├── main.c              # Entry point and CLI setup
├── agent.c/h           # REPL + turn loop + tool execution
├── api.c/h             # OpenAI-compatible API client
├── config.c/h          # Env + settings file loading
├── session.c/h         # Session persistence
├── permissions.c/h     # Permission checks
├── prompt.c/h          # System prompt assembly
├── commands/           # Slash commands
├── tools/              # Tool implementations
└── util/               # JSON, SSE, terminal, markdown, buffers, HTTP
```

## Development Notes

Helpful commands:

```bash
make test
./goosecode --help
./goosecode --permission allow
```

When testing interactively, prefer a sandbox working directory instead of the main source tree.

## License

MIT
