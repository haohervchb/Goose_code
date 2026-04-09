package main

import (
	"testing"

	tea "github.com/charmbracelet/bubbletea"
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
