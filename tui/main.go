package main

import (
	"bufio"
	"encoding/json"
	"flag"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"sort"
	"strings"
	"time"

	"github.com/charmbracelet/bubbles/textarea"
	"github.com/charmbracelet/bubbles/viewport"
	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"
	"github.com/charmbracelet/x/ansi"
)

// wrapText wraps text to the given width, preserving newlines
func wrapText(text string, width int) string {
	if width <= 0 || text == "" {
		return text
	}

	var result strings.Builder
	lines := strings.Split(text, "\n")

	for i, line := range lines {
		if i > 0 {
			result.WriteByte('\n')
		}

		lineWidth := ansi.StringWidth(line)
		if lineWidth <= width {
			result.WriteString(line)
			continue
		}

		result.WriteString(ansi.Hardwrap(line, width, true))
	}

	return result.String()
}

func separatorString(width int) string {
	if width <= 0 {
		return ""
	}

	return strings.Repeat("─", width)
}

func formatToolArgs(args map[string]interface{}) string {
	if len(args) == 0 {
		return ""
	}

	keys := make([]string, 0, len(args))
	for key := range args {
		keys = append(keys, key)
	}
	sort.Strings(keys)

	parts := make([]string, 0, len(keys))
	for _, key := range keys {
		parts = append(parts, fmt.Sprintf("%s=%v", key, args[key]))
	}

	return strings.Join(parts, ", ")
}

func headerLine(prefix, status string, width int) string {
	if width <= 0 {
		return prefix + " | " + status
	}

	separator := " | "
	available := width - ansi.StringWidth(prefix) - ansi.StringWidth(separator)
	if available <= 0 {
		return ansi.Truncate(prefix, width, "")
	}

	return prefix + separator + ansi.Truncate(status, available, "...")
}

func formatToolOutputChunk(chunk string, lineOpen bool) (string, bool) {
	if chunk == "" {
		return "", lineOpen
	}

	var out strings.Builder
	parts := strings.SplitAfter(chunk, "\n")

	for _, part := range parts {
		if part == "" {
			continue
		}

		endsLine := strings.HasSuffix(part, "\n")
		content := strings.TrimSuffix(part, "\n")

		if !lineOpen {
			out.WriteString(toolArgsStyle)
			out.WriteString("│ ")
			out.WriteString(resetStyleTool)
		}

		out.WriteString(content)
		if endsLine {
			out.WriteByte('\n')
			lineOpen = false
		} else {
			lineOpen = true
		}
	}

	return out.String(), lineOpen
}

func formatToolStartLine(name, args string) string {
	return renderToolStartEntryAtWidth(name, args, 80)
}

func renderToolStartEntryAtWidth(name, args string, width int) string {
	var line strings.Builder
	line.WriteByte('\n')
	line.WriteString(toolStyle)
	line.WriteString("tool>")
	line.WriteString(resetStyleTool)
	line.WriteByte(' ')
	line.WriteString(name)
	line.WriteByte('\n')

	if strings.TrimSpace(args) == "" {
		return line.String()
	}

	line.WriteString(renderIndentedBlockAtWidth(args, width))
	return line.String()
}

func formatToolEndLine(success bool, err string, truncated bool) string {
	return renderToolEndEntryAtWidth(success, err, truncated, 80)
}

func renderToolEndEntryAtWidth(success bool, err string, truncated bool, width int) string {
	suffix := ""
	if truncated {
		suffix = " (output truncated)"
	}

	if success {
		return strings.TrimPrefix(renderWrappedBlockAtWidth("└", toolArgsStyle, toolSuccessStyle+"[✓]"+resetStyleTool+" done"+suffix, width), "\n")
	}

	return strings.TrimPrefix(renderWrappedBlockAtWidth("└", toolArgsStyle, toolErrorStyle+"[✗]"+resetStyleTool+" "+err+suffix, width), "\n")
}

func formatAssistantChunk(chunk string, pendingPrefix bool) (string, bool) {
	if chunk == "" {
		return "", pendingPrefix
	}
	if !pendingPrefix {
		return chunk, false
	}

	idx := strings.IndexFunc(chunk, func(r rune) bool {
		return r != '\n'
	})
	if idx == -1 {
		return chunk, true
	}

	prefix := headerStyle + "goose>" + resetStyle + " "
	return chunk[:idx] + prefix + chunk[idx:], false
}

func renderWrappedBlockAtWidth(label, labelColor, text string, width int) string {
	if text == "" {
		return ""
	}
	if width <= 0 {
		width = 1
	}

	var rendered strings.Builder
	rendered.WriteByte('\n')
	logicalLines := strings.Split(strings.TrimRight(text, "\n"), "\n")
	if len(logicalLines) == 0 {
		logicalLines = []string{""}
	}

	firstPrefixWidth := ansi.StringWidth(label) + 1
	continuationPrefixWidth := 2
	firstContentWidth := width - firstPrefixWidth
	if firstContentWidth < 1 {
		firstContentWidth = 1
	}
	continuationContentWidth := width - continuationPrefixWidth
	if continuationContentWidth < 1 {
		continuationContentWidth = 1
	}

	for i, logicalLine := range logicalLines {
		wrapped := wrapText(logicalLine, firstContentWidth)
		if i > 0 {
			wrapped = wrapText(logicalLine, continuationContentWidth)
		}
		segments := strings.Split(wrapped, "\n")
		if len(segments) == 0 {
			segments = []string{""}
		}

		for j, segment := range segments {
			if i == 0 && j == 0 {
				rendered.WriteString(labelColor)
				rendered.WriteString(label)
				rendered.WriteString(resetStyle)
				rendered.WriteByte(' ')
			} else {
				rendered.WriteString(toolArgsStyle)
				rendered.WriteString("│ ")
				rendered.WriteString(resetStyle)
			}
			rendered.WriteString(segment)
			rendered.WriteByte('\n')
		}
	}

	return rendered.String()
}

