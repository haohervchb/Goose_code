package main

import (
	"bufio"
	"encoding/json"
	"flag"
	"fmt"
	"os"
	"os/exec"
	"strings"

	tea "github.com/charmbracelet/bubbletea"
)

type Backend struct {
	cmd     *exec.Cmd
	stdin   *os.File
	stdout  *bufio.Scanner
	stderr  *bufio.Scanner
}

type InitMessage struct {
	Type        string `json:"type"`
	WorkingDir  string `json:"working_dir"`
	Model       string `json:"model,omitempty"`
	Provider    string `json:"provider,omitempty"`
	BaseURL     string `json:"base_url,omitempty"`
}

type BackendMessage struct {
	Type        string `json:"type"`
	Content     string `json:"content,omitempty"`
	Done        bool   `json:"done,omitempty"`
	SessionID   string `json:"session_id,omitempty"`
	Error       string `json:"error,omitempty"`
}

func StartBackend(backendPath string) (*Backend, error) {
	cmd := exec.Command(backendPath, "--tui-mode")
	cmd.Stdin = os.Stdin
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr

	if err := cmd.Start(); err != nil {
		return nil, fmt.Errorf("failed to start backend: %w", err)
	}

	return &Backend{
		cmd:    cmd,
		stdin:  os.Stdin,
		stdout: bufio.NewScanner(os.Stdout),
		stderr: bufio.NewScanner(os.Stderr),
	}, nil
}

func (b *Backend) Close() error {
	if b.cmd.Process != nil {
		return b.cmd.Process.Kill()
	}
	return nil
}

type model struct {
	backend      *Backend
	backendPath  string
	initialized  bool
	messages     []string
	input        string
}

func (m model) Init() tea.Cmd {
	// Send init handshake to backend
	cwd, _ := os.Getwd()
	initMsg := InitMessage{
		Type:       "init",
		WorkingDir: cwd,
	}
	data, _ := json.Marshal(initMsg)
	fmt.Fprintln(m.backend.stdin, string(data))
	return nil
}

func (m model) Update(msg tea.Msg) (tea.Model, tea.Cmd) {
	switch msg := msg.(type) {
	case tea.KeyMsg:
		if msg.String() == "ctrl+c" || msg.String() == "q" {
			if m.backend != nil {
				m.backend.Close()
			}
			return m, tea.Quit
		}
	case tea.WindowSizeMsg:
		// Handle window resize if needed
	}
	return m, nil
}

func (m model) View() string {
	var b strings.Builder
	b.WriteString("goosecode TUI\n")
	b.WriteString("============\n\n")
	
	if !m.initialized {
		b.WriteString("Connecting to backend...\n")
	} else {
		b.WriteString("Connected! (Phase 2 - subprocess integration)\n")
	}
	
	b.WriteString("\nPress Ctrl+C or q to quit.\n")
	return b.String()
}

func main() {
	backendPath := flag.String("backend", "./goosecode", "Path to goosecode backend")
	flag.Parse()

	// Check if running in TUI mode (backend will set this flag when spawning us)
	// For now, we act as the TUI that spawns the backend
	
	// Start the backend
	backend, err := StartBackend(*backendPath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Failed to start backend: %v\n", err)
		os.Exit(1)
	}

	p := tea.NewProgram(model{backend: backend, backendPath: *backendPath})
	if _, err := p.Run(); err != nil {
		fmt.Fprintf(os.Stderr, "Error running TUI: %v\n", err)
		backend.Close()
		os.Exit(1)
	}
	
	backend.Close()
}