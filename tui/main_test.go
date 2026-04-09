package main

import (
	"strings"
	"testing"

	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/x/ansi"
)

func TestWindowResizeUpdatesTextInputWidth(t *testing.T) {
	m := newModel(nil)

	updated, _ := m.Update(tea.WindowSizeMsg{Width: 120, Height: 40})
	wide := updated.(model)

	if got := wide.textInput.Width(); got <= 79 {
		t.Fatalf("expected text input width to grow beyond 80, got %d", got)
	}
	if got := wide.textInput.Width(); got != 117 {
		t.Fatalf("expected text input width 117 after prompt/gutter sizing, got %d", got)
	}

	if got := wide.viewportWidth; got != 120 {
		t.Fatalf("expected viewport width 120, got %d", got)
	}

	updated, _ = wide.Update(tea.WindowSizeMsg{Width: 90, Height: 40})
	narrow := updated.(model)

	if narrow.textInput.Width() >= wide.textInput.Width() {
		t.Fatalf("expected text input width to shrink after resize, got %d then %d", wide.textInput.Width(), narrow.textInput.Width())
	}
}

func TestSeparatorStringMatchesWidth(t *testing.T) {
	got := separatorString(17)
	if len([]rune(got)) != 17 {
		t.Fatalf("expected separator width 17, got %d", len([]rune(got)))
	}
}

func TestFormatToolArgsSortsKeys(t *testing.T) {
	got := formatToolArgs(map[string]interface{}{
		"working_dir": "/tmp",
		"command":     "ls",
		"timeout":     120000,
	})

	want := "command=ls, timeout=120000, working_dir=/tmp"
	if got != want {
		t.Fatalf("expected %q, got %q", want, got)
	}
}

func TestFormatToolOutputChunkPrefixesNewLinesAndContinuesOpenLine(t *testing.T) {
	first, open := formatToolOutputChunk("hello", false)
	second, open := formatToolOutputChunk(" world\nnext\n", open)

	if open {
		t.Fatalf("expected tool output line state to close on trailing newline")
	}
	joined := ansi.Strip(first + second)
	if joined != "│ hello world\n│ next\n" {
		t.Fatalf("expected prefixed tool output, got %q", joined)
	}
}

func TestRenderToolOutputEntryCompactsLongOutput(t *testing.T) {
	longOutput := strings.Join([]string{"1", "2", "3", "4", "5", "6", "7"}, "\n") + "\n"
	got := ansi.Strip(renderToolOutputEntry(longOutput, false))

	if !strings.Contains(got, "│ 1\n│ 2\n│ 3\n") {
		t.Fatalf("expected compact tool preview, got %q", got)
	}
	if !strings.Contains(got, "4 more line(s) hidden, Ctrl+O expands") {
		t.Fatalf("expected compact tool summary, got %q", got)
	}
}

func TestRenderToolOutputEntryExpandsLongOutput(t *testing.T) {
	longOutput := strings.Join([]string{"1", "2", "3", "4", "5", "6", "7"}, "\n") + "\n"
	got := ansi.Strip(renderToolOutputEntry(longOutput, true))

	if strings.Contains(got, "hidden, Ctrl+O expands") {
		t.Fatalf("expected expanded tool output without compact summary, got %q", got)
	}
	if !strings.Contains(got, "│ 7\n") {
		t.Fatalf("expected expanded tool output to include all lines, got %q", got)
	}
}

func TestFormatToolStartLineOmitsBlankArgs(t *testing.T) {
	got := ansi.Strip(formatToolStartLine("bash", ""))
	if got != "\ntool> bash\n" {
		t.Fatalf("expected tool start without blank args, got %q", got)
	}
}

func TestFormatToolStartLineShowsArgsAsIndentedBlock(t *testing.T) {
	got := ansi.Strip(formatToolStartLine("bash", "command=ls"))
	if got != "\ntool> bash\n│ command=ls\n" {
		t.Fatalf("expected indented tool args, got %q", got)
	}
}