func renderIndentedBlockAtWidth(text string, width int) string {
	if text == "" {
		return ""
	}
	if width <= 2 {
		width = 2
	}

	var rendered strings.Builder
	logicalLines := strings.Split(strings.TrimRight(text, "\n"), "\n")
	contentWidth := width - 2
	if contentWidth < 1 {
		contentWidth = 1
	}

	for _, logicalLine := range logicalLines {
		wrapped := wrapText(logicalLine, contentWidth)
		segments := strings.Split(wrapped, "\n")
		if len(segments) == 0 {
			segments = []string{""}
		}

		for _, segment := range segments {
			rendered.WriteString(toolArgsStyle)
			rendered.WriteString("│ ")
			rendered.WriteString(resetStyle)
			rendered.WriteString(segment)
			rendered.WriteByte('\n')
		}
	}

	return rendered.String()
}

func renderAssistantEntryAtWidth(text string, width int) string {
	return renderWrappedBlockAtWidth("goose>", headerStyle, text, width)
}

func renderAssistantEntry(text string) string {
	return renderAssistantEntryAtWidth(text, 80)
}

func renderUserEntryAtWidth(text string, width int) string {
	return renderWrappedBlockAtWidth("you>", promptStyle, text, width)
}

func renderUserEntry(text string) string {
	return renderUserEntryAtWidth(text, 80)
}

func renderCommandEntryAtWidth(name, args string, width int) string {
	command := "/" + name
	if strings.TrimSpace(args) != "" {
		command += " " + args
	}

	return renderWrappedBlockAtWidth("cmd>", toolArgsStyle, command, width)
}

func renderCommandEntry(name, args string) string {
	return renderCommandEntryAtWidth(name, args, 80)
}

func renderErrorEntryAtWidth(text string, width int) string {
	trimmed := strings.TrimRight(text, "\n")
	if trimmed == "" {
		return ""
	}

	return renderWrappedBlockAtWidth("error>", errorStyle, trimmed, width)
}

func renderErrorEntry(text string) string {
	return renderErrorEntryAtWidth(text, 80)
}

func renderSystemEntryAtWidth(text string, width int) string {
	trimmed := strings.TrimRight(text, "\n")
	if trimmed == "" {
		return ""
	}

	return renderWrappedBlockAtWidth("info>", successStyle, trimmed, width)
}

func renderSystemEntry(text string) string {
	return renderSystemEntryAtWidth(text, 80)
}

func toolOutputLineCount(text string) int {
	trimmed := strings.TrimRight(text, "\n")
	if trimmed == "" {
		return 0
	}

	return strings.Count(trimmed, "\n") + 1
}

func shouldCompactToolOutput(text string) bool {
	return toolOutputLineCount(text) > 6 || len(text) > 500
}

func renderToolOutputEntryAtWidth(text string, expanded bool, width int, selected bool) string {
	if text == "" {
		return ""
	}
	marker := ""
	if selected {
		marker = toolArgsStyle + "◆ " + resetStyleTool + "selected tool block\n"
	}
	if expanded || !shouldCompactToolOutput(text) {
		return marker + renderIndentedBlockAtWidth(text, width)
	}

	trimmed := strings.TrimRight(text, "\n")
	lines := strings.Split(trimmed, "\n")
	previewCount := 3
	if len(lines) < previewCount {
		previewCount = len(lines)
	}

	previewText := strings.Join(lines[:previewCount], "\n")
	if strings.HasSuffix(text, "\n") || previewCount < len(lines) {
		previewText += "\n"
	}
	preview := renderIndentedBlockAtWidth(previewText, width)

	hidden := len(lines) - previewCount
	if hidden < 0 {
		hidden = 0
	}

	summary := fmt.Sprintf("%d more line(s) hidden, Ctrl+O expands", hidden)
	if selected {
		summary += " [selected]"
	}

	return marker + preview + toolArgsStyle + "⋮ " + resetStyleTool + summary + "\n"
}

func renderToolOutputEntry(text string, expanded bool) string {
	return renderToolOutputEntryAtWidth(text, expanded, 80, false)
}

func formatUserPrompt(text string) string {
	return "\n" + promptStyle + "you>" + resetStyle + " " + text + "\n"
}

func formatCommandFeedback(name, args string) string {
	command := "/" + name
	if strings.TrimSpace(args) != "" {
		command += " " + args
	}

	return "\n" + toolArgsStyle + "cmd>" + resetStyle + " " + command + "\n"
}

func formatErrorLine(text string) string {
	trimmed := strings.TrimRight(text, "\n")
	if trimmed == "" {
		return ""
	}

	return "\n" + errorStyle + "error>" + resetStyle + " " + trimmed + "\n"
}

type Backend struct {
	cmd        *exec.Cmd
	stdin      *os.File
	stdout     *bufio.Scanner
	sessionID  string
	sessionDir string
}

type BackendConfig struct {
	Model    string
	Provider string
	BaseURL  string
}

type BackendResponse struct {
	Type       string `json:"type"`
	Content    string `json:"content,omitempty"`
	Done       bool   `json:"done,omitempty"`
	SessionID  string `json:"session_id,omitempty"`
	SessionDir string `json:"session_dir,omitempty"`
	PlanMode   bool   `json:"plan_mode,omitempty"`
	Success    bool   `json:"success,omitempty"`
	Error      string `json:"error,omitempty"`
	Message    string `json:"message,omitempty"`
	// Tool-related fields
	ToolName   string                 `json:"name,omitempty"`
	ToolID     string                 `json:"id,omitempty"`
	ToolOutput string                 `json:"output,omitempty"`
	ToolArgs   map[string]interface{} `json:"args,omitempty"`
}

type TUIRequest struct {
	Type       string `json:"type"`
	WorkingDir string `json:"working_dir,omitempty"`
	Config     *struct {
		Model    string `json:"model,omitempty"`
		Provider string `json:"provider,omitempty"`
		BaseURL  string `json:"base_url,omitempty"`
	} `json:"config,omitempty"`
	Text string `json:"text,omitempty"`
	Name string `json:"name,omitempty"`
	Args string `json:"args,omitempty"`
}

