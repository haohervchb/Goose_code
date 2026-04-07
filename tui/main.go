package main

import (
	"bufio"
	"encoding/json"
	"flag"
	"fmt"
	"os"
	"os/exec"
	"strings"
	"time"

	"github.com/charmbracelet/bubbles/textinput"
	"github.com/charmbracelet/bubbles/viewport"
	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"
)

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

	cmd.Dir = "."
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

type model struct {
	backend   *Backend
	textInput textinput.Model
	viewport  viewport.Model
	output    string
	planMode  bool
	connected bool
	quit      bool
	fallback  bool // true if falling back to REPL
	// Tool state
	currentTool       string
	currentToolID     string
	currentToolOutput string
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
	ti := textinput.New()
	ti.Placeholder = "Type your message or /command..."
	ti.Prompt = "> "
	ti.CharLimit = 1000
	ti.Width = 80
	ti.Cursor.Style = lipgloss.NewStyle().Foreground(lipgloss.Color("32"))
	ti.Focus()

	vp := viewport.New(80, 20)
	vp.SetContent("")
	vp.YOffset = 0

	m := model{
		backend:   backend,
		textInput: ti,
		viewport:  vp,
		planMode:  false,
		connected: false,
		quit:      false,
		fallback:  false,
	}
	m.output = ""
	return m
}

func (m model) Init() tea.Cmd {
	return textinput.Blink
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
		case "up", "pageup":
			// Scroll up in viewport
			m.viewport, cmd = m.viewport.Update(msg)
			return m, cmd
		case "down", "pagedown":
			// Scroll down in viewport
			m.viewport, cmd = m.viewport.Update(msg)
			return m, cmd
		case "tab":
			m.planMode = !m.planMode
			m.backend.SendCommand("plan", "")
			// Update cursor color based on mode
			if m.planMode {
				m.textInput.Cursor.Style = lipgloss.NewStyle().Foreground(lipgloss.Color("33")) // Yellow for plan
				return m, tea.Batch(textinput.Blink, func() tea.Msg { return responseMsg("\n\033[33m[PLAN mode]\033[0m\n") })
			}
			m.textInput.Cursor.Style = lipgloss.NewStyle().Foreground(lipgloss.Color("32")) // Green for build
			return m, tea.Batch(textinput.Blink, func() tea.Msg { return responseMsg("\n\033[32m[BUILD mode]\033[0m\n") })
		case "enter":
			if m.textInput.Value() == "" {
				return m, textinput.Blink
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
					m.backend.SendQuit()
					return m, tea.Quit
				}

				if cmdName == "clear" {
					// Handle clear locally in TUI
					m.output = ""
					return m, textinput.Blink
				}

				if cmdName == "tab" {
					m.planMode = !m.planMode
					if m.planMode {
						m.backend.SendCommand("plan", "")
						return m, tea.Batch(textinput.Blink, func() tea.Msg { return responseMsg("\n[PLAN mode enabled]\n") })
					}
					m.backend.SendCommand("plan", "off")
					return m, tea.Batch(textinput.Blink, func() tea.Msg { return responseMsg("\n[PLAN mode disabled]\n") })
				}

				m.backend.SendCommand(cmdName, args)
				// Show command feedback
				m.output += fmt.Sprintf("\n%s[%s]%s %s\n", toolStyle, cmdName, resetStyleTool, args)
				return m, textinput.Blink
			}

			m.output += "\n> " + text + "\n"
			m.backend.SendPrompt(text)
			return m, tea.Batch(textinput.Blink, func() tea.Msg {
				return responseMsg("[Sent] " + text + "\n")
			})
		default:
			// Let textinput handle all other keys (typing)
			m.textInput, cmd = m.textInput.Update(msg)
			return m, cmd
		}

	case responseMsg:
		m.output += string(msg)
		m.viewport.SetContent(m.output)
		m.viewport.YOffset = 0
		m.viewport.GotoBottom()
		return m, textinput.Blink
	case backendErrorMsg:
		m.output += string(msg) + "\n"
		m.viewport.SetContent(m.output)
		m.viewport.YOffset = 0
		m.viewport.GotoBottom()
		return m, textinput.Blink
	case toolStartMsg:
		m.currentTool = msg.name
		m.currentToolID = msg.id
		m.currentToolOutput = ""
		m.output += fmt.Sprintf("\n%s[%s]%s %s%s%s\n",
			toolStyle, msg.name, resetStyleTool,
			toolArgsStyle, msg.args, resetStyleTool)
		m.viewport.SetContent(m.output)
		m.viewport.YOffset = 0
		m.viewport.GotoBottom()
		return m, textinput.Blink
	case toolOutputMsg:
		if m.currentToolID == msg.id {
			// Truncate long output
			output := msg.output
			if len(m.currentToolOutput)+len(output) > 10000 {
				remaining := 10000 - len(m.currentToolOutput)
				if remaining > 0 {
					output = output[:remaining] + "... (truncated)"
				} else {
					output = ""
				}
			}
			if output != "" {
				m.currentToolOutput += output
				m.output += output
			}
			m.viewport.SetContent(m.output)
			m.viewport.YOffset = 0
			m.viewport.GotoBottom()
		}
		return m, textinput.Blink
	case toolEndMsg:
		if m.currentToolID == msg.id {
			if msg.success {
				m.output += fmt.Sprintf("%s[✓]%s ", toolSuccessStyle, resetStyleTool)
			} else {
				m.output += fmt.Sprintf("%s[✗]%s %s", toolErrorStyle, resetStyleTool, msg.error)
			}
			m.output += "\n"
			m.currentTool = ""
			m.currentToolID = ""
			m.currentToolOutput = ""
			m.viewport.SetContent(m.output)
			m.viewport.YOffset = 0
			m.viewport.GotoBottom()
		}
		return m, textinput.Blink
	case tea.WindowSizeMsg:
		m.viewport.Width = msg.Width
		m.viewport.Height = msg.Height - 11 // 4 header + 1 sep + 3 input + 3 extra
		m.textInput.Width = msg.Width
		m.viewport.YOffset = 0
		m.viewport.GotoBottom()
		return m, textinput.Blink
	}

	return m, nil
}