func TestRenderToolStartEntryKeepsGutterOnWrappedArgs(t *testing.T) {
	got := ansi.Strip(renderToolStartEntryAtWidth("bash", strings.Repeat("x", 20), 8))
	if strings.Count(got, "│ ") < 2 {
		t.Fatalf("expected wrapped tool args to keep gutter, got %q", got)
	}
}

func TestFormatToolEndLineIncludesDoneState(t *testing.T) {
	if got := ansi.Strip(formatToolEndLine(true, "", false)); got != "└ [✓] done\n" {
		t.Fatalf("expected successful tool end line, got %q", got)
	}
}

func TestFormatToolEndLineShowsTruncationState(t *testing.T) {
	if got := ansi.Strip(formatToolEndLine(true, "", true)); got != "└ [✓] done (output truncated)\n" {
		t.Fatalf("expected truncation note in tool end line, got %q", got)
	}
}

func TestFormatUserPromptAddsYouPrefix(t *testing.T) {
	if got := ansi.Strip(formatUserPrompt("hello")); got != "\nyou> hello\n" {
		t.Fatalf("expected formatted user prompt, got %q", got)
	}
}

func TestRenderUserEntryFormatsMultilineInput(t *testing.T) {
	if got := ansi.Strip(renderUserEntry("hello\nthere\n")); got != "\nyou> hello\n│ there\n" {
		t.Fatalf("expected multiline user entry block, got %q", got)
	}
}

func TestRenderUserEntryKeepsGutterOnSoftWrap(t *testing.T) {
	got := ansi.Strip(renderUserEntryAtWidth(strings.Repeat("a", 20), 12))
	if !strings.Contains(got, "\n│ ") {
		t.Fatalf("expected wrapped user entry to keep gutter, got %q", got)
	}
}

func TestRenderCommandEntryFormatsMultilineArgs(t *testing.T) {
	if got := ansi.Strip(renderCommandEntry("model", "gpt-5\nreasoning=high\n")); got != "\ncmd> /model gpt-5\n│ reasoning=high\n" {
		t.Fatalf("expected multiline command block, got %q", got)
	}
}

func TestRenderCommandEntryKeepsGutterOnSoftWrap(t *testing.T) {
	got := ansi.Strip(renderCommandEntryAtWidth("model", strings.Repeat("x", 20), 12))
	if !strings.Contains(got, "\n│ ") {
		t.Fatalf("expected wrapped command entry to keep gutter, got %q", got)
	}
}

func TestFormatCommandFeedbackFormatsSlashCommand(t *testing.T) {
	if got := ansi.Strip(formatCommandFeedback("model", "gpt-5")); got != "\ncmd> /model gpt-5\n" {
		t.Fatalf("expected formatted command feedback, got %q", got)
	}
}

func TestFormatErrorLineAddsErrorPrefix(t *testing.T) {
	if got := ansi.Strip(formatErrorLine("Error: boom")); got != "\nerror> Error: boom\n" {
		t.Fatalf("expected formatted error line, got %q", got)
	}
}

func TestRenderErrorEntryFormatsMultilineError(t *testing.T) {
	if got := ansi.Strip(renderErrorEntry("Error: boom\ndetails\n")); got != "\nerror> Error: boom\n│ details\n" {
		t.Fatalf("expected multiline error block, got %q", got)
	}
}

func TestRenderToolOutputEntryKeepsGutterOnSoftWrap(t *testing.T) {
	got := ansi.Strip(renderToolOutputEntryAtWidth(strings.Repeat("z", 20), true, 8))
	if strings.Count(got, "│ ") < 2 {
		t.Fatalf("expected wrapped tool output to keep gutter on each line, got %q", got)
	}
}

func TestRenderSystemEntryFormatsMultilineInfo(t *testing.T) {
	if got := ansi.Strip(renderSystemEntry("Connected!\nSession abc\n")); got != "\ninfo> Connected!\n│ Session abc\n" {
		t.Fatalf("expected multiline system block, got %q", got)
	}
}