func NewBackend(path string) (*Backend, error) {
	return &Backend{}, nil
}

func (b *Backend) Start(backendPath string, cfg BackendConfig) error {
	args := []string{"--tui-mode"}
	if cfg.Model != "" {
		args = append(args, "--model", cfg.Model)
	}
	if cfg.Provider != "" {
		args = append(args, "--provider", cfg.Provider)
	}
	if cfg.BaseURL != "" {
		args = append(args, "--base-url", cfg.BaseURL)
	}

	cmd := exec.Command(backendPath, args...)

	stdin, err := cmd.StdinPipe()
	if err != nil {
		return fmt.Errorf("failed to create stdin pipe: %w", err)
	}

	stdout, err := cmd.StdoutPipe()
	if err != nil {
		return fmt.Errorf("failed to create stdout pipe: %w", err)
	}

	cmd.Stderr = os.Stderr

	if err := cmd.Start(); err != nil {
		return fmt.Errorf("failed to start backend: %w", err)
	}

	b.cmd = cmd
	b.stdin, _ = stdin.(*os.File)
	b.stdout = bufio.NewScanner(stdout)

	const maxCapacity = 1024 * 1024
	b.stdout.Buffer(make([]byte, maxCapacity), maxCapacity)

	return nil
}

func (b *Backend) Send(req TUIRequest) error {
	data, err := json.Marshal(req)
	if err != nil {
		return fmt.Errorf("failed to marshal request: %w", err)
	}

	_, err = fmt.Fprintln(b.stdin, string(data))
	if err != nil {
		return fmt.Errorf("failed to send request: %w", err)
	}

	return nil
}

func (b *Backend) ReadResponse() (*BackendResponse, error) {
	if !b.stdout.Scan() {
		if err := b.stdout.Err(); err != nil {
			return nil, fmt.Errorf("error reading stdout: %w", err)
		}
		return nil, fmt.Errorf("backend closed stdout")
	}

	line := b.stdout.Text()
	var resp BackendResponse
	if err := json.Unmarshal([]byte(line), &resp); err != nil {
		// Not JSON - treat as raw text output (streaming response)
		return &BackendResponse{
			Type:    "response",
			Content: line,
			Done:    false,
		}, nil
	}

	return &resp, nil
}

func (b *Backend) SendInit(workingDir string, cfg BackendConfig) error {
	req := TUIRequest{
		Type:       "init",
		WorkingDir: workingDir,
	}
	if cfg.Model != "" || cfg.Provider != "" || cfg.BaseURL != "" {
		req.Config = &struct {
			Model    string `json:"model,omitempty"`
			Provider string `json:"provider,omitempty"`
			BaseURL  string `json:"base_url,omitempty"`
		}{
			Model:    cfg.Model,
			Provider: cfg.Provider,
			BaseURL:  cfg.BaseURL,
		}
	}
	return b.Send(req)
}

func (b *Backend) SendPrompt(text string) error {
	return b.Send(TUIRequest{Type: "prompt", Text: text})
}

func (b *Backend) SendCommand(name, args string) error {
	return b.Send(TUIRequest{Type: "command", Name: name, Args: args})
}

func (b *Backend) SendQuit() error {
	return b.Send(TUIRequest{Type: "quit"})
}

func (b *Backend) Close() error {
	if b.stdin != nil {
		b.stdin.Close()
	}
	if b.cmd != nil && b.cmd.Process != nil {
		b.cmd.Process.Kill()
		b.cmd.Wait()
	}
	return nil
}

func (b *Backend) WaitForInit(timeout time.Duration) (*BackendResponse, error) {
	deadline := time.Now().Add(timeout)

	for time.Now().Before(deadline) {
		resp, err := b.ReadResponse()
		if err != nil {
			return nil, err
		}
		if resp == nil {
			time.Sleep(100 * time.Millisecond)
			continue
		}

		if resp.Type == "init_ok" {
			b.sessionID = resp.SessionID
			b.sessionDir = resp.SessionDir
			return resp, nil
		}

		if resp.Type == "error" {
			return nil, fmt.Errorf("backend error: %s", resp.Message)
		}
	}

	return nil, fmt.Errorf("timeout waiting for init_ok")
}

type responseMsg string
type responseDoneMsg struct{}
type backendErrorMsg string
type toolStartMsg struct {
	id   string
	name string
	args string
}
type toolOutputMsg struct {
	id     string
	output string
}
type toolEndMsg struct {
	id      string
	success bool
	error   string
}

type transcriptKind string

const (
	transcriptSystem     transcriptKind = "system"
	transcriptUser       transcriptKind = "user"
	transcriptAssistant  transcriptKind = "assistant"
	transcriptCommand    transcriptKind = "command"
	transcriptError      transcriptKind = "error"
	transcriptToolStart  transcriptKind = "tool_start"
	transcriptToolOutput transcriptKind = "tool_output"
	transcriptToolEnd    transcriptKind = "tool_end"
)

type transcriptEntry struct {
	kind     transcriptKind
	text     string
	meta     string
	success  bool
	expanded bool
}

type model struct {
	backend    *Backend
	textInput  textarea.Model
	viewport   viewport.Model
	output     string
	entries    []transcriptEntry
	planMode   bool
	connected  bool
	showHelp   bool
	helpOffset int
	quit       bool
	fallback   bool // true if falling back to REPL
	// Tool state
	currentTool             string
	currentToolID           string
	currentToolOutput       string
	currentToolTruncated    bool
	currentToolOutputEntry  int
	selectedToolOutputEntry int
	currentAssistantEntry   int
	toolOutputLineOpen      bool
	isRunning               bool // true when a tool is executing
	assistantResponding     bool
	hasUnseenOutput         bool
	awaitingAssistantPrefix bool
	viewportWidth           int // current viewport width for text wrapping
	windowHeight            int
	activeModel             string
	activeProvider          string
}

