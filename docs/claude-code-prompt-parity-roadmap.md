# Claude Code Prompt Parity Roadmap

This document tracks the implementation plan for bringing goosecode closer to Claude Code's prompt-management and prompt-engineering stack.

Reference sources being mirrored:

- Core prompt assembly:
  - `~/claude_code_src/src/constants/prompts.ts`
  - `~/claude_code_src/src/utils/systemPrompt.ts`
  - `~/claude_code_src/src/constants/systemPromptSections.ts`
- Compaction:
  - `~/claude_code_src/src/services/compact/prompt.ts`
- Session memory:
  - `~/claude_code_src/src/services/SessionMemory/prompts.ts`
- Tool result budget and replacement:
  - `~/claude_code_src/src/utils/toolResultStorage.ts`
- Tool search / deferred tool exposure:
  - `~/claude_code_src/src/utils/toolSearch.ts`
  - `~/claude_code_src/src/utils/toolSchemaCache.ts`
- Agent prompt engineering:
  - `~/claude_code_src/src/tools/AgentTool/prompt.ts`
- Ask-user prompt behavior:
  - `~/claude_code_src/src/tools/AskUserQuestionTool/prompt.ts`
- Structured init/runtime metadata:
  - `~/claude_code_src/src/utils/messages/systemInit.ts`

Execution rule for every slice:

1. Implement one focused slice.
2. Add or update automated tests.
3. Run `make test`.
4. Run live sandbox validation against `localhost:8083`.
5. Isolate `HOME` when session, memory, provider, subagent, or worktree state matters.
6. Commit after both automated and sandbox tests pass.

## Phase A: Prompt Engine Foundation

### A1. Layered Effective System Prompt
Mirror:
- `~/claude_code_src/src/utils/systemPrompt.ts`

Implement:
1. default prompt layer
2. custom prompt layer
3. append prompt layer
4. agent-specific layer
5. override layer
6. deterministic precedence matching Claude Code's effective prompt logic

Goosecode files:
1. `src/prompt.c`
2. `src/prompt.h`
3. `src/agent.c`
4. `src/tools/agent_tool.c`

Commit name:
- `Introduce layered system prompt assembly`

Tests:
1. `test_prompt_override_precedence`
2. `test_prompt_agent_overrides_default`
3. `test_prompt_append_layer_is_last`
4. `test_prompt_default_layer_still_present`

Sandbox prompts:
1. normal chat turn
2. subagent turn
3. one-shot prompt with override/custom prompt if exposed

### A2. Named Prompt Sections
Mirror:
- `~/claude_code_src/src/constants/systemPromptSections.ts`

Implement:
1. named prompt sections
2. cached section type
3. uncached section type
4. ordered section resolution

Likely sections:
1. intro/system identity
2. task execution rules
3. action safety
4. environment
5. git context
6. instruction files
7. provider/model/runtime state
8. plan mode
9. optional append/custom sections

Goosecode files:
1. `src/prompt.c`
2. `src/prompt.h`
3. `src/prompt_sections.c`
4. `src/prompt_sections.h`

Commit name:
- `Add named system prompt sections`

Tests:
1. `test_prompt_sections_order`
2. `test_prompt_sections_skip_nulls`
3. `test_prompt_sections_cached_vs_uncached_flags`

Sandbox prompts:
1. repeated turns in same session
2. plan mode on/off
3. provider switch then prompt

### A3. Prompt Section Cache
Mirror:
- `~/claude_code_src/src/constants/systemPromptSections.ts`
- `~/claude_code_src/src/utils/toolSchemaCache.ts` for stability mindset

Implement:
1. memoize stable sections
2. invalidate on `/clear`
3. invalidate on `/compact`
4. invalidate on provider/model/base-url changes
5. invalidate on instruction-file changes if practical

Goosecode files:
1. `src/prompt_sections.c`
2. `src/agent.c`
3. `src/session.c`

Commit name:
- `Cache stable system prompt sections`

Tests:
1. `test_prompt_section_cache_hits`
2. `test_prompt_section_cache_clears_on_compact`
3. `test_prompt_section_cache_clears_on_provider_change`

Sandbox prompts:
1. repeated identical turns
2. `/clear`
3. `/compact`
4. `/provider set ollama`

### A4. Real Dynamic Boundary Semantics
Mirror:
- `~/claude_code_src/src/constants/prompts.ts`