func TestHeaderLineTruncatesStatusToWindowWidth(t *testing.T) {
	line := headerLine("GOOSE CODE [BUILD]", "connected | session abc123 | running bash | /help | /exit", 30)

	if got := ansi.StringWidth(line); got > 30 {
		t.Fatalf("expected header line width <= 30, got %d", got)
	}
	if !strings.Contains(ansi.Strip(line), "...") {
		t.Fatalf("expected truncated header status, got %q", ansi.Strip(line))
	}
}

func TestWindowResizeAccountsForPromptStatusHeight(t *testing.T) {
	m := newModel(nil)
	m.activeProvider = "ollama"
	m.activeModel = "llama3-super-long-name"

	updated, _ := m.Update(tea.WindowSizeMsg{Width: 80, Height: 30})
	wide := updated.(model)

	updated, _ = wide.Update(tea.WindowSizeMsg{Width: 40, Height: 30})
	narrow := updated.(model)

	if narrow.viewport.Height < wide.viewport.Height {
		t.Fatalf("expected compact footer to preserve viewport height on narrow terminals, got %d then %d", wide.viewport.Height, narrow.viewport.Height)
	}
}

func TestWrapTextLimitsVisibleLineWidth(t *testing.T) {
	wrapped := wrapText("\033[32m"+strings.Repeat("a", 24)+"\033[0m", 10)

	for _, line := range strings.Split(wrapped, "\n") {
		if got := ansi.StringWidth(line); got > 10 {
			t.Fatalf("expected wrapped line width <= 10, got %d in %q", got, ansi.Strip(line))
		}
	}
}

func TestWindowResizeRewrapsViewportContent(t *testing.T) {
	m := newModel(nil)

	updated, _ := m.Update(responseMsg(strings.Repeat("b", 30)))
	beforeResize := updated.(model)
	beforeLines := beforeResize.viewport.TotalLineCount()

	updated, _ = beforeResize.Update(tea.WindowSizeMsg{Width: 10, Height: 20})
	afterResize := updated.(model)

	if afterResize.viewport.TotalLineCount() <= beforeLines {
		t.Fatalf("expected viewport content to rewrap to more lines after resize, got %d then %d", beforeLines, afterResize.viewport.TotalLineCount())
	}
	if got := ansi.StringWidth(afterResize.viewport.View()); got <= 10 && afterResize.viewport.TotalLineCount() == 1 {
		t.Fatalf("expected wrapped viewport content after resize")
	}
}

func TestViewLeavesRightGutterToAvoidSeparatorWrap(t *testing.T) {
	m := newModel(nil)
	updated, _ := m.Update(tea.WindowSizeMsg{Width: 20, Height: 12})
	narrow := updated.(model)
	updated, _ = narrow.Update(responseMsg(strings.Repeat("x", 80)))
	rendered := updated.(model)

	lines := strings.Split(rendered.View(), "\n")
	for _, line := range lines[4:] {
		if got := ansi.StringWidth(line); got >= 20 {
			t.Fatalf("expected rendered line width < terminal width to avoid wrap, got %d in %q", got, ansi.Strip(line))
		}
	}
	if !strings.Contains(ansi.Strip(rendered.View()), strings.Repeat("─", 19)) {
		t.Fatalf("expected separator line to remain visible in rendered view")
	}
}

func TestPgUpAndPgDownScrollViewport(t *testing.T) {
	m := newModel(nil)
	m.viewportHeightForTest(4)
	m.seedTranscript(strings.Join([]string{"1", "2", "3", "4", "5", "6", "7", "8"}, "\n"))
	m.syncViewport(true)
	start := m.viewport.YOffset

	updated, _ := m.Update(tea.KeyMsg{Type: tea.KeyPgUp})
	up := updated.(model)
	if up.viewport.YOffset >= start {
		t.Fatalf("expected pgup to move viewport up, got %d then %d", start, up.viewport.YOffset)
	}

	updated, _ = up.Update(tea.KeyMsg{Type: tea.KeyPgDown})
	down := updated.(model)
	if down.viewport.YOffset <= up.viewport.YOffset {
		t.Fatalf("expected pgdown to move viewport down, got %d then %d", up.viewport.YOffset, down.viewport.YOffset)
	}
}