func (m model) View() string {
	var s strings.Builder

	// Header (fixed at top)
	if m.planMode {
		s.WriteString("\033[1m    __      \033[0m  \033[1;33mGOOSE CODE\033[0m v0.2.0 \033[33m[PLAN]\033[0m | /help, /exit to quit\n")
	} else {
		s.WriteString("\033[1m    __      \033[0m  \033[1;36mGOOSE CODE\033[0m v0.2.0 \033[32m[BUILD]\033[0m | /help, /exit to quit\n")
	}
	s.WriteString("\033[1m___( o)>  \033[0m  ╔═╗╔═╗╔═╗╔═╗╔═╗  ╔═╗╔═╗╔╦╗╔═╗\n")
	s.WriteString("\033[1m\\ <_. )   \033[0m  ║ ╦║ ║║ ║╚═╗║╣   ║  ║ ║ ║║║╣ \n")
	s.WriteString("\033[1m `---'    \033[0m  ╚═╝╚═╝╚═╝╚═╝╚═╝  ╚═╝╚═╝═╩╝╚═╝\n")

	// Chat messages (scrollable viewport)
	s.WriteString(m.viewport.View())

	// Input area (fixed at bottom)
	if m.planMode {
		s.WriteString("\n\033[33m────────────────────────────────────────────────────────\033[0m\n")
	} else {
		s.WriteString("\n\033[36m────────────────────────────────────────────────────────\033[0m\n")
	}
	s.WriteString(m.textInput.View())

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
	backendPath := flag.String("backend", "./goosecode", "Path to goosecode backend")
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
					respChan <- responseMsg(resp.Content)
					if resp.Done {
						respChan <- responseMsg("\n")
					}
				} else if resp.Type == "error" {
					errChan <- backendErrorMsg(errorStyle + "Error: " + resp.Message + resetStyle)
				} else if resp.Type == "tool_start" {
					// Format args as string for display
					argsStr := ""
					if resp.ToolArgs != nil {
						for k, v := range resp.ToolArgs {
							if argsStr != "" {
								argsStr += ", "
							}
							argsStr += fmt.Sprintf("%s=%v", k, v)
						}
					}
					toolStartChan <- toolStartMsg{
						id:   resp.ToolID,
						name: resp.ToolName,
						args: argsStr,
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
		close(errChan)
		close(toolStartChan)
		close(toolOutputChan)
		close(toolEndChan)
	}()

	m := newModel(backend)
	m.connected = true
	m.output = fmt.Sprintf("\033[32mConnected!\033[0m Session: %s\n\n", resp.SessionID)
	m.viewport.SetContent(m.output)
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
