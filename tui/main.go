package main

import (
	"flag"
	"fmt"
	"os"
	"strings"
	"time"

	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/bubbles/textinput"
	"github.com/charmbracelet/lipgloss"
)

var (
	headerStyle = lipgloss.NewStyle().
			Bold(true).
			Foreground(lipgloss.Color("#6c71c4"))
	
	promptStyle = lipgloss.NewStyle().
			Foreground(lipgloss.Color("#268bd2"))
	
	errorStyle = lipgloss.NewStyle().
			Foreground(lipgloss.Color("#dc322f"))
	
	successStyle = lipgloss.NewStyle().
			Foreground(lipgloss.Color("#859900"))
)

type model struct {
	backend      *Backend
	backendPath  string
	initialized  bool
	connecting   bool
	err          error
	messages     []string
	input        textinput.Model
	planMode     bool
	sessionInfo  string
}

func newModel() model {
	ti := textinput.New()
	ti.Placeholder = "Type a message..."
	ti.Focus()
	
	return model{
		input:       ti,
		messages:    []string{},
		connecting:  true,
		planMode:    false,
	}
}

func (m model) Init() tea.Cmd {
	return func() tea.Msg {
		// Initialize backend connection
		backend, err := NewBackend(m.backendPath)
		if err != nil {
			return fmt.Errorf("failed to create backend: %w", err)
		}
		
		cfg := BackendConfig{}
		
		// Load config from environment
		if model := os.Getenv("OPENAI_MODEL"); model != "" {
			cfg.Model = model
		}
		if baseURL := os.Getenv("OPENAI_BASE_URL"); baseURL != "" {
			cfg.BaseURL = baseURL
		}
		if provider := os.Getenv("GOOSECODE_PROVIDER"); provider != "" {
			cfg.Provider = provider
		}
		
		if err := backend.Start(m.backendPath, cfg); err != nil {
			return fmt.Errorf("failed to start backend: %w", err)
		}
		
		cwd, _ := os.Getwd()
		if err := backend.SendInit(cwd, cfg); err != nil {
			return fmt.Errorf("failed to send init: %w", err)
		}
		
		// Wait for init_ok
		resp, err := backend.WaitForInit(10 * time.Second)
		if err != nil {
			backend.Close()
			return fmt.Errorf("init timeout: %w", err)
		}
		
		m.backend = backend
		m.initialized = true
		m.connecting = false
		m.sessionInfo = fmt.Sprintf("Session: %s", resp.SessionID)
		
		return nil
	}
}

func (m model) Update(msg tea.Msg) (tea.Model, tea.Cmd) {
	switch msg := msg.(type) {
	case error:
		m.err = msg
		return m, nil
		
	case string:
		// Add message to chat
		m.messages = append(m.messages, msg)
		return m, nil
		
	case tea.KeyMsg:
		if m.err != nil {
			if msg.String() == "q" || msg.String() == "ctrl+c" {
				return m, tea.Quit
			}
			return m, nil
		}
		
		switch msg.String() {
		case "ctrl+c", "q":
			if m.backend != nil {
				m.backend.SendQuit()
				m.backend.Close()
			}
			return m, tea.Quit
			
		case "tab":
			// Toggle plan mode
			m.planMode = !m.planMode
			if m.backend != nil {
				cmd := "exit_plan"
				if m.planMode {
					cmd = "enter_plan"
				}
				m.backend.SendCommand(cmd, "")
			}
			return m, nil
			
		case "enter":
			text := m.input.Value()
			if text == "" {
				return m, nil
			}
			
			m.input.SetValue("")
			
			// Check if it's a command
			if strings.HasPrefix(text, "/") {
				parts := strings.SplitN(text[1:], " ", 2)
				cmd := parts[0]
				var args string
				if len(parts) > 1 {
					args = parts[1]
				}
				m.messages = append(m.messages, fmt.Sprintf("> /%s %s", cmd, args))
				if m.backend != nil {
					m.backend.SendCommand(cmd, args)
				}
			} else {
				m.messages = append(m.messages, fmt.Sprintf("> %s", text))
				if m.backend != nil {
					m.backend.SendPrompt(text)
				}
			}
			return m, nil
		}
	}
	
	var cmd tea.Cmd
	m.input, cmd = m.input.Update(msg)
	return m, cmd
}

func (m model) View() string {
	var b strings.Builder
	
	// Header
	b.WriteString(headerStyle.Render("  __      \n ___ >-   GOOSE CODE\n  <_. )   TUI Mode\n   `-'    "))
	b.WriteString("\n\n")
	
	if m.connecting {
		b.WriteString("Connecting to backend...\n")
		b.WriteString(fmt.Sprintf("Backend: %s\n", m.backendPath))
		b.WriteString("\nPress Ctrl+C to cancel.\n")
		return b.String()
	}
	
	if m.err != nil {
		b.WriteString(errorStyle.Render("Error: "))
		b.WriteString(m.err.Error())
		b.WriteString("\n\n")
		b.WriteString("Press q to quit.\n")
		return b.String()
	}
	
	// Session info
	if m.sessionInfo != "" {
		b.WriteString(successStyle.Render(m.sessionInfo))
		b.WriteString("\n")
	}
	
	// Plan mode indicator
	if m.planMode {
		b.WriteString(promptStyle.Render("[ PLAN MODE ] "))
	} else {
		b.WriteString(promptStyle.Render("[ BUILD MODE ] "))
	}
	b.WriteString("\n\n")
	
	// Messages
	if len(m.messages) > 0 {
		b.WriteString(strings.Join(m.messages, "\n"))
		b.WriteString("\n\n")
	}
	
	// Input
	b.WriteString(m.input.View())
	
	return b.String()
}

func main() {
	backendPath := flag.String("backend", "./goosecode", "Path to goosecode backend")
	flag.Parse()
	
	m := newModel()
	m.backendPath = *backendPath
	
	p := tea.NewProgram(m, tea.WithAltScreen())
	if _, err := p.Run(); err != nil {
		fmt.Fprintf(os.Stderr, "Error running TUI: %v\n", err)
		os.Exit(1)
	}
}