func TestHomeAndEndJumpViewport(t *testing.T) {
	m := newModel(nil)
	m.viewportHeightForTest(4)
	m.seedTranscript(strings.Join([]string{"1", "2", "3", "4", "5", "6", "7", "8"}, "\n"))
	m.syncViewport(true)
	m.viewport.LineUp(2)
	m.hasUnseenOutput = true

	updated, _ := m.Update(tea.KeyMsg{Type: tea.KeyHome})
	atTop := updated.(model)
	if !atTop.viewport.AtTop() {
		t.Fatalf("expected home to jump to top")
	}

	updated, _ = atTop.Update(tea.KeyMsg{Type: tea.KeyEnd})
	atBottom := updated.(model)
	if !atBottom.viewport.AtBottom() {
		t.Fatalf("expected end to jump to bottom")
	}
	if atBottom.hasUnseenOutput {
		t.Fatalf("expected end to clear unread output hint")
	}
}

func TestViewKeepsGooseBannerAndShowsStatus(t *testing.T) {
	m := newModel(&Backend{sessionID: "abc123"})
	m.connected = true
	m.isRunning = true
	m.currentTool = "bash"
	m.viewportWidth = 120
	m.entries = []transcriptEntry{{kind: transcriptSystem, text: "Connected!\nSession abc123\n"}}
	m.syncViewport(true)

	view := ansi.Strip(m.View())

	if !strings.Contains(view, "___( o)>") {
		t.Fatalf("expected goose banner to remain in the header")
	}
	if !strings.Contains(view, "session abc123") {
		t.Fatalf("expected session status in header, got %q", view)
	}
	if !strings.Contains(view, "running bash") {
		t.Fatalf("expected running tool status in header, got %q", view)
	}
	if !strings.Contains(view, "info> Connected!") {
		t.Fatalf("expected system transcript block in view, got %q", view)
	}
}

func TestTabToggleDoesNotAppendTranscriptNoise(t *testing.T) {
	m := newModel(nil)

	updated, _ := m.Update(tea.KeyMsg{Type: tea.KeyTab})
	toggled := updated.(model)

	if !toggled.planMode {
		t.Fatalf("expected plan mode to be enabled after tab toggle")
	}
	if toggled.output != "" {
		t.Fatalf("expected mode toggle to stay out of transcript, got %q", toggled.output)
	}
	if !strings.Contains(ansi.Strip(toggled.View()), "[PLAN]") {
		t.Fatalf("expected header to reflect plan mode after tab toggle")
	}
}

func TestViewShowsPromptStatusRow(t *testing.T) {
	m := newModel(nil)
	m.activeProvider = "ollama"
	m.activeModel = "llama3"
	m.viewportWidth = 120
	m.relayout()

	view := ansi.Strip(m.View())

	for _, needle := range []string{"ollama/llama3", "mode BUILD", "Tab toggles mode", "PgUp/PgDn scroll", "Home/End jump", "/clear resets", "Ctrl+O to", "ggles last tool block", "transcript"} {
		if !strings.Contains(view, needle) {
			t.Fatalf("expected prompt status to contain %q, got %q", needle, view)
		}
	}
}

func TestPromptStatusShowsHelpHintOnWideTerminals(t *testing.T) {
	m := newModel(nil)
	m.viewportWidth = 140
	m.relayout()
	view := ansi.Strip(m.View())

	if !strings.Contains(view, "F1 help over") || !strings.Contains(view, "lay") {
		t.Fatalf("expected footer help hint on wide terminals, got %q", view)
	}
}

func TestPromptStatusCompactsOnNarrowTerminals(t *testing.T) {
	m := newModel(nil)
	m.activeProvider = "ollama"
	m.activeModel = "llama3"
	m.viewportWidth = 40
	m.relayout()

	view := ansi.Strip(m.View())
	if !strings.Contains(view, "mode BUILD") {
		t.Fatalf("expected compact prompt status to keep mode, got %q", view)
	}
	for _, hidden := range []string{"PgUp/PgDn scroll", "Home/End jump", "/clear resets transcript", "F1 help overlay"} {
		if strings.Contains(view, hidden) {
			t.Fatalf("expected compact prompt status to hide %q, got %q", hidden, view)
		}
	}
}

