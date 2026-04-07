# TUI Implementation Plan for goosecode

## 1. Overview

Replace the current C REPL with a Go-based TUI (bubbletea) that communicates with goosecode via subprocess IPC. **TUI is the default**; `--repl` flag falls back to old REPL.

**Architecture**:
```
┌─────────────────┐     JSON/stdio      ┌─────────────────┐
│   Go TUI        │ ◄─────────────────► │   goosecode     │
│  (bubbletea)    │                     │   (C backend)   │
└─────────────────┘                     └─────────────────┘
```

TUI is the parent process that spawns goosecode as subprocess - matching opencode's pattern where TUI is the "face" and the backend is the "brain".

---

## 2. Protocol Specification

### TUI → goosecode (JSON over stdio)

```json
{"type": "init", "working_dir": "/path", "config": {"model": "llama3"}}
{"type": "prompt", "text": "explain this code"}
{"type": "command", "name": "plan", "args": "set"}
{"type": "quit"}
{"type": "ping"}
```

### goosecode → TUI

```json
{"type": "init_ok", "session_id": "abc123"}
{"type": "response", "content": "Here is ", "done": false}
{"type": "tool_start", "name": "bash", "id": "call_123", "args": {"command": "ls"}}
{"type": "tool_output", "id": "call_123", "output": "total 12\n..."}
{"type": "tool_end", "id": "call_123", "success": true}
{"type": "error", "message": "API key missing"}
{"type": "session_info", "message_count": 5, "plan_mode": false}
```

---

## 3. Implementation Phases

### Phase 1: Project Setup (Go TUI Skeleton)
**Goal**: Basic Go project with bubbletea

| Task | Description |
|------|-------------|
| 1.1 | Create `tui/go.mod` - `go mod init github.com/rah/goosecode/tui` |
| 1.2 | Add dependencies: bubbletea, bubbles, lipgloss |
| 1.3 | Create `tui/main.go` - minimal bubbletea app that prints "Starting..." |
| 1.4 | Add `tui` target to Makefile |

**Testing**: `make tui` compiles; `./goosecode-tui` shows window; Ctrl+C exits cleanly

---

### Phase 2: Subprocess Launch Integration
**Goal**: Modify main.c to launch TUI by default

| Task | Description |
|------|-------------|
| 2.1 | Add `--repl` flag parsing in main.c (default: use TUI) |
| 2.2 | When `use_tui=1`: spawn goosecode-tui via execvp |
| 2.3 | TUI parses `--backend` flag to find goosecode path |
| 2.4 | TUI spawns goosecode as child process with stdin/stdout pipes |
| 2.5 | Send init handshake on startup |

**Testing**: `./goosecode` launches TUI; `./goosecode --repl` launches old REPL; Ctrl+C terminates both

---

### Phase 3: Protocol Implementation - Basic I/O
**Goal**: Get prompts flowing to goosecode, responses back to TUI

| Task | Description |
|------|-------------|
| 3.1 | Create `tui/protocol.go` - JSON marshal/unmarshal for all message types |
| 3.2 | Create `tui/backend.go` - manages subprocess, SendPrompt(), ReadMessage() |
| 3.3 | Update bubbletea model: on Enter, send prompt via backend |
| 3.4 | On backend message: append to response buffer |
| 3.5 | Show "Thinking..." indicator while waiting |

**Testing**: Type prompt → response appears in TUI; errors display properly

---

### Phase 4: Tool Call Display
**Goal**: Render tool executions (matching opencode quality)

| Task | Description |
|------|-------------|
| 4.1 | Create `tui/ui/tools.go` - ToolCall, ToolOutput components |
| 4.2 | On `tool_start`: add to pending list, show in viewport |
| 4.3 | On `tool_output`: stream output to buffer |
| 4.4 | On `tool_end`: show success/error indicator |
| 4.5 | Truncate long outputs (>10k chars), add expand hint |

**Styling**: Tool name in bold cyan, args dim white, output monospace, errors in red

**Testing**: Run prompt that triggers tool → see tool bubble, output, success/error styling

---

### Phase 5: Slash Commands & Plan Mode Toggle
**Goal**: Implement commands with Tab toggle for Plan/Build

| Task | Description |
|------|-------------|
| 5.1 | Detect `/` prefix, parse command name + args |
| 5.2 | Send via `{"type": "command", ...}` |
| 5.3 | **Tab key toggles Plan/Build mode** (matching opencode) |
| 5.4 | Implement: /help, /exit, /clear, /model, /session, /plan |
| 5.5 | Show command output as assistant message |

**Testing**: `/help` shows commands; Tab toggles prompt indicator; `/exit` exits cleanly

---

### Phase 6: Chat History & Persistence
**Goal**: Scrollable history, session persistence

| Task | Description |
|------|-------------|
| 6.1 | Use bubbletea.Viewport for scrolling |
| 6.2 | Store `[]Message{role, content, timestamp}` |
| 6.3 | Auto-scroll on new message, PageUp/Down for history |
| 6.4 | On init: request existing session, display on startup |
| 6.5 | `/session list`, `/session new` commands |

**Testing**: Scroll up to see history; restart TUI → previous session loads

---

### Phase 7: Polish & Edge Cases
**Goal**: Robust error handling, edge cases

| Task | Description |
|------|-------------|
| 7.1 | Backend crash: show error, offer to restart |
| 7.2 | Window resize: reflow content |
| 7.3 | Pipe input (`echo "x" | ./goosecode`): detect non-TTY, handle gracefully |
| 7.4 | Loading spinners during API calls |
| 7.5 | Keyboard shortcuts: Ctrl+C, Ctrl+L |

**Testing**: Resize terminal; pipe input from file; backend crash shows recovery

---

## 4. Key Design Decisions (matching opencode)

| Question | Decision |
|----------|----------|
| Session storage | Backend (goosecode) owns - TUI just displays |
| Config sharing | TUI reads directly from `~/.goosecode/` and `./.goosecode/` |
| Plan mode | **Tab toggles Plan/Build** (exact match to opencode) |
| MCP tools | Treat as regular tool calls |
| Streaming | Word-by-word immediate output |

---

## 5. File Structure

```
goosecode/
├── src/
│   ├── main.c           # Modified: adds --repl flag, spawns TUI
│   ├── agent.c          # Unchanged (adds protocol handling)
│   └── ...
├── tui/
│   ├── go.mod
│   ├── main.go          # bubbletea entry point
│   ├── model.go        # Main TUI model
│   ├── protocol.go     # JSON serialization
│   ├── backend.go      # Subprocess management
│   └── ui/
│       ├── prompt.go   # Input component
│       ├── history.go  # Chat history viewport
│       ├── tools.go    # Tool call display
│       └── styles.go   # Lipgloss styles
├── Makefile            # Updated: builds both binaries
└── docs/
    └── tui-plan.md     # This plan
```

---

## 6. Makefile Changes

```makefile
all: goosecode goosecode-tui

goosecode-tui:
	cd tui && go build -o ../$@ .

install: all
	install -m 755 goosecode goosecode-tui $(PREFIX)/bin/
```

---

## 7. Testing Strategy

| Phase | Manual Tests | Automated |
|-------|-------------|-----------|
| 1 | Binary runs, shows window | `go test ./...` |
| 2 | TUI launches via goosecode, --repl works | Subprocess spawn test |
| 3 | Prompt→response flow | Protocol round-trip test |
| 4 | Tools display correctly | Mock tool output test |
| 5 | Commands, Tab toggle | Command parser tests |
| 6 | History persists | Session save/load test |
| 7 | Edge cases | Error path tests |