func (m model) sendPrompt(text string) {
	if m.backend == nil {
		return
	}
	_ = m.backend.SendPrompt(text)
}

func (m model) sendCommand(name, args string) {
	if m.backend == nil {
		return
	}
	_ = m.backend.SendCommand(name, args)
}

func (m *model) applyComposerState() {
	if m.assistantResponding || m.isRunning {
		m.textInput.Prompt = "… "
		m.textInput.Placeholder = "Goose is working; you can keep typing your next prompt..."
		return
	}

	m.textInput.Prompt = "> "
	m.textInput.Placeholder = "Type your message or /command..."
}

func (m *model) appendTranscriptEntry(kind transcriptKind, text, meta string, success bool) int {
	m.entries = append(m.entries, transcriptEntry{kind: kind, text: text, meta: meta, success: success})
	return len(m.entries) - 1
}

func (m *model) toggleLastToolOutputEntry() bool {
	for i := len(m.entries) - 1; i >= 0; i-- {
		if m.entries[i].kind == transcriptToolOutput {
			m.selectedToolOutputEntry = i
			m.entries[i].expanded = !m.entries[i].expanded
			return true
		}
	}

	return false
}

func (m model) toolOutputEntryIndices() []int {
	indices := make([]int, 0)
	for i, entry := range m.entries {
		if entry.kind == transcriptToolOutput {
			indices = append(indices, i)
		}
	}
	return indices
}

func (m model) validSelectedToolOutputEntry() bool {
	return m.selectedToolOutputEntry >= 0 && m.selectedToolOutputEntry < len(m.entries) && m.entries[m.selectedToolOutputEntry].kind == transcriptToolOutput
}

func (m *model) toggleSelectedToolOutputEntry() bool {
	if m.validSelectedToolOutputEntry() {
		m.entries[m.selectedToolOutputEntry].expanded = !m.entries[m.selectedToolOutputEntry].expanded
		return true
	}
	return m.toggleLastToolOutputEntry()
}

func (m *model) selectToolOutputEntry(delta int) bool {
	indices := m.toolOutputEntryIndices()
	if len(indices) == 0 {
		m.selectedToolOutputEntry = -1
		return false
	}
	if !m.validSelectedToolOutputEntry() {
		if delta >= 0 {
			m.selectedToolOutputEntry = indices[0]
		} else {
			m.selectedToolOutputEntry = indices[len(indices)-1]
		}
		return true
	}

	current := 0
	for i, idx := range indices {
		if idx == m.selectedToolOutputEntry {
			current = i
			break
		}
	}
	next := (current + delta + len(indices)) % len(indices)
	m.selectedToolOutputEntry = indices[next]
	return true
}

func (m model) selectedToolOutputPosition() (int, int) {
	indices := m.toolOutputEntryIndices()
	if len(indices) == 0 || !m.validSelectedToolOutputEntry() {
		return 0, len(indices)
	}
	for i, idx := range indices {
		if idx == m.selectedToolOutputEntry {
			return i + 1, len(indices)
		}
	}
	return 0, len(indices)
}

func (m model) hasCollapsedToolOutput() bool {
	for _, entry := range m.entries {
		if entry.kind == transcriptToolOutput && shouldCompactToolOutput(entry.text) && !entry.expanded {
			return true
		}
	}

	return false
}

func (m *model) appendToTranscriptEntry(idx int, text string) {
	if idx < 0 || idx >= len(m.entries) || text == "" {
		return
	}
	m.entries[idx].text += text
}

func (m *model) renderTranscriptEntry(idx int, entry transcriptEntry) string {
	switch entry.kind {
	case transcriptUser:
		return renderUserEntryAtWidth(entry.text, m.renderWidth())
	case transcriptAssistant:
		return renderAssistantEntryAtWidth(entry.text, m.renderWidth())
	case transcriptCommand:
		return renderCommandEntryAtWidth(entry.text, entry.meta, m.renderWidth())
	case transcriptError:
		return renderErrorEntryAtWidth(entry.text, m.renderWidth())
	case transcriptToolStart:
		return renderToolStartEntryAtWidth(entry.text, entry.meta, m.renderWidth())
	case transcriptToolOutput:
		selected := m.validSelectedToolOutputEntry() && m.selectedToolOutputEntry == idx
		return renderToolOutputEntryAtWidth(entry.text, entry.expanded, m.renderWidth(), selected)
	case transcriptToolEnd:
		return renderToolEndEntryAtWidth(entry.success, entry.text, entry.meta == "truncated", m.renderWidth())
	default:
		return renderSystemEntryAtWidth(entry.text, m.renderWidth())
	}
}

func (m model) renderTranscript() string {
	var rendered strings.Builder

	for i, entry := range m.entries {
		rendered.WriteString(m.renderTranscriptEntry(i, entry))
	}

	return rendered.String()
}

func (m model) sessionStatus() string {
	parts := make([]string, 0, 4)
	if m.connected {
		parts = append(parts, "connected")
	}
	if m.activeProvider != "" || m.activeModel != "" {
		providerModel := strings.TrimPrefix(strings.TrimSpace(m.activeProvider+"/"+m.activeModel), "/")
		if providerModel != "" {
			parts = append(parts, providerModel)
		}
	}
	if m.backend != nil && m.backend.sessionID != "" {
		parts = append(parts, "session "+m.backend.sessionID)
	}
	if m.assistantResponding {
		parts = append(parts, "responding")
	}
	if m.isRunning && m.currentTool != "" {
		parts = append(parts, "running "+m.currentTool)
	}
	if m.hasUnseenOutput {
		parts = append(parts, "new output below")
	}
	if m.viewport.TotalLineCount() > m.viewport.Height && !m.viewport.AtBottom() {
		parts = append(parts, fmt.Sprintf("scroll %d%%", int(m.viewport.ScrollPercent()*100)))
	}
	if m.hasCollapsedToolOutput() {
		parts = append(parts, "tool block compact")
	}
	if selected, total := m.selectedToolOutputPosition(); selected > 0 {
		parts = append(parts, fmt.Sprintf("tool %d/%d selected", selected, total))
	}
	if m.showHelp {
		parts = append(parts, "help open")
	}
	parts = append(parts, "/help", "/exit")
	return strings.Join(parts, " | ")
}