func TestF1TogglesHelpOverlay(t *testing.T) {
	m := newModel(nil)
	m.viewportWidth = 120
	m.relayout()

	updated, _ := m.Update(tea.KeyMsg{Type: tea.KeyF1})
	help := updated.(model)
	view := ansi.Strip(help.View())

	if !help.showHelp {
		t.Fatalf("expected F1 to open help overlay")
	}
	for _, needle := range []string{"Help", "Navigation", "Prompt", "Tools", "Commands"} {
		if !strings.Contains(view, needle) {
			t.Fatalf("expected help overlay to contain %q, got %q", needle, view)
		}
	}
}

func TestEscClosesHelpOverlay(t *testing.T) {
	m := newModel(nil)
	m.showHelp = true

	updated, _ := m.Update(tea.KeyMsg{Type: tea.KeyEsc})
	closed := updated.(model)

	if closed.showHelp {
		t.Fatalf("expected esc to close help overlay")
	}
}

func TestSlashHelpOpensOverlayLocally(t *testing.T) {
	m := newModel(nil)
	m.textInput.SetValue("/help")

	updated, _ := m.Update(tea.KeyMsg{Type: tea.KeyEnter})
	help := updated.(model)

	if !help.showHelp {
		t.Fatalf("expected /help to open the local help overlay")
	}
	if strings.Contains(ansi.Strip(help.output), "cmd> /help") {
		t.Fatalf("expected /help to stay local instead of appending command feedback, got %q", ansi.Strip(help.output))
	}
}

func TestSlashHelpOffClosesOverlayLocally(t *testing.T) {
	m := newModel(nil)
	m.showHelp = true
	m.textInput.SetValue("/help off")

	updated, _ := m.Update(tea.KeyMsg{Type: tea.KeyEnter})
	closed := updated.(model)

	if closed.showHelp {
		t.Fatalf("expected /help off to close the local help overlay")
	}
}

func TestPromptViewHidesLineNumbers(t *testing.T) {
	m := newModel(nil)
	view := ansi.Strip(m.textInput.View())

	if strings.Contains(view, ">   1 ") {
		t.Fatalf("expected textarea prompt to hide editor-style line numbers, got %q", view)
	}
}

func TestClearCommandResetsViewportContent(t *testing.T) {
	m := newModel(nil)
	m.seedTranscript("existing output")
	m.syncViewport(true)
	m.textInput.SetValue("/clear")

	updated, _ := m.Update(tea.KeyMsg{Type: tea.KeyEnter})
	cleared := updated.(model)

	if cleared.output != "" {
		t.Fatalf("expected clear command to reset transcript output, got %q", cleared.output)
	}
	if strings.TrimSpace(ansi.Strip(cleared.viewport.View())) != "" {
		t.Fatalf("expected clear command to clear viewport content, got %q", ansi.Strip(cleared.viewport.View()))
	}
}

func TestPromptSubmitDoesNotDuplicateSentLine(t *testing.T) {
	m := newModel(nil)
	m.textInput.SetValue("hello world")

	updated, _ := m.Update(tea.KeyMsg{Type: tea.KeyEnter})
	submitted := updated.(model)
	view := ansi.Strip(submitted.viewport.View())

	if !strings.Contains(view, "you> hello world") {
		t.Fatalf("expected submitted prompt to appear in transcript, got %q", view)
	}
	if strings.Contains(view, "[Sent] hello world") {
		t.Fatalf("expected submit flow to avoid duplicate sent line, got %q", view)
	}
}

func TestPromptSubmitRendersMultilineUserBlock(t *testing.T) {
	m := newModel(nil)
	m.textInput.SetValue("hello\nworld")

	updated, _ := m.Update(tea.KeyMsg{Type: tea.KeyEnter})
	submitted := updated.(model)
	view := ansi.Strip(submitted.output)

	if !strings.Contains(view, "you> hello\n│ world") {
		t.Fatalf("expected multiline user prompt block in transcript, got %q", view)
	}
}

