package main

import (
	"bufio"
	"bytes"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"sort"
	"strings"
	"sync"
	"time"

	"github.com/charmbracelet/bubbles/textarea"
	"github.com/charmbracelet/bubbles/viewport"
	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"
	"github.com/charmbracelet/x/ansi"
)

var providerPresets = []providerInfo{
	{"openai", "https://api.openai.com/v1", "gpt-4o", ""},
	{"ollama", "http://localhost:11434/v1", "llama3", ""},
	{"vllm", "http://localhost:8000/v1", "model", ""},
	{"llama.cpp", "http://localhost:8080/v1", "model", ""},
	{"ik-llama", "http://localhost:8080/v1", "model", ""},
}

func newConnectionState() *connectionState {
	return &connectionState{
		step:          0,
		providers:     append([]providerInfo{}, providerPresets...),
		selectedIndex: 0,
		fieldIndex:    0,
	}
}

func getSettingsPath() string {
	home, err := os.UserHomeDir()
	if err != nil {
		home = os.Getenv("HOME")
	}
	if home == "" {
		home = "/tmp"
	}
	return filepath.Join(home, ".goosecode", "settings.json")
}

type settingsJSON struct {
	Provider         string                     `json:"provider"`
	BaseURL          string                     `json:"base_url"`
	Model            string                     `json:"model"`
	ProviderProfiles map[string]providerProfile `json:"provider_profiles"`
}

type providerProfile struct {
	BaseURL string `json:"base_url"`
	Model   string `json:"model"`
	APIKey  string `json:"api_key"`
}

func hasConfiguredProvider() bool {
	path := getSettingsPath()
	data, err := os.ReadFile(path)
	if err != nil {
		return false
	}
	var settings settingsJSON
	if err := json.Unmarshal(data, &settings); err != nil {
		return false
	}
	if settings.Provider != "" && settings.Model != "" {
		return true
	}
	if settings.ProviderProfiles != nil && len(settings.ProviderProfiles) > 0 {
		for _, p := range settings.ProviderProfiles {
			if p.Model != "" && p.BaseURL != "" {
				return true
			}
		}
	}
	return false
}

func loadProvidersFromSettings() []providerInfo {
	path := getSettingsPath()
	data, err := os.ReadFile(path)
	if err != nil {
		return nil
	}
	var settings settingsJSON
	if err := json.Unmarshal(data, &settings); err != nil {
		return nil
	}
	providers := []providerInfo{}
	if settings.ProviderProfiles != nil {
		for name, p := range settings.ProviderProfiles {
			providers = append(providers, providerInfo{
				name:    name,
				baseURL: p.BaseURL,
				model:   p.Model,
				apiKey:  p.APIKey,
			})
		}
	}
	return providers
}

func saveProviderToSettings(p providerInfo) error {
	path := getSettingsPath()
	dir := filepath.Dir(path)
	if err := os.MkdirAll(dir, 0755); err != nil {
		return err
	}

	var settings settingsJSON
	data, err := os.ReadFile(path)
	if err == nil {
		json.Unmarshal(data, &settings)
	}
	if settings.ProviderProfiles == nil {
		settings.ProviderProfiles = make(map[string]providerProfile)
	}

	settings.Provider = p.name
	settings.ProviderProfiles[p.name] = providerProfile{
		BaseURL: p.baseURL,
		Model:   p.model,
		APIKey:  p.apiKey,
	}

	out, err := json.MarshalIndent(settings, "", "  ")
	if err != nil {
		return err
	}
	return os.WriteFile(path, out, 0644)
}

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