func (m model) promptStatus() string {
	modeLabel := "build"
	modeColor := "\033[36m"
	if m.planMode {
		modeLabel = "plan"
		modeColor = "\033[33m"
	}

	parts := []string{modeColor + "mode " + strings.ToUpper(modeLabel) + resetStyle}
	width := m.renderWidth()
	if m.assistantResponding {
		parts = append(parts, "\033[35mGoose is responding\033[0m")
	} else if width >= 45 {
		parts = append(parts, "\033[32mReady for input\033[0m")
	}
	if width >= 45 {
		parts = append(parts, "\033[90mTab toggles mode\033[0m")
	}
	if width >= 60 {
		parts = append(parts, "\033[90mPgUp/PgDn scroll\033[0m")
	}
	if width >= 74 {
		parts = append(parts, "\033[90mHome/End jump\033[0m")
	}
	if width >= 92 {
		parts = append(parts, "\033[90m/clear resets transcript\033[0m")
	}
	if selected, total := m.selectedToolOutputPosition(); width >= 100 && selected > 0 {
		parts = append(parts, fmt.Sprintf("\033[37mtool %d/%d selected\033[0m", selected, total))
	}
	if width >= 112 {
		parts = append(parts, "\033[90mCtrl+O toggles selected tool block\033[0m")
	}
	if width >= 118 {
		parts = append(parts, "\033[90mCtrl+P/Ctrl+N switch tool block\033[0m")
	}
	if width >= 128 {
		parts = append(parts, "\033[90mF1 help overlay\033[0m")
	}

	if m.activeProvider != "" || m.activeModel != "" {
		providerModel := strings.TrimPrefix(strings.TrimSpace(m.activeProvider+"/"+m.activeModel), "/")
		if providerModel != "" {
			parts = append([]string{"\033[37m" + providerModel + "\033[0m"}, parts...)
		}
	}

	return wrapText(strings.Join(parts, " \033[90m|\033[0m "), m.renderWidth())
}

func lineCount(text string) int {
	if text == "" {
		return 0
	}

	return strings.Count(text, "\n") + 1
}

func (m model) renderWidth() int {
	if m.viewportWidth <= 1 {
		return 1
	}

	// Leave one spare cell to avoid terminal auto-wrap pushing the separator
	// and prompt area around when a rendered line lands exactly on the edge.
	return m.viewportWidth - 1
}

func (m *model) relayout() {
	const headerHeight = 4
	const separatorHeight = 1

	if m.viewportWidth <= 0 {
		m.viewportWidth = 80
	}
	if m.windowHeight <= 0 {
		m.windowHeight = 29
	}

	renderWidth := m.renderWidth()
	m.textInput.SetWidth(renderWidth)
	m.textInput.SetHeight(3)
	m.viewport.Width = renderWidth
	statusHeight := lineCount(m.promptStatus())
	viewportHeight := m.windowHeight - headerHeight - separatorHeight - m.textInput.Height() - statusHeight
	if viewportHeight < 1 {
		viewportHeight = 1
	}
	m.viewport.Height = viewportHeight
}

func (m *model) syncViewport(follow bool) {
	offset := m.viewport.YOffset
	m.output = m.renderTranscript()
	m.viewport.SetContent(m.output)
	if follow {
		m.viewport.GotoBottom()
		return
	}

	maxOffset := m.viewport.TotalLineCount() - m.viewport.Height
	if maxOffset < 0 {
		maxOffset = 0
	}
	if offset > maxOffset {
		offset = maxOffset
	}
	m.viewport.SetYOffset(offset)
}

func (m *model) noteViewportState(follow, changed bool) {
	if follow || m.viewport.AtBottom() {
		m.hasUnseenOutput = false
		return
	}
	if changed {
		m.hasUnseenOutput = true
	}
}

func (m model) helpBody() string {
	sections := []string{
		headerStyle + "Help" + resetStyle,
		"",
		successStyle + "Navigation" + resetStyle,
		"Up/Down scroll line by line",
		"PgUp/PgDn scroll by page",
		"Home/End jump to the top or bottom",
		"Mouse wheel scrolls the transcript",
		"",
		successStyle + "Prompt" + resetStyle,
		"Enter submits the current prompt",
		"Tab toggles plan/build mode",
		"F1 opens or closes this help view",
		"Esc closes help",
		"",
		successStyle + "Tools" + resetStyle,
		"Ctrl+P/Ctrl+N switch the selected tool block",
		"Ctrl+O toggles the selected tool output block",
		"Long tool output starts compact and can be expanded",
		"Selected tool blocks are marked in the transcript and banner",
		"",
		successStyle + "Commands" + resetStyle,
		"/help opens this overlay locally",
		"/clear resets the transcript",
		"/exit leaves the TUI",
	}

	return wrapText(strings.Join(sections, "\n"), m.renderWidth())
}

func (m model) helpLines() []string {
	return strings.Split(m.helpBody(), "\n")
}

func (m model) helpMaxOffset() int {
	maxOffset := len(m.helpLines()) - m.viewport.Height
	if maxOffset < 0 {
		return 0
	}
	return maxOffset
}

func clamp(value, min, max int) int {
	if value < min {
		return min
	}
	if value > max {
		return max
	}
	return value
}

func (m *model) moveHelp(delta int) {
	m.helpOffset = clamp(m.helpOffset+delta, 0, m.helpMaxOffset())
}

func (m *model) pageHelp(delta int) {
	step := m.viewport.Height
	if step < 1 {
		step = 1
	}
	m.moveHelp(delta * step)
}