func TestSlashCommandUsesCommandFeedbackPrefix(t *testing.T) {
	m := newModel(nil)
	m.textInput.SetValue("/model gpt-5")

	updated, _ := m.Update(tea.KeyMsg{Type: tea.KeyEnter})
	commanded := updated.(model)
	transcript := ansi.Strip(commanded.output)

	if !strings.Contains(transcript, "cmd> /model gpt-5") {
		t.Fatalf("expected slash command feedback in transcript, got %q", transcript)
	}
}

func TestCommandEntryRendersMultilineBlock(t *testing.T) {
	m := newModel(nil)
	m.appendTranscriptEntry(transcriptCommand, "model", "gpt-5\nreasoning=high", false)
	m.syncViewport(true)
	transcript := ansi.Strip(m.output)

	if !strings.Contains(transcript, "cmd> /model gpt-5\n│ reasoning=high") {
		t.Fatalf("expected multiline command block in transcript, got %q", transcript)
	}
}

func TestBackendErrorsUseErrorPrefix(t *testing.T) {
	m := newModel(nil)

	updated, _ := m.Update(backendErrorMsg("Error: boom"))
	errored := updated.(model)
	transcript := ansi.Strip(errored.output)

	if !strings.Contains(transcript, "error> Error: boom") {
		t.Fatalf("expected labeled error in transcript, got %q", transcript)
	}
}

func TestBackendErrorsRenderMultilineBlock(t *testing.T) {
	m := newModel(nil)

	updated, _ := m.Update(backendErrorMsg("Error: boom\ndetails"))
	errored := updated.(model)
	transcript := ansi.Strip(errored.output)

	if !strings.Contains(transcript, "error> Error: boom\n│ details") {
		t.Fatalf("expected multiline error block in transcript, got %q", transcript)
	}
}

func TestAssistantReplyGetsSinglePrefixAcrossChunks(t *testing.T) {
	m := newModel(nil)
	m.textInput.SetValue("hello")

	updated, _ := m.Update(tea.KeyMsg{Type: tea.KeyEnter})
	submitted := updated.(model)
	updated, _ = submitted.Update(responseMsg("Hi"))
	updated, _ = updated.(model).Update(responseMsg(" there"))
	replied := updated.(model)
	transcript := ansi.Strip(replied.output)

	if strings.Count(transcript, "goose>") != 1 {
		t.Fatalf("expected one assistant prefix for streamed reply, got %q", transcript)
	}
	if !strings.Contains(transcript, "goose> Hi there") {
		t.Fatalf("expected assistant reply to be prefixed once, got %q", transcript)
	}
}

func TestAssistantPrefixRearmsForNextReply(t *testing.T) {
	m := newModel(nil)
	m.textInput.SetValue("first")

	updated, _ := m.Update(tea.KeyMsg{Type: tea.KeyEnter})
	updated, _ = updated.(model).Update(responseMsg("One\n"))
	firstReply := updated.(model)
	firstReply.textInput.SetValue("second")

	updated, _ = firstReply.Update(tea.KeyMsg{Type: tea.KeyEnter})
	updated, _ = updated.(model).Update(responseMsg("Two"))
	secondReply := updated.(model)
	transcript := ansi.Strip(secondReply.output)

	if strings.Count(transcript, "goose>") != 2 {
		t.Fatalf("expected assistant prefix for each reply, got %q", transcript)
	}
	if !strings.Contains(transcript, "goose> One") || !strings.Contains(transcript, "goose> Two") {
		t.Fatalf("expected both replies to carry assistant prefixes, got %q", transcript)
	}
}

func TestAssistantRepliesRenderAsMultiLineBlocks(t *testing.T) {
	m := newModel(nil)
	m.textInput.SetValue("hello")

	updated, _ := m.Update(tea.KeyMsg{Type: tea.KeyEnter})
	updated, _ = updated.(model).Update(responseMsg("line one\nline two\n"))
	replied := updated.(model)
	transcript := ansi.Strip(replied.output)

	if !strings.Contains(transcript, "goose> line one\n│ line two") {
		t.Fatalf("expected multi-line assistant block rendering, got %q", transcript)
	}
}

