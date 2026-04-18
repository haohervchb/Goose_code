CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -O2 -std=c11 -D_GNU_SOURCE -Isrc
LDFLAGS = -lcurl -lpthread 
DEPFLAGS = -MMD -MP
PREFIX  ?= $(HOME)/.local
INSTALL_BINDIR ?= $(PREFIX)/bin

SRCDIR  = src
OBJDIR  = build
BINDIR  = .

UTIL_SRCS = $(SRCDIR)/util/cJSON.c \
            $(SRCDIR)/util/strbuf.c \
            $(SRCDIR)/util/http.c \
            $(SRCDIR)/util/sse.c \
            $(SRCDIR)/util/json_util.c \
            $(SRCDIR)/util/terminal.c \
            $(SRCDIR)/util/markdown.c \
            $(SRCDIR)/util/early_input.c \
            $(SRCDIR)/util/tui_protocol.c

TOOL_SRCS = $(SRCDIR)/tools/tools.c \
            $(SRCDIR)/tools/bash.c \
            $(SRCDIR)/tools/bash_security.c \
            $(SRCDIR)/tools/file_read.c \
            $(SRCDIR)/tools/file_write.c \
            $(SRCDIR)/tools/file_edit.c \
            $(SRCDIR)/tools/glob_search.c \
            $(SRCDIR)/tools/grep_search.c \
            $(SRCDIR)/tools/web_fetch.c \
            $(SRCDIR)/tools/web_search.c \
            $(SRCDIR)/tools/todo_write.c \
            $(SRCDIR)/tools/task_store.c \
            $(SRCDIR)/tools/task_tools.c \
            $(SRCDIR)/tools/subagent_store.c \
            $(SRCDIR)/tools/mcp_client.c \
            $(SRCDIR)/tools/mcp_tools.c \
            $(SRCDIR)/tools/lsp_client.c \
            $(SRCDIR)/tools/lsp_tool.c \
            $(SRCDIR)/tools/skill.c \
            $(SRCDIR)/tools/agent_tool.c \
            $(SRCDIR)/tools/tool_search.c \
            $(SRCDIR)/tools/notebook_edit.c \
            $(SRCDIR)/tools/sleep.c \
            $(SRCDIR)/tools/send_message.c \
            $(SRCDIR)/tools/ask_user_question.c \
            $(SRCDIR)/tools/plan_mode.c \
            $(SRCDIR)/tools/config_tool.c \
            $(SRCDIR)/tools/structured_out.c \
            $(SRCDIR)/tools/repl_tool.c \
            $(SRCDIR)/tools/powershell.c

CMD_SRCS  = $(SRCDIR)/commands/commands.c \
            $(SRCDIR)/commands/cmd_help.c \
            $(SRCDIR)/commands/cmd_model.c \
            $(SRCDIR)/commands/cmd_session.c \
            $(SRCDIR)/commands/cmd_compact.c \
            $(SRCDIR)/commands/cmd_permissions.c \
            $(SRCDIR)/commands/cmd_clear.c \
            $(SRCDIR)/commands/cmd_cost.c \
            $(SRCDIR)/commands/cmd_exit.c \
            $(SRCDIR)/commands/cmd_plan.c \
            $(SRCDIR)/commands/cmd_config.c \
            $(SRCDIR)/commands/cmd_provider.c \
            $(SRCDIR)/commands/cmd_branch.c \
            $(SRCDIR)/commands/cmd_commit.c \
            $(SRCDIR)/commands/cmd_review.c \
            $(SRCDIR)/commands/cmd_runtime.c \
            $(SRCDIR)/commands/cmd_subagents.c \
            $(SRCDIR)/commands/cmd_tasks.c \
            $(SRCDIR)/commands/cmd_tools.c

CORE_SRCS = $(SRCDIR)/api.c \
            $(SRCDIR)/config.c \
            $(SRCDIR)/provider.c \
            $(SRCDIR)/tool_result_store.c \
            $(SRCDIR)/system_init.c \
            $(SRCDIR)/session.c \
            $(SRCDIR)/session_memory.c \
            $(SRCDIR)/compact.c \
            $(SRCDIR)/permissions.c \
            $(SRCDIR)/prompt_sections.c \
            $(SRCDIR)/prompt.c \
            $(SRCDIR)/agent.c \
            $(SRCDIR)/main.c

ALL_SRCS  = $(UTIL_SRCS) $(TOOL_SRCS) $(CMD_SRCS) $(CORE_SRCS)
ALL_OBJS  = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(ALL_SRCS))
ALL_DEPS  = $(ALL_OBJS:.o=.d)

TARGET    = $(BINDIR)/goosecode-backend
TUI_TARGET = $(BINDIR)/goosecode-tui
GO        ?= $(shell command -v go)

.PHONY: all clean test install uninstall tui

all: backend tui

$(TARGET): $(ALL_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

backend: $(TARGET)

tui:
	cd tui && $(GO) build -o ../$(TUI_TARGET) .
	@ln -sf $(TUI_TARGET) $(BINDIR)/goosecode

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -I$(SRCDIR) -c -o $@ $<

test: $(TARGET)
	@echo "Running tests..."
	@$(CC) $(CFLAGS) -I$(SRCDIR) -o build/test_runner tests/test_api.c $(UTIL_SRCS) $(SRCDIR)/api.c $(SRCDIR)/config.c $(SRCDIR)/provider.c $(SRCDIR)/tool_result_store.c $(SRCDIR)/system_init.c $(SRCDIR)/session.c $(SRCDIR)/session_memory.c $(SRCDIR)/compact.c $(SRCDIR)/permissions.c $(SRCDIR)/prompt_sections.c $(SRCDIR)/prompt.c $(SRCDIR)/agent.c $(TOOL_SRCS) $(CMD_SRCS) $(LDFLAGS) && ./build/test_runner

install: $(TARGET) tui
	@mkdir -p "$(INSTALL_BINDIR)"
	@install -m 755 "$(TARGET)" "$(INSTALL_BINDIR)/goosecode"
	@install -m 755 "$(TUI_TARGET)" "$(INSTALL_BINDIR)/goosecode-tui"
	@printf 'Installed goosecode to %s/goosecode and %s/goosecode-tui\n' "$(INSTALL_BINDIR)" "$(INSTALL_BINDIR)"

uninstall:
	@rm -f "$(INSTALL_BINDIR)/goosecode"
	@printf 'Removed %s/goosecode\n' "$(INSTALL_BINDIR)"

clean:
	rm -rf $(OBJDIR) $(TARGET) build/test_runner

-include $(ALL_DEPS)