func (m model) helpContent() string {
	lines := m.helpLines()
	start := clamp(m.helpOffset, 0, m.helpMaxOffset())
	end := start + m.viewport.Height
	if end > len(lines) {
		end = len(lines)
	}
	visible := append([]string{}, lines[start:end]...)
	for len(visible) < m.viewport.Height {
		visible = append(visible, "")
	}
	return strings.Join(visible, "\n")
}

const (
	toolStyle        = "\033[1;36m" // Bold cyan for tool name
	toolArgsStyle    = "\033[90m"   // Dim gray for args
	toolOutputStyle  = "\033[37m"   // White for output
	toolSuccessStyle = "\033[32m"   // Green for success
	toolErrorStyle   = "\033[31m"   // Red for error
	resetStyleTool   = "\033[0m"
)

func newModel(backend *Backend) model {
	ti := textarea.New()
	ti.Placeholder = "Type your message or /command..."
	ti.Prompt = "> "
	ti.ShowLineNumbers = false
	ti.CharLimit = 1000
	ti.SetWidth(80)
	ti.SetHeight(3)
	ti.Focus()

	vp := viewport.New(80, 20)
	vp.SetContent("")
	vp.YOffset = 0

	m := model{
		backend:                 backend,
		textInput:               ti,
		viewport:                vp,
		planMode:                false,
		connected:               false,
		quit:                    false,
		fallback:                false,
		currentToolOutputEntry:  -1,
		selectedToolOutputEntry: -1,
		currentAssistantEntry:   -1,
		viewportWidth:           80,
		windowHeight:            29,
	}
	m.output = ""
	m.viewport.SetContent("")
	m.applyComposerState()
	m.relayout()
	return m
}

func (m model) Init() tea.Cmd {
	return textarea.Blink
}

