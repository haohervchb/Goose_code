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

	if got := wide.textInput.Width(); got <= 80 {
		t.Fatalf("expected text input width to grow beyond 80, got %d", got)
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

func TestViewKeepsGooseBannerAndShowsStatus(t *testing.T) {
	m := newModel(&Backend{sessionID: "abc123"})
	m.connected = true
	m.isRunning = true
	m.currentTool = "bash"

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

	view := ansi.Strip(m.View())

	for _, needle := range []string{"ollama/llama3", "mode BUILD", "Tab toggles mode", "PgUp/PgDn scroll", "/clear resets", "transcript"} {
		if !strings.Contains(view, needle) {
			t.Fatalf("expected prompt status to contain %q, got %q", needle, view)
		}
	}
}

func TestViewportAutoFollowsWhenAlreadyAtBottom(t *testing.T) {
	m := newModel(nil)
	m.viewportHeightForTest(4)
	m.output = strings.Join([]string{"1", "2", "3", "4", "5", "6"}, "\n")
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
	m.output = strings.Join([]string{"1", "2", "3", "4", "5", "6"}, "\n")
	m.syncViewport(true)
	m.viewport.LineUp(2)

	updated, _ := m.Update(responseMsg("7\n"))
	after := updated.(model)

	if after.viewport.AtBottom() {
		t.Fatalf("expected viewport to preserve scroll position when user is reading older output")
	}
}

func (m *model) viewportHeightForTest(height int) {
	m.viewport.Height = height
	m.viewportWidth = 20
	m.viewport.Width = 20
}