func formatTokenCount(n int64) string {
	s := fmt.Sprintf("%d", n)
	if len(s) <= 3 {
		return s
	}
	result := make([]byte, 0, len(s)+(len(s)-1)/3)
	for i, digit := range s {
		if i > 0 && (len(s)-i)%3 == 0 {
			result = append(result, ',')
		}
		result = append(result, byte(digit))
	}
	return string(result)
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
	// Config from backend (flat structure to match backend's init_ok)
	Provider string `json:"provider,omitempty"`
	BaseURL  string `json:"base_url,omitempty"`
	Model    string `json:"model,omitempty"`
	// Tool-related fields
	ToolName   string                 `json:"name,omitempty"`
	ToolID     string                 `json:"id,omitempty"`
	ToolOutput string                 `json:"output,omitempty"`
	ToolArgs   map[string]interface{} `json:"args,omitempty"`
	// Token tracking
	InputTokens         int64 `json:"input_tokens,omitempty"`
	OutputTokens        int64 `json:"output_tokens,omitempty"`
	CacheReadTokens     int64 `json:"cache_read_tokens,omitempty"`
	CacheCreationTokens int64 `json:"cache_creation_tokens,omitempty"`
	ContextWindow       int64 `json:"context_window,omitempty"`
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

// RequestInput reads a request_input message and returns the prompt
func (b *Backend) RequestInput() (string, error) {
	resp, err := b.ReadResponse()
	if err != nil {
		return "", err
	}
	if resp.Type != "request_input" {
		return "", fmt.Errorf("expected request_input, got %s", resp.Type)
	}
	return resp.Content, nil
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

func (b *Backend) SendConfig(provider, baseURL, model string) error {
	return b.Send(TUIRequest{
		Type: "config",
		Config: &struct {
			Model    string `json:"model,omitempty"`
			Provider string `json:"provider,omitempty"`
			BaseURL  string `json:"base_url,omitempty"`
		}{
			Model:    model,
			Provider: provider,
			BaseURL:  baseURL,
		},
	})
}

func (b *Backend) SendResponse(text string) error {
	return b.Send(TUIRequest{Type: "response", Text: text})
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
type requestInputMsg struct {
	prompt string
}
type tokenUpdateMsg struct {
	inputTokens         int64
	outputTokens        int64
	cacheReadTokens     int64
	cacheCreationTokens int64
	contextWindow       int64
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

type providerInfo struct {
	name    string
	baseURL string
	model   string
	apiKey  string
}

type connectionState struct {
	step          int
	providers     []providerInfo
	selectedIndex int
	editing       providerInfo
	editingIndex  int
	providerName  string
	baseURL       string
	model         string
	apiKey        string
	testResult    string
	testSuccess   bool
	fieldIndex    int
	pendingInput  string
}

type model struct {
	mu         sync.Mutex // Protects concurrent access to model fields
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
	activeBaseURL           string
	// Connection wizard state
	connectionState *connectionState
	// Tab toggle debounce
	tabTogglePending bool
	// Input request state
	requestingInput    bool
	requestInputPrompt string
	// Token tracking
	totalContextTokens int64
	contextWindow      int64
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
	parts := make([]string, 0, 5)
	if m.connected {
		parts = append(parts, "connected")
	}
	if m.activeProvider != "" || m.activeModel != "" {
		providerModel := strings.TrimPrefix(strings.TrimSpace(m.activeProvider+"/"+m.activeModel), "/")
		if providerModel != "" {
			parts = append(parts, providerModel)
		}
	}
	if m.activeBaseURL != "" {
		parts = append(parts, m.activeBaseURL)
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
		successStyle + "Overview" + resetStyle,
		"Type a normal prompt and press Enter to send it.",
		"Type a slash command to run a local TUI action or forward a command to Goose.",
		"Most slash commands are forwarded to the backend unchanged.",
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
		"While Goose is responding the composer shows a busy marker",
		"",
		successStyle + "Local Commands" + resetStyle,
		"/help opens this overlay locally",
		"/help off closes this overlay",
		"/clear resets the transcript",
		"/exit leaves the TUI",
		"/tab mirrors the Tab mode toggle",
		"",
		successStyle + "Common Backend Commands" + resetStyle,
		"/model list",
		"/model set <name>",
		"/provider list",
		"/provider set <name>",
		"/provider test",
		"/session",
		"/compact",
		"/plan",
		"/config",
		"/tasks",
		"/branch",
		"/commit",
		"/review",
		"/subagents",
		"/permissions",
		"/tools",
		"",
		successStyle + "Tools" + resetStyle,
		"Ctrl+P/Ctrl+N switch the selected tool block",
		"Ctrl+O toggles the selected tool output block",
		"Long tool output starts compact and can be expanded",
		"Selected tool blocks are marked in the transcript and banner",
		"",
		successStyle + "Examples" + resetStyle,
		"/provider list",
		"/provider set ollama",
		"/provider test",
		"/model list",
		"/model set llama3",
		"/tasks create investigate parser failure",
		"/review",
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
			// Connection wizard: cancel and exit
			if m.connectionState != nil {
				m.connectionState = nil
				return m, nil
			}
		case "up", "pageup", "pgup":
			if m.connectionState != nil {
				// Connection wizard navigation
				if m.connectionState.step == 0 {
					if msg.String() == "up" {
						m.connectionState.selectedIndex--
						if m.connectionState.selectedIndex < 0 {
							m.connectionState.selectedIndex = len(m.connectionState.providers) - 1
						}
					} else {
						m.connectionState.selectedIndex = 0
					}
					return m, nil
				}
				return m, nil
			}
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
			if m.connectionState != nil {
				// Connection wizard navigation
				if m.connectionState.step == 0 {
					if msg.String() == "down" {
						m.connectionState.selectedIndex++
						if m.connectionState.selectedIndex >= len(m.connectionState.providers) {
							m.connectionState.selectedIndex = 0
						}
					} else {
						m.connectionState.selectedIndex = len(m.connectionState.providers) - 1
					}
					return m, nil
				}
				return m, nil
			}
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
			if m.connectionState != nil {
				// Connection wizard: cycle through fields in edit mode
				if m.connectionState.step == 1 {
					m.connectionState.fieldIndex = (m.connectionState.fieldIndex + 1) % 3
				}
				return m, nil
			}
			if m.planMode {
				m.sendCommand("plan", "off")
			} else {
				m.sendCommand("plan", "")
			}
			m.relayout()
			return m, nil
		case "enter":
			// Connection wizard handling
			if m.connectionState != nil {
				if m.connectionState.step == 0 {
					// Select provider from list and go to edit mode
					idx := m.connectionState.selectedIndex
					if idx >= 0 && idx < len(m.connectionState.providers) {
						p := m.connectionState.providers[idx]
						m.connectionState.editing = p
						m.connectionState.editingIndex = idx
						m.connectionState.providerName = p.name
						m.connectionState.baseURL = p.baseURL
						m.connectionState.model = p.model
						m.connectionState.apiKey = p.apiKey
					} else {
						// New provider - clear fields
						m.connectionState.editing = providerInfo{}
						m.connectionState.providerName = ""
						m.connectionState.baseURL = ""
						m.connectionState.model = ""
						m.connectionState.apiKey = ""
					}
					m.connectionState.step = 1
					m.connectionState.fieldIndex = 0
					m.connectionState.pendingInput = ""
					m.textInput.Reset()
					return m, nil
				}
				if m.connectionState.step == 1 {
					// Apply pending input to current field (only if not empty)
					text := m.connectionState.pendingInput
					if text != "" {
						switch m.connectionState.fieldIndex {
						case 0:
							m.connectionState.providerName = text
						case 1:
							m.connectionState.baseURL = text
						case 2:
							m.connectionState.model = text
						}
					}
					m.connectionState.pendingInput = ""
					m.textInput.Reset()

					// Move to next field or finish
					if m.connectionState.fieldIndex < 2 {
						m.connectionState.fieldIndex++
					} else {
						// All fields filled - go to API key step
						m.connectionState.step = 2
					}
					return m, nil
				}
				if m.connectionState.step == 2 {
					// API key step - test connection first
					m.connectionState.apiKey = m.connectionState.pendingInput
					m.connectionState.pendingInput = ""
					m.textInput.Reset()

					// Run connection test
					ok, result := testConnection(m.connectionState.baseURL, m.connectionState.apiKey, m.connectionState.model)
					m.connectionState.testSuccess = ok
					m.connectionState.testResult = result

					// Go to test result step
					m.connectionState.step = 3
					return m, nil
				}
				if m.connectionState.step == 3 {
					if !m.connectionState.testSuccess {
						// Bad connection - go back to edit
						m.connectionState.step = 1
						return m, nil
					}
					// Good connection - apply and exit
					m.activeProvider = m.connectionState.providerName
					m.activeModel = m.connectionState.model
					m.activeBaseURL = m.connectionState.baseURL

					// Save to settings
					saveProviderToSettings(providerInfo{
						name:    m.connectionState.providerName,
						baseURL: m.connectionState.baseURL,
						model:   m.connectionState.model,
						apiKey:  m.connectionState.apiKey,
					})

					// Send config to backend
					if m.backend != nil {
						m.backend.SendConfig(m.connectionState.providerName, m.connectionState.baseURL, m.connectionState.model)
					}

					// Exit wizard
					m.connectionState = nil
					return m, nil
				}
				return m, nil
			}

			if m.textInput.Value() == "" {
				return m, textarea.Blink
			}

			text := m.textInput.Value()
			m.textInput.Reset()

			// If we're waiting for input for a backend request, send response
			if m.requestingInput {
				m.requestingInput = false
				m.requestInputPrompt = ""
				if m.backend != nil {
					m.backend.SendResponse(text)
				}
				m.appendTranscriptEntry(transcriptUser, text, "", false)
				m.syncViewport(true)
				return m, textarea.Blink
			}

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

				if cmdName == "connect" {
					m.connectionState = newConnectionState()
					m.connectionState.providers = loadProvidersFromSettings()
					if m.connectionState.providers == nil {
						m.connectionState.providers = append([]providerInfo{}, providerPresets...)
					} else {
						m.connectionState.providers = append(m.connectionState.providers, providerPresets...)
					}
					return m, nil
				}

				if cmdName == "provider" {
					// Handle /provider command locally
					if args == "" || args == "show" {
						// Show current provider config
						info := fmt.Sprintf("Current provider:\n  provider: %s\n  base_url: %s\n  model: %s\n",
							m.activeProvider, m.activeBaseURL, m.activeModel)
						m.appendTranscriptEntry(transcriptSystem, info, "", false)
						m.syncViewport(true)
						return m, nil
					}
					if args == "list" {
						// List available presets
						var list bytes.Buffer
						list.WriteString("Available provider presets:\n")
						for _, p := range providerPresets {
							list.WriteString(fmt.Sprintf("  - %s -> %s (%s)\n", p.name, p.baseURL, p.model))
						}
						m.appendTranscriptEntry(transcriptSystem, list.String(), "", false)
						m.syncViewport(true)
						return m, nil
					}
					if strings.HasPrefix(args, "set ") {
						providerName := strings.TrimPrefix(args, "set ")
						// Find the provider preset
						for i, p := range providerPresets {
							if p.name == providerName {
								// Open connection wizard with this provider preselected
								m.connectionState = newConnectionState()
								m.connectionState.providers = loadProvidersFromSettings()
								if m.connectionState.providers == nil {
									m.connectionState.providers = append([]providerInfo{}, providerPresets...)
								} else {
									m.connectionState.providers = append(m.connectionState.providers, providerPresets...)
								}
								m.connectionState.selectedIndex = i
								m.connectionState.step = 1
								m.connectionState.fieldIndex = 0
								m.connectionState.providerName = p.name
								m.connectionState.baseURL = p.baseURL
								m.connectionState.model = p.model
								m.connectionState.apiKey = ""
								return m, nil
							}
						}
						m.appendTranscriptEntry(transcriptError, "Unknown provider: "+providerName+". Use /provider list for available presets.", "", false)
						m.syncViewport(true)
						return m, nil
					}
					// Pass to backend for other cases
					m.sendCommand(cmdName, args)
					m.appendTranscriptEntry(transcriptCommand, cmdName, args, false)
					m.syncViewport(true)
					return m, nil
				}

				if cmdName == "model" {
					// Handle /model command locally
					if args == "" || args == "show" {
						// Show current model
						info := fmt.Sprintf("Current model: %s\nProvider: %s\n", m.activeModel, m.activeProvider)
						m.appendTranscriptEntry(transcriptCommand, "model", args, false)
						m.appendTranscriptEntry(transcriptSystem, info, "", false)
						m.syncViewport(true)
						return m, nil
					}
					if args == "list" {
						// Let backend handle listing models (it knows the API)
						m.appendTranscriptEntry(transcriptCommand, "model", "list", false)
						m.syncViewport(true)
						if m.backend != nil {
							m.sendCommand("model", "list")
						}
						return m, nil
					}
					if strings.HasPrefix(args, "set ") {
						modelName := strings.TrimPrefix(args, "set ")
						// Switch model without saving to settings
						m.activeModel = modelName
						if m.backend != nil {
							m.backend.SendConfig(m.activeProvider, m.activeBaseURL, modelName)
						}
						m.appendTranscriptEntry(transcriptCommand, "model", args, false)
						info := fmt.Sprintf("Model switched to: %s\n", modelName)
						m.appendTranscriptEntry(transcriptSystem, info, "", false)
						m.syncViewport(true)
						return m, nil
					}
					// /model <name> - treat as set
					if args != "" && !strings.Contains(args, " ") {
						m.activeModel = args
						if m.backend != nil {
							m.backend.SendConfig(m.activeProvider, m.activeBaseURL, args)
						}
						m.appendTranscriptEntry(transcriptCommand, "model", args, false)
						info := fmt.Sprintf("Model switched to: %s\n", args)
						m.appendTranscriptEntry(transcriptSystem, info, "", false)
						m.syncViewport(true)
						return m, nil
					}
					// Pass to backend for other cases
					m.sendCommand(cmdName, args)
					m.appendTranscriptEntry(transcriptCommand, cmdName, args, false)
					m.syncViewport(true)
					return m, nil
				}

				if cmdName == "tab" {
					if m.planMode {
						m.sendCommand("plan", "off")
					} else {
						m.sendCommand("plan", "")
					}
					m.relayout()
					return m, nil
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
			// Capture typed input in connection wizard
			if m.connectionState != nil && (m.connectionState.step == 1 || m.connectionState.step == 2) {
				m.connectionState.pendingInput = m.textInput.Value()
			}
			return m, cmd
		}

	case responseMsg:
		follow := m.viewport.AtBottom()
		content := string(msg)
		if content != "" {
			m.mu.Lock()
			if m.currentAssistantEntry < 0 || m.currentAssistantEntry >= len(m.entries) || m.entries[m.currentAssistantEntry].kind != transcriptAssistant {
				m.currentAssistantEntry = m.appendTranscriptEntry(transcriptAssistant, "", "", false)
			}
			m.appendToTranscriptEntry(m.currentAssistantEntry, content)
			m.awaitingAssistantPrefix = strings.IndexFunc(m.entries[m.currentAssistantEntry].text, func(r rune) bool {
				return r != '\n'
			}) == -1
			m.mu.Unlock()
		}
		m.syncViewport(follow)
		m.noteViewportState(follow, msg != "")
		return m, textarea.Blink
	case responseDoneMsg:
		m.mu.Lock()
		m.assistantResponding = false
		m.awaitingAssistantPrefix = false
		m.currentAssistantEntry = -1
		m.applyComposerState()
		m.mu.Unlock()
		return m, textarea.Blink
	case backendErrorMsg:
		follow := m.viewport.AtBottom()
		m.mu.Lock()
		m.assistantResponding = false
		m.currentAssistantEntry = -1
		m.awaitingAssistantPrefix = false
		m.applyComposerState()
		m.appendTranscriptEntry(transcriptError, string(msg), "", false)
		m.mu.Unlock()
		m.syncViewport(follow)
		m.noteViewportState(follow, msg != "")
		return m, textarea.Blink
	case toolStartMsg:
		follow := m.viewport.AtBottom()
		m.mu.Lock()
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
		m.mu.Unlock()
		m.syncViewport(follow)
		m.noteViewportState(follow, true)
		return m, textarea.Blink
	case toolOutputMsg:
		m.mu.Lock()
		if m.currentToolID == msg.id {
			follow := m.viewport.AtBottom()
			// Truncate long output
			output := msg.output
			const truncationSuffix = "... (truncated)"
			const maxToolOutput = 10000

			if len(m.currentToolOutput)+len(output) > maxToolOutput {
				m.currentToolTruncated = true
				remaining := maxToolOutput - len(m.currentToolOutput) - len(truncationSuffix)
				if remaining > 0 {
					output = output[:remaining] + truncationSuffix
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
			m.relayout()
			m.mu.Unlock()
			m.syncViewport(follow)
			m.noteViewportState(follow, output != "")
		} else {
			m.mu.Unlock()
		}
		return m, textarea.Blink
	case toolEndMsg:
		m.mu.Lock()
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
			m.relayout()
			m.mu.Unlock()
			m.syncViewport(follow)
			m.noteViewportState(follow, true)
		} else {
			m.mu.Unlock()
		}
		return m, textarea.Blink
	case tea.WindowSizeMsg:
		follow := m.viewport.AtBottom()
		m.mu.Lock()
		m.viewportWidth = msg.Width
		m.windowHeight = msg.Height
		m.relayout()
		m.mu.Unlock()
		m.syncViewport(follow)
		return m, textarea.Blink
	case requestInputMsg:
		// Show the prompt and wait for user input
		follow := m.viewport.AtBottom()
		m.mu.Lock()
		m.appendTranscriptEntry(transcriptUser, msg.prompt, "", false)
		m.requestInputPrompt = msg.prompt
		m.requestingInput = true
		m.textInput.Focus()
		m.applyComposerState()
		m.mu.Unlock()
		m.syncViewport(follow)
		return m, textarea.Blink
	case bool:
		// Sync plan mode from backend's authoritative state via session_info
		m.mu.Lock()
		m.planMode = msg
		if msg {
			m.textInput.Cursor.Style = lipgloss.NewStyle().Foreground(lipgloss.Color("33")) // Yellow for plan
		} else {
			m.textInput.Cursor.Style = lipgloss.NewStyle().Foreground(lipgloss.Color("32")) // Green for build
		}
		m.mu.Unlock()
		m.relayout()
		return m, nil
	case tokenUpdateMsg:
		m.mu.Lock()
		m.totalContextTokens = msg.inputTokens + msg.outputTokens + msg.cacheReadTokens + msg.cacheCreationTokens
		m.contextWindow = msg.contextWindow
		m.mu.Unlock()
		return m, nil
	}

	return m, nil
}

func testConnection(baseURL, apiKey, modelName string) (bool, string) {
	if baseURL == "" {
		return false, "Base URL is empty"
	}

	base := strings.TrimSuffix(baseURL, "/")
	client := &http.Client{Timeout: 10 * time.Second}

	// Step 1: Check if base URL is reachable
	req, err := http.NewRequest("GET", base, nil)
	if err != nil {
		return false, fmt.Sprintf("Invalid URL: %s", baseURL)
	}
	if apiKey != "" {
		req.Header.Set("Authorization", "Bearer "+apiKey)
	}

	resp, err := client.Do(req)
	if err != nil {
		if strings.Contains(err.Error(), "connection refused") {
			return false, fmt.Sprintf("Connection refused - is the server running at %s?", baseURL)
		}
		if strings.Contains(err.Error(), "no such host") || strings.Contains(err.Error(), "lookup") {
			return false, fmt.Sprintf("Host not found: %s", baseURL)
		}
		if strings.Contains(err.Error(), "timeout") {
			return false, fmt.Sprintf("Connection timeout - is the server running at %s?", baseURL)
		}
		return false, fmt.Sprintf("Connection failed: %v", err)
	}
	defer resp.Body.Close()
	io.ReadAll(resp.Body)

	// Step 2: Check models endpoint and verify model name
	modelEndpoints := []string{"/v1/models", "/models"}
	var modelErr error
	for _, ep := range modelEndpoints {
		modelURL := base + ep
		req2, err := http.NewRequest("GET", modelURL, nil)
		if err != nil {
			modelErr = err
			continue
		}
		if apiKey != "" {
			req2.Header.Set("Authorization", "Bearer "+apiKey)
		}

		resp2, err := client.Do(req2)
		if err != nil {
			modelErr = err
			continue
		}
		body, _ := io.ReadAll(resp2.Body)
		resp2.Body.Close()

		if resp2.StatusCode >= 200 && resp2.StatusCode < 300 {
			// Parse models list and check if model name matches
			if modelName != "" {
				var modelsData map[string]interface{}
				if json.Unmarshal(body, &modelsData) == nil {
					if data, ok := modelsData["data"].([]interface{}); ok {
						found := false
						for _, m := range data {
							if mmap, ok := m.(map[string]interface{}); ok {
								if id, ok := mmap["id"].(string); ok {
									if id == modelName || strings.HasSuffix(id, "/"+modelName) || id == modelName+"-4bit" {
										found = true
										break
									}
								}
							}
						}
						if !found {
							// Try partial match - show available models
							var modelList []string
							for _, m := range data {
								if mmap, ok := m.(map[string]interface{}); ok {
									if id, ok := mmap["id"].(string); ok {
										modelList = append(modelList, id)
									}
								}
							}
							return false, fmt.Sprintf("Model '%s' not found. Available: %s", modelName, strings.Join(modelList, ", "))
						}
					}
				}
			}
			return true, fmt.Sprintf("OK (HTTP %d) - server and models endpoint working", resp2.StatusCode)
		}
		modelErr = fmt.Errorf("HTTP %d", resp2.StatusCode)
	}

	// Base URL works but models endpoint doesn't
	if modelErr != nil {
		if strings.Contains(modelErr.Error(), "HTTP 404") {
			return false, fmt.Sprintf("Server reachable (HTTP %d) but /v1/models not found - check model name", resp.StatusCode)
		}
		return false, fmt.Sprintf("Server reachable (HTTP %d) but models endpoint failed: %v", resp.StatusCode, modelErr)
	}

	return false, "Unknown error"
}

func (m model) renderConnectionGuide() string {
	if m.connectionState == nil {
		return ""
	}

	var s strings.Builder
	width := m.renderWidth()
	if width < 50 {
		width = 50
	}
	borderTop := "\n\033[1;36m  Connection Guide\033[0m\n" +
		"\033[36m  " + strings.Repeat("─", width) + "\033[0m\n"

	s.WriteString(borderTop)

	if m.connectionState.step == 0 {
		// Provider list
		s.WriteString("  Select a provider:\n\n")
		for i, p := range m.connectionState.providers {
			prefix := "    "
			if i == m.connectionState.selectedIndex {
				prefix = "\033[32m  ▶ \033[0m"
			}
			s.WriteString(fmt.Sprintf("%s%s  %s | %s | %s\n", prefix, p.name, p.baseURL, p.model, p.apiKey))
		}
		s.WriteString("\n  \033[90m[↑/↓] navigate  [Enter] select  [Esc] cancel\033[0m\n")
	} else if m.connectionState.step == 1 {
		// Edit provider details with arrow indicator
		fields := []struct {
			label    string
			value    string
			isActive bool
		}{
			{"Provider", m.connectionState.providerName, m.connectionState.fieldIndex == 0},
			{"Base URL", m.connectionState.baseURL, m.connectionState.fieldIndex == 1},
			{"Model", m.connectionState.model, m.connectionState.fieldIndex == 2},
		}
		for _, f := range fields {
			prefix := "    "
			if f.isActive {
				prefix = "\033[32m  ▶ \033[0m"
			}
			s.WriteString(fmt.Sprintf("%s%s: \033[33m%s\033[0m\n", prefix, f.label, f.value))
		}
		s.WriteString("\n  \033[90m[Tab] next field  [Enter] advance  [Esc] cancel\033[0m\n")
	} else if m.connectionState.step == 2 {
		// API key
		s.WriteString(fmt.Sprintf("  Provider: \033[32m%s\033[0m\n", m.connectionState.providerName))
		s.WriteString(fmt.Sprintf("  Base URL: \033[32m%s\033[0m\n", m.connectionState.baseURL))
		s.WriteString(fmt.Sprintf("  Model: \033[32m%s\033[0m\n", m.connectionState.model))
		s.WriteString("\n  API Key (optional): ")
		if m.connectionState.pendingInput != "" {
			s.WriteString(fmt.Sprintf("\033[33m%s\033[0m", m.connectionState.pendingInput))
		} else {
			s.WriteString("\033[90m[empty for none]\033[0m")
		}
		s.WriteString("\n")
	} else if m.connectionState.step == 3 {
		// Connection test result
		s.WriteString(fmt.Sprintf("  Provider: \033[32m%s\033[0m\n", m.connectionState.providerName))
		s.WriteString(fmt.Sprintf("  Base URL: \033[32m%s\033[0m\n", m.connectionState.baseURL))
		s.WriteString(fmt.Sprintf("  Model: \033[32m%s\033[0m\n", m.connectionState.model))
		s.WriteString("\n  Testing connection...\n")

		// Check if it's a real failure (can't connect) vs warning (models endpoint issue)
		result := m.connectionState.testResult
		isRealFailure := strings.Contains(result, "Connection refused") ||
			strings.Contains(result, "Host not found") ||
			strings.Contains(result, "Connection timeout") ||
			strings.Contains(result, "Connection failed") ||
			strings.Contains(result, "Invalid URL")

		if m.connectionState.testSuccess {
			s.WriteString(fmt.Sprintf("  \033[32m%s\033[0m\n", result))
			s.WriteString("  \033[32m[Enter] Accept and connect  [Esc] Go back\033[0m\n")
		} else if isRealFailure {
			s.WriteString(fmt.Sprintf("  \033[31m%s\033[0m\n", result))
			s.WriteString("  \033[31m[Enter] Go back to edit  [Esc] cancel\033[0m\n")
		} else {
			// Base URL reachable but models endpoint issue - warning but can proceed
			s.WriteString(fmt.Sprintf("  \033[33m%s\033[0m\n", result))
			s.WriteString("  \033[33m[Enter] Accept anyway  [Esc] Go back\033[0m\n")
		}
	}

	s.WriteString("  \033[36m" + strings.Repeat("─", width) + "\033[0m\n")
	return s.String()
}

func (m model) View() string {
	var s strings.Builder
	status := m.sessionStatus()
	headerPrefix := ""

	// Header (fixed at top)
	if m.planMode {
		headerPrefix = "\033[1m    __      \033[0m  \033[1;33mGOOSE CODE\033[0m v0.3.2 \033[33m[PLAN]\033[0m"
	} else {
		headerPrefix = "\033[1m    __      \033[0m  \033[1;36mGOOSE CODE\033[0m v0.3.2 \033[32m[BUILD]\033[0m"
	}
	if m.assistantResponding {
		headerPrefix += " \033[35m[RESPONDING]\033[0m"
	}
	s.WriteString(headerLine(headerPrefix, status, m.renderWidth()))
	s.WriteString("\n")
	s.WriteString("\033[1m___( o)>  \033[0m  ╔═╗╔═╗╔═╗╔═╗╔═╗  ╔═╗╔═╗╔╦╗╔═╗\n")
	s.WriteString("\033[1m\\ <_. )   \033[0m  ║ ╦║ ║║ ║╚═╗║╣   ║  ║ ║ ║║║╣ \n")
	// Context token count on same line as ASCII art line 3
	if m.totalContextTokens > 0 {
		var ctxStr string
		if m.contextWindow > 0 {
			pct := int((m.totalContextTokens * 100) / m.contextWindow)
			ctxStr = fmt.Sprintf("\033[90m%s (%d%%)\033[0m", formatTokenCount(m.totalContextTokens), pct)
		} else {
			ctxStr = fmt.Sprintf("\033[90m%s\033[0m", formatTokenCount(m.totalContextTokens))
		}
		s.WriteString("\033[1m `---'    \033[0m  ╚═╝╚═╝╚═╝╚═╝╚═╝  ╚═╝╚═╝═╩╝╚═╝  " + ctxStr + "\n")
	} else {
		s.WriteString("\033[1m `---'    \033[0m  ╚═╝╚═╝╚═╝╚═╝╚═╝  ╚═╝╚═╝═╩╝╚═╝\n")
	}

	// Chat messages (scrollable viewport)
	if m.connectionState != nil {
		s.WriteString(m.renderConnectionGuide())
	} else if m.showHelp {
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
	requestInputChan := make(chan string, 100)
	planModeChan := make(chan bool, 10)
	tokenUpdateChan := make(chan tokenUpdateMsg, 10)

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
				} else if resp.Type == "request_input" {
					requestInputChan <- resp.Content
				} else if resp.Type == "session_info" {
					// Sync plan mode from backend's authoritative state - send to channel
					planModeChan <- resp.PlanMode
				} else if resp.Type == "token_update" {
					tokenUpdateChan <- tokenUpdateMsg{
						inputTokens:         resp.InputTokens,
						outputTokens:        resp.OutputTokens,
						cacheReadTokens:     resp.CacheReadTokens,
						cacheCreationTokens: resp.CacheCreationTokens,
						contextWindow:       resp.ContextWindow,
					}
				}
			}
		}
	}()

	m := newModel(backend)
	m.connected = true
	// Use command-line flags first, but override with backend's actual config if available
	if resp.Provider != "" || resp.Model != "" || resp.BaseURL != "" {
		if resp.Provider != "" {
			m.activeProvider = resp.Provider
		}
		if resp.Model != "" {
			m.activeModel = resp.Model
		}
		if resp.BaseURL != "" {
			m.activeBaseURL = resp.BaseURL
		}
	} else {
		m.activeModel = cfg.Model
		m.activeProvider = cfg.Provider
		m.activeBaseURL = cfg.BaseURL
	}

	// First-run detection: if no provider configured, trigger connection wizard
	if cfg.Provider == "" && cfg.Model == "" && !hasConfiguredProvider() {
		m.connectionState = newConnectionState()
		m.connectionState.providers = loadProvidersFromSettings()
		if m.connectionState.providers == nil {
			m.connectionState.providers = append([]providerInfo{}, providerPresets...)
		} else {
			// Append presets to saved providers
			m.connectionState.providers = append(m.connectionState.providers, providerPresets...)
		}
	}

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

	var wg sync.WaitGroup

	// Run goroutines to send messages to TUI
	wg.Add(1)
	go func() {
		defer wg.Done()
		for msg := range respChan {
			p.Send(msg)
		}
	}()

	wg.Add(1)
	go func() {
		defer wg.Done()
		for msg := range respDoneChan {
			p.Send(msg)
		}
	}()

	wg.Add(1)
	go func() {
		defer wg.Done()
		for msg := range errChan {
			p.Send(msg)
		}
	}()

	wg.Add(1)
	go func() {
		defer wg.Done()
		for msg := range toolStartChan {
			p.Send(msg)
		}
	}()

	wg.Add(1)
	go func() {
		defer wg.Done()
		for msg := range toolOutputChan {
			p.Send(msg)
		}
	}()

	wg.Add(1)
	go func() {
		defer wg.Done()
		for msg := range toolEndChan {
			p.Send(msg)
		}
	}()

	wg.Add(1)
	go func() {
		defer wg.Done()
		for msg := range requestInputChan {
			// Send prompt to TUI and wait for user input
			p.Send(requestInputMsg{prompt: msg})
			// Wait for user input from requestInputRespChan
			// The actual reading of input happens in the Update function
		}
	}()

	wg.Add(1)
	go func() {
		defer wg.Done()
		for planMode := range planModeChan {
			p.Send(planMode)
		}
	}()

	wg.Add(1)
	go func() {
		defer wg.Done()
		for msg := range tokenUpdateChan {
			p.Send(msg)
		}
	}()

	// Run the TUI program
	if _, runErr := p.Run(); runErr != nil {
		fmt.Fprintf(os.Stderr, "Error running TUI: %v\n", runErr)
		os.Exit(1)
	}

	// Close all channels to signal goroutines to exit
	close(respChan)
	close(respDoneChan)
	close(errChan)
	close(toolStartChan)
	close(toolOutputChan)
	close(toolEndChan)
	close(requestInputChan)
	close(planModeChan)
	close(tokenUpdateChan)

	// Wait for all goroutines to finish before exiting
	wg.Wait()
}