func (m model) Update(msg tea.Msg) (tea.Model, tea.Cmd) {
	var cmd tea.Cmd

	switch msg := msg.(type) {
	case tea.KeyMsg:
		// Handle special keys
		switch msg.String() {
		case "ctrl+c":
			if m.connected && !m.quit {
				m.quit = true
				m.backend.SendQuit()
			}
			return m, tea.Quit
		case "f1":
			m.showHelp = !m.showHelp
			if m.showHelp {
				m.helpOffset = 0
			}
			return m, nil
		case "esc":
			if m.showHelp {
				m.showHelp = false
				return m, nil
			}
		case "up", "pageup", "pgup":
			if m.showHelp {
				if msg.String() == "up" {
					m.moveHelp(-1)
				} else {
					m.pageHelp(-1)
				}
				return m, nil
			}
			// Scroll up in viewport
			m.viewport, cmd = m.viewport.Update(msg)
			return m, cmd
		case "down", "pagedown", "pgdown":
			if m.showHelp {
				if msg.String() == "down" {
					m.moveHelp(1)
				} else {
					m.pageHelp(1)
				}
				return m, nil
			}
			// Scroll down in viewport
			m.viewport, cmd = m.viewport.Update(msg)
			if m.viewport.AtBottom() {
				m.hasUnseenOutput = false
			}
			return m, cmd
		case "home":
			if m.showHelp {
				m.helpOffset = 0
				return m, nil
			}
			m.viewport.GotoTop()
			return m, nil
		case "end":
			if m.showHelp {
				m.helpOffset = m.helpMaxOffset()
				return m, nil
			}
			m.viewport.GotoBottom()
			m.hasUnseenOutput = false
			return m, nil
		case "ctrl+o":
			follow := m.viewport.AtBottom()
			if m.toggleSelectedToolOutputEntry() {
				m.syncViewport(follow)
			}
			return m, nil
		case "ctrl+n":
			follow := m.viewport.AtBottom()
			if m.selectToolOutputEntry(1) {
				m.syncViewport(follow)
			}
			return m, nil
		case "ctrl+p":
			follow := m.viewport.AtBottom()
			if m.selectToolOutputEntry(-1) {
				m.syncViewport(follow)
			}
			return m, nil
		case "tab":
			m.planMode = !m.planMode
			m.sendCommand("plan", "")
			m.relayout()
			// Update cursor color based on mode
			if m.planMode {
				m.textInput.Cursor.Style = lipgloss.NewStyle().Foreground(lipgloss.Color("33")) // Yellow for plan
				return m, textarea.Blink
			}
			m.textInput.Cursor.Style = lipgloss.NewStyle().Foreground(lipgloss.Color("32")) // Green for build
			return m, textarea.Blink
		case "enter":
			if m.textInput.Value() == "" {
				return m, textarea.Blink
			}

			text := m.textInput.Value()
			m.textInput.Reset()

			if strings.HasPrefix(text, "/") {
				parts := strings.SplitN(text[1:], " ", 2)
				cmdName := parts[0]
				var args string
				if len(parts) > 1 {
					args = parts[1]
				}

				if cmdName == "quit" || cmdName == "exit" {
					// Clean exit - just quit without fallback
					if m.backend != nil {
						_ = m.backend.SendQuit()
					}
					return m, tea.Quit
				}

				if cmdName == "clear" {
					// Handle clear locally in TUI
					m.entries = nil
					m.output = ""
					m.currentAssistantEntry = -1
					m.currentToolOutputEntry = -1
					m.selectedToolOutputEntry = -1
					m.currentToolOutput = ""
					m.toolOutputLineOpen = false
					m.assistantResponding = false
					m.awaitingAssistantPrefix = false
					m.applyComposerState()
					m.syncViewport(true)
					return m, textarea.Blink
				}

				if cmdName == "help" {
					m.showHelp = args != "off" && args != "close"
					return m, textarea.Blink
				}

				if cmdName == "tab" {
					m.planMode = !m.planMode
					m.relayout()
					if m.planMode {
						m.sendCommand("plan", "")
						m.textInput.Cursor.Style = lipgloss.NewStyle().Foreground(lipgloss.Color("33"))
						return m, textarea.Blink
					}
					m.sendCommand("plan", "off")
					m.textInput.Cursor.Style = lipgloss.NewStyle().Foreground(lipgloss.Color("32"))
					return m, textarea.Blink
				}

				m.sendCommand(cmdName, args)
				// Show command feedback
				m.currentAssistantEntry = -1
				m.appendTranscriptEntry(transcriptCommand, cmdName, args, false)
				m.syncViewport(true)
				return m, textarea.Blink
			}

			m.currentAssistantEntry = -1
			m.appendTranscriptEntry(transcriptUser, text, "", false)
			m.sendPrompt(text)
			m.assistantResponding = true
			m.awaitingAssistantPrefix = true
			m.applyComposerState()
			m.syncViewport(true)
			return m, textarea.Blink
		default:
			// Let textarea handle all other keys (typing)
			m.textInput, cmd = m.textInput.Update(msg)
			return m, cmd
		}

	case responseMsg:
		follow := m.viewport.AtBottom()
		content := string(msg)
		if content != "" {
			if m.currentAssistantEntry < 0 || m.currentAssistantEntry >= len(m.entries) || m.entries[m.currentAssistantEntry].kind != transcriptAssistant {
				m.currentAssistantEntry = m.appendTranscriptEntry(transcriptAssistant, "", "", false)
			}
			m.appendToTranscriptEntry(m.currentAssistantEntry, content)
			m.awaitingAssistantPrefix = strings.IndexFunc(m.entries[m.currentAssistantEntry].text, func(r rune) bool {
				return r != '\n'
			}) == -1
		}
		m.syncViewport(follow)
		m.noteViewportState(follow, msg != "")
		return m, textarea.Blink
	case responseDoneMsg:
		m.assistantResponding = false
		m.awaitingAssistantPrefix = false
		m.currentAssistantEntry = -1
		m.applyComposerState()
		return m, textarea.Blink
	case backendErrorMsg:
		follow := m.viewport.AtBottom()
		m.assistantResponding = false
		m.currentAssistantEntry = -1
		m.awaitingAssistantPrefix = false
		m.applyComposerState()
		m.appendTranscriptEntry(transcriptError, string(msg), "", false)
		m.syncViewport(follow)
		m.noteViewportState(follow, msg != "")
		return m, textarea.Blink
	case toolStartMsg:
		follow := m.viewport.AtBottom()
		m.currentTool = msg.name
		m.currentToolID = msg.id
		m.currentToolOutput = ""
		m.currentToolTruncated = false
		m.currentToolOutputEntry = -1
		m.toolOutputLineOpen = false
		m.isRunning = true
		m.applyComposerState()
		m.currentAssistantEntry = -1
		m.appendTranscriptEntry(transcriptToolStart, msg.name, msg.args, false)
		m.syncViewport(follow)
		m.noteViewportState(follow, true)
		return m, textarea.Blink
	case toolOutputMsg:
		if m.currentToolID == msg.id {
			follow := m.viewport.AtBottom()
			// Truncate long output
			output := msg.output
			if len(m.currentToolOutput)+len(output) > 10000 {
				m.currentToolTruncated = true
				remaining := 10000 - len(m.currentToolOutput)
				if remaining > 0 {
					output = output[:remaining] + "... (truncated)"
				} else {
					output = ""
				}
			}
			if output != "" {
				if m.currentToolOutputEntry < 0 || m.currentToolOutputEntry >= len(m.entries) || m.entries[m.currentToolOutputEntry].kind != transcriptToolOutput {
					m.currentToolOutputEntry = m.appendTranscriptEntry(transcriptToolOutput, "", "", false)
					m.selectedToolOutputEntry = m.currentToolOutputEntry
				}
				m.currentToolOutput += output
				m.toolOutputLineOpen = !strings.HasSuffix(m.currentToolOutput, "\n")
				m.appendToTranscriptEntry(m.currentToolOutputEntry, output)
			}
			m.syncViewport(follow)
			m.noteViewportState(follow, output != "")
		}
		return m, textarea.Blink
	case toolEndMsg:
		if m.currentToolID == msg.id {
			follow := m.viewport.AtBottom()
			meta := ""
			if m.currentToolTruncated {
				meta = "truncated"
			}
			m.appendTranscriptEntry(transcriptToolEnd, msg.error, meta, msg.success)
			m.currentTool = ""
			m.currentToolID = ""
			m.currentToolOutput = ""
			m.currentToolTruncated = false
			m.currentToolOutputEntry = -1
			m.toolOutputLineOpen = false
			m.isRunning = false
			m.applyComposerState()
			m.syncViewport(follow)
			m.noteViewportState(follow, true)
		}
		return m, textarea.Blink
	case tea.WindowSizeMsg:
		follow := m.viewport.AtBottom()
		m.viewportWidth = msg.Width
		m.windowHeight = msg.Height
		m.relayout()
		m.syncViewport(follow)
		return m, textarea.Blink
	}

	return m, nil
}