Implement:
1. explicit static prompt prefix builder
2. explicit dynamic suffix builder
3. preserve stable boundary across turns

Goosecode files:
1. `src/prompt.c`
2. `src/prompt.h`

Commit name:
- `Split static and dynamic system prompt regions`

Tests:
1. `test_prompt_static_prefix_stability`
2. `test_prompt_dynamic_suffix_changes_without_prefix_churn`

Sandbox prompts:
1. compare no-change repeated turns
2. change plan mode
3. change provider/model

## Phase B: Compaction

### B1. Model-Driven Full Compact Prompt
Mirror:
- `~/claude_code_src/src/services/compact/prompt.ts`

Implement:
1. no-tools compact preamble
2. structured compact prompt
3. summary sections matching Claude Code's compact categories
4. call model to compact instead of current crude inline summary

Goosecode files:
1. `src/compact.c`
2. `src/compact.h`
3. `src/session.c`
4. `src/agent.c`

Commit name:
- `Replace stub compaction with model-driven compact prompt`

Tests:
1. `test_compact_prompt_includes_no_tools_rule`
2. `test_compact_prompt_sections_present`
3. `test_compact_fallback_when_model_fails`

Sandbox prompts:
1. force long conversation until compact triggers
2. verify no tool calls happen during compact
3. continue after compact

### B2. Compact Output Formatter
Mirror:
- `~/claude_code_src/src/services/compact/prompt.ts` function `formatCompactSummary`

Implement:
1. strip scratch analysis if present
2. normalize `<summary>` section if used
3. emit clean continuation summary

Goosecode files:
1. `src/compact.c`

Commit name:
- `Format compact summaries for continuation`

Tests:
1. `test_compact_formatter_strips_analysis`
2. `test_compact_formatter_extracts_summary`

Sandbox prompts:
1. long session then inspect resumed continuation behavior

### B3. Partial Compact Variants
Mirror:
- `~/claude_code_src/src/services/compact/prompt.ts` partial modes

Implement:
1. full compact
2. partial compact for older prefix
3. optional up-to compact

Goosecode files:
1. `src/compact.c`
2. `src/session.c`
3. `src/commands/cmd_compact.c`

Commit name:
- `Add partial compaction modes`

Tests:
1. `test_partial_compact_prompt_scope`
2. `test_partial_compact_preserves_recent_messages`

Sandbox prompts:
1. mixed long session with recent working context preserved

## Phase C: Session Memory

### C1. Structured Session Memory File
Mirror:
- `~/claude_code_src/src/services/SessionMemory/prompts.ts`

Implement memory file with sections:
1. Session Title
2. Current State
3. Task Specification
4. Files and Functions
5. Workflow
6. Errors and Corrections
7. Codebase and System Documentation
8. Learnings
9. Key Results
10. Worklog

Goosecode files:
1. `src/session_memory.c`
2. `src/session_memory.h`
3. `src/session.c`
4. `src/config.c`

Commit name:
- `Add structured session memory artifact`

Tests:
1. `test_session_memory_file_created`
2. `test_session_memory_template_loads`
3. `test_session_memory_resume_loads_existing_file`

Sandbox prompts:
1. fresh session
2. resume same session
3. verify file exists in sandbox home

### C2. Session Memory Update Prompt
Mirror:
- `~/claude_code_src/src/services/SessionMemory/prompts.ts` function `buildSessionMemoryUpdatePrompt`

Implement:
1. memory update prompt
2. current notes injected
3. section reminders for oversized content
4. current-state emphasis

Goosecode files:
1. `src/session_memory.c`
2. `src/agent.c`

Commit name:
- `Add session memory update prompt`

Tests:
1. `test_session_memory_prompt_substitutes_notes`
2. `test_session_memory_prompt_adds_section_reminders`

Sandbox prompts:
1. multi-turn task
2. inspect memory file content after updates

### C3. Memory Truncation for Compact
Mirror:
- `~/claude_code_src/src/services/SessionMemory/prompts.ts` function `truncateSessionMemoryForCompact`

Implement:
1. per-section truncation
2. total memory budget
3. preserve important sections

Goosecode files:
1. `src/session_memory.c`

Commit name:
- `Truncate session memory for compact budgets`

Tests:
1. `test_session_memory_section_truncation`
2. `test_session_memory_total_budget`

Sandbox prompts:
1. intentionally oversized memory content then compact

## Phase D: Tool Result Budget Management

