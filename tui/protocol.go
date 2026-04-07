package main

import "encoding/json"

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

type BackendResponse struct {
	Type        string `json:"type"`
	Content     string `json:"content,omitempty"`
	Done        bool   `json:"done,omitempty"`
	SessionID   string `json:"session_id,omitempty"`
	SessionDir  string `json:"session_dir,omitempty"`
	MessageCount int   `json:"message_count,omitempty"`
	PlanMode    bool   `json:"plan_mode,omitempty"`
	ToolName    string `json:"name,omitempty"`
	ToolID      string `json:"id,omitempty"`
	ToolArgs    map[string]interface{} `json:"args,omitempty"`
	ToolOutput  string `json:"output,omitempty"`
	Success     bool   `json:"success,omitempty"`
	Error       string `json:"error,omitempty"`
	Message     string `json:"message,omitempty"`
}

func ParseResponse(data []byte) (*BackendResponse, error) {
	var resp BackendResponse
	if err := json.Unmarshal(data, &resp); err != nil {
		return nil, err
	}
	return &resp, nil
}

func NewInitRequest(workingDir, model, provider, baseURL string) TUIRequest {
	req := TUIRequest{
		Type:       "init",
		WorkingDir: workingDir,
	}
	if model != "" || provider != "" || baseURL != "" {
		req.Config = &struct {
			Model    string `json:"model,omitempty"`
			Provider string `json:"provider,omitempty"`
			BaseURL  string `json:"base_url,omitempty"`
		}{
			Model:    model,
			Provider: provider,
			BaseURL:  baseURL,
		}
	}
	return req
}

func NewPromptRequest(text string) TUIRequest {
	return TUIRequest{
		Type: "prompt",
		Text: text,
	}
}

func NewCommandRequest(name, args string) TUIRequest {
	return TUIRequest{
		Type: "command",
		Name: name,
		Args: args,
	}
}

func NewQuitRequest() TUIRequest {
	return TUIRequest{Type: "quit"}
}

func (r *TUIRequest) MarshalJSON() ([]byte, error) {
	return json.Marshal(r)
}