func (m model) View() string {
	var s strings.Builder
	status := m.sessionStatus()
	headerPrefix := ""

	// Header (fixed at top)
	if m.planMode {
		headerPrefix = "\033[1m    __      \033[0m  \033[1;33mGOOSE CODE\033[0m v0.3.1 \033[33m[PLAN]\033[0m"
	} else {
		headerPrefix = "\033[1m    __      \033[0m  \033[1;36mGOOSE CODE\033[0m v0.3.1 \033[32m[BUILD]\033[0m"
	}
	if m.assistantResponding {
		headerPrefix += " \033[35m[RESPONDING]\033[0m"
	}
	s.WriteString(headerLine(headerPrefix, status, m.renderWidth()))
	s.WriteString("\n")
	s.WriteString("\033[1m___( o)>  \033[0m  ╔═╗╔═╗╔═╗╔═╗╔═╗  ╔═╗╔═╗╔╦╗╔═╗\n")
	s.WriteString("\033[1m\\ <_. )   \033[0m  ║ ╦║ ║║ ║╚═╗║╣   ║  ║ ║ ║║║╣ \n")
	s.WriteString("\033[1m `---'    \033[0m  ╚═╝╚═╝╚═╝╚═╝╚═╝  ╚═╝╚═╝═╩╝╚═╝\n")

	// Chat messages (scrollable viewport)
	if m.showHelp {
		s.WriteString(m.helpContent())
	} else {
		s.WriteString(m.viewport.View())
	}

	// Input area (fixed at bottom)
	if m.planMode {
		s.WriteString("\n\033[33m" + separatorString(m.renderWidth()) + "\033[0m\n")
	} else {
		s.WriteString("\n\033[36m" + separatorString(m.renderWidth()) + "\033[0m\n")
	}
	s.WriteString(m.textInput.View())
	s.WriteString("\n")
	s.WriteString(m.promptStatus())

	return s.String()
}

const (
	headerStyle  = "\033[1;36m"
	promptStyle  = "\033[34m"
	errorStyle   = "\033[31m"
	successStyle = "\033[32m"
	resetStyle   = "\033[0m"
)

type exitToReplMsg struct{}

func main() {
	execPath, err := os.Executable()
	if err != nil {
		execPath = "./goosecode-backend"
	} else {
		execPath = filepath.Join(filepath.Dir(execPath), "goosecode-backend")
	}
	backendPath := flag.String("backend", execPath, "Path to goosecode backend")
	model := flag.String("model", "", "Model to use")
	provider := flag.String("provider", "", "Provider preset")
	baseURL := flag.String("base-url", "", "API base URL")
	flag.Parse()

	if *model != "" {
		os.Setenv("OPENAI_MODEL", *model)
	}
	if *baseURL != "" {
		os.Setenv("OPENAI_BASE_URL", *baseURL)
	}
	if *provider != "" {
		os.Setenv("GOOSECODE_PROVIDER", *provider)
	}

	cfg := BackendConfig{
		Model:    *model,
		Provider: *provider,
		BaseURL:  *baseURL,
	}

	backend, err := NewBackend(*backendPath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Failed to create backend: %v\n", err)
		os.Exit(1)
	}

	cwd, _ := os.Getwd()

	if err := backend.Start(*backendPath, cfg); err != nil {
		fmt.Fprintf(os.Stderr, "Failed to start backend: %v\n", err)
		os.Exit(1)
	}
	defer backend.Close()

	if err := backend.SendInit(cwd, cfg); err != nil {
		fmt.Fprintf(os.Stderr, "Failed to send init: %v\n", err)
		os.Exit(1)
	}

	resp, err := backend.WaitForInit(10 * time.Second)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Init timeout: %v\n", err)
		os.Exit(1)
	}

	respChan := make(chan responseMsg, 100)
	respDoneChan := make(chan responseDoneMsg, 10)
	errChan := make(chan backendErrorMsg, 10)
	toolStartChan := make(chan toolStartMsg, 10)
	toolOutputChan := make(chan toolOutputMsg, 100)
	toolEndChan := make(chan toolEndMsg, 10)

	go func() {
		for {
			resp, err := backend.ReadResponse()
			if err != nil {
				_ = resp
				errChan <- backendErrorMsg(fmt.Sprintf("Error: %v", err))
				break
			}
			if resp != nil {
				if resp.Type == "response" {
					if resp.Content != "" {
						respChan <- responseMsg(resp.Content)
					}
					if resp.Done {
						respDoneChan <- responseDoneMsg{}
					}
				} else if resp.Type == "error" {
					errChan <- backendErrorMsg(errorStyle + "Error: " + resp.Message + resetStyle)
				} else if resp.Type == "tool_start" {
					toolStartChan <- toolStartMsg{
						id:   resp.ToolID,
						name: resp.ToolName,
						args: formatToolArgs(resp.ToolArgs),
					}
				} else if resp.Type == "tool_output" {
					toolOutputChan <- toolOutputMsg{
						id:     resp.ToolID,
						output: resp.ToolOutput,
					}
				} else if resp.Type == "tool_end" {
					toolEndChan <- toolEndMsg{
						id:      resp.ToolID,
						success: resp.Success,
						error:   resp.Error,
					}
				}
			}
		}
		close(respChan)
		close(respDoneChan)
		close(errChan)
		close(toolStartChan)
		close(toolOutputChan)
		close(toolEndChan)
	}()

	m := newModel(backend)
	m.connected = true
	m.activeModel = cfg.Model
	m.activeProvider = cfg.Provider
	m.appendTranscriptEntry(transcriptSystem, fmt.Sprintf("\033[32mConnected!\033[0m Session: %s\n\n", resp.SessionID), "", false)
	m.syncViewport(true)
	m.viewport.GotoBottom()

	tty, err := os.OpenFile("/dev/tty", os.O_RDWR, 0)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Cannot open TTY: %v\n", err)
		os.Exit(1)
	}
	defer tty.Close()

	p := tea.NewProgram(&m, tea.WithAltScreen(), tea.WithInput(tty), tea.WithOutput(tty))

	// Run goroutines to send messages to TUI
	go func() {
		for msg := range respChan {
			p.Send(msg)
		}
	}()

	go func() {
		for msg := range respDoneChan {
			p.Send(msg)
		}
	}()

	go func() {
		for msg := range errChan {
			p.Send(msg)
		}
	}()

	go func() {
		for msg := range toolStartChan {
			p.Send(msg)
		}
	}()

	go func() {
		for msg := range toolOutputChan {
			p.Send(msg)
		}
	}()

	go func() {
		for msg := range toolEndChan {
			p.Send(msg)
		}
	}()

	// Run the TUI program
	if _, runErr := p.Run(); runErr != nil {
		fmt.Fprintf(os.Stderr, "Error running TUI: %v\n", runErr)
		os.Exit(1)
	}
}
