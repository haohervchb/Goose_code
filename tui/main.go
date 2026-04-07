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
	tea "github.com/charmbracelet/bubbletea"
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

type model struct {
	backend   *Backend
	textInput textinput.Model
	output    string
	planMode  bool
	connected bool
	quit      bool
}

func newModel(backend *Backend) model {
	ti := textinput.New()
	ti.Placeholder = "Type your message or /command..."
	ti.Prompt = "> "
	ti.CharLimit = 1000
	ti.Width = 80
	ti.Focus()

	return model{
		backend:   backend,
		textInput: ti,
		planMode:  false,
		connected: false,
		quit:      false,
	}
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
		case "tab":
			m.planMode = !m.planMode
			m.backend.SendCommand("plan", "")
			if m.planMode {
				return m, tea.Batch(textinput.Blink, func() tea.Msg { return responseMsg("\n[PLAN mode enabled]\n") })
			}
			return m, tea.Batch(textinput.Blink, func() tea.Msg { return responseMsg("\n[PLAN mode disabled]\n") })
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
					m.quit = true
					m.backend.SendQuit()
					return m, tea.Quit
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
		return m, textinput.Blink
	case backendErrorMsg:
		m.output += string(msg) + "\n"
		return m, textinput.Blink
	case tea.WindowSizeMsg:
		return m, textinput.Blink
	}

	return m, nil
}

func (m model) View() string {
	var s strings.Builder

	s.WriteString("\033[1;36m  __  \033[0m\n")
	s.WriteString("\033[1;36m ___ >-   GOOSE CODE\033[0m\n")
	s.WriteString("\033[1;36m  <_. )   TUI Mode  \033[0m\n")
	s.WriteString("\033[1;36m   `-'\033[0m\n\n")

	if m.connected {
		s.WriteString("\033[32mConnected! Session: " + m.backend.sessionID + "\033[0m\n")
	} else {
		s.WriteString("\033[33mConnecting...\033[0m\n")
	}

	s.WriteString("\n")

	if m.output != "" {
		s.WriteString(m.output)
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
				}
			}
		}
		close(respChan)
		close(errChan)
	}()

	m := newModel(backend)
	m.connected = true
	m.output = fmt.Sprintf("\033[32mConnected! Session: %s\033[0m\n\n", resp.SessionID)

	tty, err := os.OpenFile("/dev/tty", os.O_RDWR, 0)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Cannot open TTY: %v\n", err)
		os.Exit(1)
	}
	defer tty.Close()

	p := tea.NewProgram(&m, tea.WithAltScreen(), tea.WithInput(tty), tea.WithOutput(tty))

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

	if _, err := p.Run(); err != nil {
		fmt.Fprintf(os.Stderr, "Error running TUI: %v\n", err)
		os.Exit(1)
	}
}