### D1. Persist Oversized Tool Results
Mirror:
- `~/claude_code_src/src/utils/toolResultStorage.ts`

Implement:
1. persist oversized results to disk
2. replace prompt-visible content with stable preview
3. track by tool-use ID
4. reconstruct on resume

Goosecode files:
1. `src/tool_result_store.c`
2. `src/tool_result_store.h`
3. `src/session.c`
4. `src/agent.c`

Commit name:
- `Persist oversized tool results and replace with previews`

Tests:
1. `test_large_tool_result_persisted`
2. `test_large_tool_result_preview_inserted`
3. `test_tool_result_reconstructed_on_resume`

Sandbox prompts:
1. large `web_fetch`
2. large `bash`
3. large `read_file`
4. resume same session

### D2. Message-Level Tool Result Budget
Mirror:
- `~/claude_code_src/src/utils/toolResultStorage.ts` per-message budget logic

Implement:
1. budget by message
2. replace only what is needed
3. freeze seen decisions
4. reapply prior replacements deterministically

Goosecode files:
1. `src/tool_result_store.c`
2. `src/agent.c`

Commit name:
- `Enforce message-level tool result budgets`

Tests:
1. `test_tool_result_budget_selects_largest_fresh_results`
2. `test_seen_unreplaced_results_remain_frozen`

Sandbox prompts:
1. multiple large tool results in one turn
2. repeated follow-up turns

## Phase E: Tool Schema Cache and Deferred Tool Exposure

### E1. Tool Schema Cache
Mirror:
- `~/claude_code_src/src/utils/toolSchemaCache.ts`

Implement:
1. session-scoped schema cache
2. stable schema bytes across turns
3. explicit invalidation when tool surface changes

Goosecode files:
1. `src/tools/tools.c`
2. `src/agent.c`
3. cache helper module if needed

Commit name:
- `Cache rendered tool schemas per session`

Tests:
1. `test_tool_schema_cache_reuse`
2. `test_tool_schema_cache_invalidates_on_provider_change`
3. `test_tool_schema_cache_invalidates_on_mcp_change`

Sandbox prompts:
1. repeated turns
2. provider switch
3. MCP config change

### E2. Deferred Tool Search
Mirror:
- `~/claude_code_src/src/utils/toolSearch.ts`

Implement:
1. mark some tools deferred
2. threshold-based deferral when tool descriptions get too large
3. keep `tool_search` as discovery path
4. likely defer MCP-heavy and integration-heavy tool descriptions first

Goosecode files:
1. `src/tools/tools.c`
2. `src/tools/tool_search.c`
3. `src/agent.c`

Commit name:
- `Defer heavy tool descriptions behind tool search`

Tests:
1. `test_tool_search_threshold_defers_heavy_tools`
2. `test_deferred_tool_discovery_still_works`

Sandbox prompts:
1. large MCP configuration
2. ask model to discover deferred tools

## Phase F: Agent Prompt Engineering

### F1. Stronger Agent Prompt Structure
Mirror:
- `~/claude_code_src/src/tools/AgentTool/prompt.ts`

Implement:
1. explicit when-to-use guidance
2. stronger prompt-writing guidance
3. clearer per-agent behavior sections
4. examples tuned for subagent use

Goosecode files:
1. `src/tools/agent_tool.c`

Commit name:
- `Strengthen agent prompt engineering`

Tests:
1. `test_agent_prompt_contains_when_to_use_guidance`
2. `test_agent_prompt_differs_by_agent_type`

Sandbox prompts:
1. `explore`
2. `plan`
3. `general`

### F2. Fork-Like Inherited Context Mode
Mirror conceptually:
- `~/claude_code_src/src/tools/AgentTool/prompt.ts`

Implement:
1. fresh subagent mode
2. inherited-context “fork” mode
3. explicit runtime distinction

Goosecode files:
1. `src/tools/agent_tool.c`
2. `src/tools/subagent_store.c`

Commit name:
- `Add inherited-context subagent fork mode`

Tests:
1. `test_subagent_fork_inherits_context`
2. `test_fresh_subagent_starts_clean`

Sandbox prompts:
1. compare fork vs fresh on same task

## Phase G: Plan-Aware Ask-User Behavior

### G1. Plan-Specific Ask-User Prompt Rules
Mirror:
- `~/claude_code_src/src/tools/AskUserQuestionTool/prompt.ts`