func TestTranscriptStoresSeparateTypedEntries(t *testing.T) {
	m := newModel(nil)
	m.textInput.SetValue("hello")

	updated, _ := m.Update(tea.KeyMsg{Type: tea.KeyEnter})
	updated, _ = updated.(model).Update(responseMsg("Hi"))
	updated, _ = updated.(model).Update(toolStartMsg{id: "call_1", name: "bash", args: "command=ls"})
	state := updated.(model)

	if len(state.entries) < 3 {
		t.Fatalf("expected transcript entries for user, assistant, and tool, got %d", len(state.entries))
	}
	if state.entries[0].kind != transcriptUser || state.entries[1].kind != transcriptAssistant || state.entries[2].kind != transcriptToolStart {
		t.Fatalf("expected typed transcript entries, got %#v", state.entries[:3])
	}
}

func TestViewportAutoFollowsWhenAlreadyAtBottom(t *testing.T) {
	m := newModel(nil)
	m.viewportHeightForTest(4)
	m.seedTranscript(strings.Join([]string{"1", "2", "3", "4", "5", "6"}, "\n"))
	m.syncViewport(true)

	updated, _ := m.Update(responseMsg("7\n"))
	after := updated.(model)

	if !after.viewport.AtBottom() {
		t.Fatalf("expected viewport to stay at bottom when new output arrives at bottom")
	}
}

func TestViewportPreservesScrollWhenUserScrolledUp(t *testing.T) {
	m := newModel(nil)
	m.viewportHeightForTest(4)
	m.seedTranscript(strings.Join([]string{"1", "2", "3", "4", "5", "6"}, "\n"))
	m.syncViewport(true)
	m.viewport.LineUp(2)

	updated, _ := m.Update(responseMsg("7\n"))
	after := updated.(model)

	if after.viewport.AtBottom() {
		t.Fatalf("expected viewport to preserve scroll position when user is reading older output")
	}
}

func TestBannerShowsUnreadOutputHintWhenScrolledUp(t *testing.T) {
	m := newModel(nil)
	m.viewportHeightForTest(4)
	m.viewportWidth = 120
	m.relayout()
	m.seedTranscript(strings.Join([]string{"1", "2", "3", "4", "5", "6"}, "\n"))
	m.syncViewport(true)
	m.viewport.LineUp(2)

	updated, _ := m.Update(responseMsg("7\n"))
	after := updated.(model)

	if !after.hasUnseenOutput {
		t.Fatalf("expected unseen output flag after streaming while scrolled up")
	}
	if !strings.Contains(ansi.Strip(after.View()), "new output below") {
		t.Fatalf("expected banner to show unseen output hint")
	}
}

func TestUnreadOutputHintClearsAtBottom(t *testing.T) {
	m := newModel(nil)
	m.viewportHeightForTest(4)
	m.viewportWidth = 120
	m.relayout()
	m.seedTranscript(strings.Join([]string{"1", "2", "3", "4", "5", "6"}, "\n"))
	m.syncViewport(true)
	m.viewport.LineUp(2)

	updated, _ := m.Update(responseMsg("7\n"))
	withUnread := updated.(model)
	for !withUnread.viewport.AtBottom() {
		updated, _ = withUnread.Update(tea.KeyMsg{Type: tea.KeyDown})
		withUnread = updated.(model)
	}
	atBottom := withUnread

	if atBottom.hasUnseenOutput {
		t.Fatalf("expected unseen output flag to clear after returning to bottom")
	}
}

func TestBannerShowsScrollPercentWhenReadingOlderOutput(t *testing.T) {
	m := newModel(nil)
	m.viewportHeightForTest(4)
	m.viewportWidth = 120
	m.relayout()
	m.seedTranscript(strings.Join([]string{"1", "2", "3", "4", "5", "6", "7", "8"}, "\n"))
	m.syncViewport(true)
	m.viewport.LineUp(2)

	view := ansi.Strip(m.View())
	if !strings.Contains(view, "scroll ") {
		t.Fatalf("expected banner to show scroll percent while away from bottom, got %q", view)
	}
}