Implement:
1. ask clarifying questions before finalizing plans
2. do not ask for invisible-plan approval
3. direct users to exit-plan flow for plan approval
4. avoid wording that references unseen plan text

Goosecode files:
1. `src/tools/tools.c`
2. `src/tools/ask_user_question.c`
3. `src/prompt.c`

Commit name:
- `Make ask-user prompt rules plan-mode aware`

Tests:
1. `test_ask_user_tool_prompt_mentions_plan_constraints`
2. `test_plan_mode_prompt_approval_guidance_present`

Sandbox prompts:
1. ambiguous planning task
2. verify model chooses clarification before final plan

## Phase H: System Reminder and Structured Init Metadata

### H1. System Reminder Semantics
Mirror:
- `~/claude_code_src/src/constants/prompts.ts`

Implement:
1. explicit internal reminder semantics
2. prompt instruction explaining reminder meaning
3. use for compaction and runtime metadata markers where useful

Goosecode files:
1. `src/prompt.c`
2. `src/session.c`
3. `src/agent.c`

Commit name:
- `Add system reminder prompt semantics`

Tests:
1. `test_prompt_explains_system_reminders`

Sandbox prompts:
1. compact then continue
2. confirm reminder-bearing session still behaves

### H2. Structured Runtime Init Metadata
Mirror:
- `~/claude_code_src/src/utils/messages/systemInit.ts`

Implement:
1. internal structured init state object with:
   - cwd
   - provider
   - model
   - permission mode
   - tools
   - commands
   - subagent types
   - MCP servers
2. expose it for future UI or SDK use

Goosecode files:
1. `src/system_init.c`
2. `src/system_init.h`
3. `src/agent.c`

Commit name:
- `Add structured runtime init metadata`

Tests:
1. `test_system_init_metadata_contains_runtime_state`

Sandbox prompts:
1. launch and inspect state via command or debug path

## Phase I: Output Style and Language Preference

### I1. Output Style Section
Mirror:
- `~/claude_code_src/src/constants/prompts.ts`

Implement:
1. optional output-style config
2. prompt section for response style

Goosecode files:
1. `src/prompt.c`
2. `src/config.c`
3. optional config command support

Commit name:
- `Add output style prompt section`

Tests:
1. `test_output_style_prompt_section_present_when_set`

Sandbox prompts:
1. concise style
2. detailed style

### I2. Language Preference Section
Mirror:
- `~/claude_code_src/src/constants/prompts.ts`

Implement:
1. response language setting
2. prompt section enforcing language

Goosecode files:
1. `src/prompt.c`
2. `src/config.c`
3. `src/commands/cmd_config.c`

Commit name:
- `Add language preference prompt section`

Tests:
1. `test_language_section_present_when_configured`

Sandbox prompts:
1. non-English response preference

## Phase J: Evaluation and Hardening

### J1. Prompt Regression Suite
Implement:
1. prompt-assembly regression tests
2. compact prompt tests
3. memory prompt tests
4. schema-cache tests
5. tool-result-budget tests

Commit name:
- `Add prompt management regression suite`

### J2. Sandbox Evaluation Matrix
Run and fix until clean:
1. long coding session with compact
2. resume after compact
3. provider switch mid-session
4. large tool outputs
5. MCP-heavy session
6. subagent-heavy session
7. plan/ask-user session
8. interrupt during long stream and continue

Suggested sandboxes:
1. `/tmp/goosecode-sandboxes/phase-a-prompt`
2. `/tmp/goosecode-sandboxes/phase-b-compact`
3. `/tmp/goosecode-sandboxes/phase-c-memory`
4. `/tmp/goosecode-sandboxes/phase-d-tool-results`
5. `/tmp/goosecode-sandboxes/phase-e-tool-search`
6. `/tmp/goosecode-sandboxes/phase-f-agents`
7. `/tmp/goosecode-sandboxes/phase-g-plan-ask`
8. `/tmp/goosecode-sandboxes/phase-j-eval`

## Recommended Implementation Order

1. Phase A
2. Phase B
3. Phase C
4. Phase D
5. Phase E
6. Phase F
7. Phase G
8. Phase H
9. Phase I
10. Phase J

## Highest-Value Missing Pieces First

If the goal is to close the biggest Claude Code gap earliest, prioritize:

1. real compaction prompt pipeline
2. session memory
3. tool-result persistence/replacement
4. tool schema cache
5. deferred tool search

These are the biggest differences between goosecode's current prompt system and Claude Code's prompt-management stack.