func TestToolOutputIsPrefixedInTranscript(t *testing.T) {
	m := newModel(nil)
	m.entries = []transcriptEntry{{kind: transcriptToolOutput, text: "line one\nline two\n", expanded: true}}
	m.syncViewport(true)

	view := ansi.Strip(m.viewport.View())

	for _, needle := range []string{"│ line one", "│ line two"} {
		if !strings.Contains(view, needle) {
			t.Fatalf("expected tool output transcript to contain %q, got %q", needle, view)
		}
	}
}

func TestLongToolOutputStartsCompactAndCtrlOTogglesExpansion(t *testing.T) {
	m := newModel(nil)
	m.currentToolID = "call_1"
	longOutput := strings.Join([]string{"1", "2", "3", "4", "5", "6", "7"}, "\n") + "\n"

	updated, _ := m.Update(toolOutputMsg{id: "call_1", output: longOutput})
	compact := updated.(model)
	compactTranscript := ansi.Strip(compact.output)

	if !strings.Contains(compactTranscript, "4 more line(s) hidden, Ctrl+O expands") {
		t.Fatalf("expected compact tool output summary by default, got %q", compactTranscript)
	}

	updated, _ = compact.Update(tea.KeyMsg{Type: tea.KeyCtrlO})
	expanded := updated.(model)
	expandedTranscript := ansi.Strip(expanded.output)

	if strings.Contains(expandedTranscript, "hidden, Ctrl+O expands") {
		t.Fatalf("expected expanded tool output after Ctrl+O, got %q", expandedTranscript)
	}
	if !strings.Contains(expandedTranscript, "│ 7") {
		t.Fatalf("expected expanded tool output to show later lines, got %q", expandedTranscript)
	}

	updated, _ = expanded.Update(tea.KeyMsg{Type: tea.KeyCtrlO})
	recollapsed := updated.(model)
	if !strings.Contains(ansi.Strip(recollapsed.output), "4 more line(s) hidden, Ctrl+O expands") {
		t.Fatalf("expected Ctrl+O to collapse the last tool block again, got %q", ansi.Strip(recollapsed.output))
	}
}

func TestToolEndStartsOnNewLineAfterOpenOutput(t *testing.T) {
	m := newModel(nil)
	m.currentToolID = "call_1"
	m.toolOutputLineOpen = true
	formatted, _ := formatToolOutputChunk("partial", false)
	m.entries = []transcriptEntry{{kind: transcriptToolOutput, text: "partial"}}
	m.output = formatted

	updated, _ := m.Update(toolEndMsg{id: "call_1", success: true})
	after := updated.(model)
	transcript := ansi.Strip(after.output)

	if !strings.Contains(transcript, "│ partial\n└ [✓] done") {
		t.Fatalf("expected tool result on a new line after partial output, got %q", transcript)
	}
}

func TestToolEndShowsTruncationNoteAfterLongOutput(t *testing.T) {
	m := newModel(nil)

	updated, _ := m.Update(toolStartMsg{id: "call_1", name: "bash", args: "command=long"})
	started := updated.(model)
	updated, _ = started.Update(toolOutputMsg{id: "call_1", output: strings.Repeat("x", 10050)})
	updated, _ = updated.(model).Update(toolEndMsg{id: "call_1", success: true})
	finished := updated.(model)
	transcript := ansi.Strip(finished.output)

	if !strings.Contains(transcript, "done (output truncated)") {
		t.Fatalf("expected tool end line to mention truncation, got %q", transcript)
	}
}

func (m *model) viewportHeightForTest(height int) {
	m.windowHeight = height + 4 + 1 + 3 + lineCount(m.promptStatus())
	m.viewportWidth = 20
	m.relayout()
}

func (m *model) seedTranscript(text string) {
	m.entries = []transcriptEntry{{kind: transcriptSystem, text: text}}
	m.currentAssistantEntry = -1
	m.currentToolOutputEntry = -1
	m.output = text
}
