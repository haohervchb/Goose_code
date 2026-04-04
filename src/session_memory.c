#include "session_memory.h"
#include "util/json_util.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SESSION_MEMORY_MAX_SECTION_CHARS 8000
#define SESSION_MEMORY_COMPACT_SECTION_CHARS 2000
#define SESSION_MEMORY_COMPACT_TOTAL_CHARS 12000

static char *extract_last_role_content(const Session *sess, const char *role) {
    if (!sess || !sess->messages) return strdup("");
    int total = cJSON_GetArraySize(sess->messages);
    for (int i = total - 1; i >= 0; i--) {
        cJSON *msg = cJSON_GetArrayItem(sess->messages, i);
        const char *msg_role = json_get_string(msg, "role");
        const char *content = json_get_string(msg, "content");
        if (msg_role && content && strcmp(msg_role, role) == 0) return strdup(content);
    }
    return strdup("");
}

static char *apply_section_update(const char *notes, const char *section_header, const char *replacement) {
    const char *section_pos = strstr(notes, section_header);
    if (!section_pos) return strdup(notes);

    const char *after_header = section_pos + strlen(section_header);
    const char *desc_end = strchr(after_header, '\n');
    if (!desc_end) return strdup(notes);
    desc_end++;
    const char *next_section = strstr(desc_end, "\n# ");
    const char *section_end = next_section ? next_section + 1 : notes + strlen(notes);

    StrBuf out = strbuf_new();
    strbuf_append_len(&out, notes, (size_t)(section_pos - notes));
    strbuf_append(&out, section_header);
    strbuf_append_len(&out, after_header, (size_t)(desc_end - after_header));
    if (replacement && replacement[0]) {
        strbuf_append_char(&out, '\n');
        strbuf_append(&out, replacement);
        if (out.len > 0 && out.data[out.len - 1] != '\n') strbuf_append_char(&out, '\n');
        strbuf_append_char(&out, '\n');
    }
    strbuf_append(&out, section_end);
    return strbuf_detach(&out);
}

static int session_memory_fallback_update(const GooseConfig *cfg, const Session *sess, const char *current_notes) {
    char *notes_path = session_memory_path(cfg, sess);
    char *last_user = extract_last_role_content(sess, "user");
    char *last_assistant = extract_last_role_content(sess, "assistant");
    (void)current_notes;

    const char *state_text = (last_assistant && last_assistant[0]) ? last_assistant : last_user;
    const char *result_text = (last_assistant && last_assistant[0]) ? last_assistant : last_user;

    char *template_notes = session_memory_default_template();
    char *updated = apply_section_update(template_notes, "# Current State\n", state_text);
    free(template_notes);
    char *tmp = apply_section_update(updated, "# Task Specification\n", last_user);
    free(updated);
    updated = tmp;
    tmp = apply_section_update(updated, "# Key Results\n", result_text);
    free(updated);
    updated = tmp;
    tmp = apply_section_update(updated, "# Worklog\n", last_user);
    free(updated);
    updated = tmp;

    FILE *f = fopen(notes_path, "w");
    free(notes_path);
    free(last_user);
    free(last_assistant);
    if (!f) {
        free(updated);
        return -1;
    }
    fputs(updated, f);
    fclose(f);
    free(updated);
    return 0;
}

static int session_memory_output_looks_valid(const char *text) {
    if (!text) return 0;
    while (*text == ' ' || *text == '\n' || *text == '\r' || *text == '\t') text++;
    if (strncmp(text, "# Session Title", 15) != 0) return 0;
    if (!strstr(text, "# Current State")) return 0;
    if (!strstr(text, "# Worklog")) return 0;
    return 1;
}

static char *substitute_var(const char *template_text, const char *needle, const char *replacement) {
    StrBuf out = strbuf_new();
    const char *scan = template_text;
    size_t needle_len = strlen(needle);

    while (1) {
        const char *pos = strstr(scan, needle);
        if (!pos) {
            strbuf_append(&out, scan);
            break;
        }
        strbuf_append_len(&out, scan, (size_t)(pos - scan));
        strbuf_append(&out, replacement ? replacement : "");
        scan = pos + needle_len;
    }

    return strbuf_detach(&out);
}

char *session_memory_default_template(void) {
    return strdup(
        "# Session Title\n"
        "_A short and distinctive 5-10 word descriptive title for the session._\n\n"
        "# Current State\n"
        "_What is actively being worked on right now? Pending tasks and immediate next steps._\n\n"
        "# Task Specification\n"
        "_What did the user ask to build? Include design decisions and explanatory context._\n\n"
        "# Files and Functions\n"
        "_What are the important files and why are they relevant?_\n\n"
        "# Workflow\n"
        "_What commands are usually run and in what order?_\n\n"
        "# Errors and Corrections\n"
        "_Errors encountered, how they were fixed, and what approaches failed._\n\n"
        "# Codebase and System Documentation\n"
        "_Important components and how they fit together._\n\n"
        "# Learnings\n"
        "_What worked well? What should be avoided?_\n\n"
        "# Key Results\n"
        "_Repeat any exact answer, table, or deliverable the user asked for._\n\n"
        "# Worklog\n"
        "_Step-by-step terse log of what was attempted and done._\n");
}

char *session_memory_default_update_prompt(void) {
    return strdup(
        "IMPORTANT: This message and these instructions are NOT part of the actual user conversation. Do NOT include references to note-taking or these instructions in the notes content.\n\n"
        "Based on the user conversation above, update the session notes file.\n\n"
        "The file {{notesPath}} has already been read for you. Here are its current contents:\n"
        "<current_notes_content>\n"
        "{{currentNotes}}\n"
        "</current_notes_content>\n\n"
        "Your only task is to output the full updated notes file as markdown. Do not call tools.\n\n"
        "CRITICAL RULES:\n"
        "- Preserve all section headers exactly\n"
        "- Preserve the italic template description lines exactly\n"
        "- Only change the content below those description lines\n"
        "- Do not add new sections\n"
        "- Keep sections concise and info-dense\n"
        "- Always update Current State to reflect the most recent work\n"
        "- Keep each section under roughly 2000 words/characters when possible\n");
}

char *session_memory_path(const GooseConfig *cfg, const Session *sess) {
    size_t len = strlen(cfg->session_memory_dir) + strlen(sess->id) + 8;
    char *path = malloc(len);
    snprintf(path, len, "%s/%s.md", cfg->session_memory_dir, sess->id);
    return path;
}

int session_memory_ensure(const GooseConfig *cfg, const Session *sess) {
    char *path = session_memory_path(cfg, sess);
    char *existing = json_read_file(path);
    if (existing) {
        free(existing);
        free(path);
        return 0;
    }

    char *tmpl = session_memory_default_template();
    FILE *f = fopen(path, "w");
    if (!f) {
        free(tmpl);
        free(path);
        return -1;
    }
    fputs(tmpl, f);
    fclose(f);
    free(tmpl);
    free(path);
    return 0;
}

char *session_memory_load(const GooseConfig *cfg, const Session *sess) {
    char *path = session_memory_path(cfg, sess);
    char *content = json_read_file(path);
    free(path);
    return content;
}

char *session_memory_build_update_prompt(const char *current_notes, const char *notes_path) {
    char *tmpl = session_memory_default_update_prompt();
    char *with_notes = substitute_var(tmpl, "{{currentNotes}}", current_notes ? current_notes : "");
    free(tmpl);
    char *with_path = substitute_var(with_notes, "{{notesPath}}", notes_path ? notes_path : "");
    free(with_notes);

    if (current_notes && strlen(current_notes) > SESSION_MEMORY_MAX_SECTION_CHARS) {
        StrBuf out = strbuf_from(with_path);
        free(with_path);
        strbuf_append(&out, "\n\nIMPORTANT: The notes file is large. Condense verbose sections while preserving Current State, Errors and Corrections, Files and Functions, and Worklog.\n");
        return strbuf_detach(&out);
    }
    return with_path;
}

int session_memory_update(const GooseConfig *cfg, const Session *sess, const ApiConfig *api_cfg) {
    char *notes_path = session_memory_path(cfg, sess);
    char *current_notes = session_memory_load(cfg, sess);
    if (!current_notes) current_notes = session_memory_default_template();

    char *update_prompt = session_memory_build_update_prompt(current_notes, notes_path);
    char *conversation_text = json_to_string(sess->messages);
    if (!conversation_text) {
        free(notes_path);
        free(current_notes);
        free(update_prompt);
        return -1;
    }

    cJSON *req_messages = cJSON_CreateArray();
    cJSON_AddItemToArray(req_messages, json_build_message("system", update_prompt));
    cJSON_AddItemToArray(req_messages, json_build_message("user", conversation_text));
    free(conversation_text);
    free(update_prompt);

    ApiResponse resp = api_send_message(api_cfg, req_messages, NULL);
    cJSON_Delete(req_messages);
    if (resp.status != API_OK || !session_memory_output_looks_valid(resp.text_content.data)) {
        api_response_free(&resp);
        int rc = session_memory_fallback_update(cfg, sess, current_notes);
        free(notes_path);
        free(current_notes);
        return rc;
    }

    FILE *f = fopen(notes_path, "w");
    if (!f) {
        api_response_free(&resp);
        free(notes_path);
        free(current_notes);
        return -1;
    }
    fputs(resp.text_content.data, f);
    fclose(f);

    api_response_free(&resp);
    free(notes_path);
    free(current_notes);
    return 0;
}

SessionMemoryTruncateResult session_memory_truncate_for_compact(const char *content) {
    SessionMemoryTruncateResult result = {0};
    if (!content) {
        result.truncated_content = strdup("");
        return result;
    }

    StrBuf out = strbuf_new();
    const char *cursor = content;

    while (*cursor) {
        const char *section = strstr(cursor, "# ");
        if (!section || section != cursor) {
            const char *next = strstr(cursor, "\n# ");
            if (!next) {
                strbuf_append(&out, cursor);
                break;
            }
            strbuf_append_len(&out, cursor, (size_t)(next + 1 - cursor));
            cursor = next + 1;
            continue;
        }

        const char *next = strstr(section + 2, "\n# ");
        const char *section_end = next ? next + 1 : content + strlen(content);
        size_t section_len = (size_t)(section_end - section);

        if (section_len <= SESSION_MEMORY_COMPACT_SECTION_CHARS) {
            strbuf_append_len(&out, section, section_len);
        } else {
            result.was_truncated = 1;
            strbuf_append_len(&out, section, SESSION_MEMORY_COMPACT_SECTION_CHARS);
            if (out.len > 0 && out.data[out.len - 1] != '\n') strbuf_append_char(&out, '\n');
            strbuf_append(&out, "[... section truncated for length ...]\n");
        }

        cursor = section_end;
    }

    if (out.len > SESSION_MEMORY_COMPACT_TOTAL_CHARS) {
        result.was_truncated = 1;
        out.data[SESSION_MEMORY_COMPACT_TOTAL_CHARS] = '\0';
        out.len = SESSION_MEMORY_COMPACT_TOTAL_CHARS;
        strbuf_append(&out, "\n[... total memory budget reached ...]");
        result.truncated_content = strbuf_detach(&out);
    } else {
        result.truncated_content = strbuf_detach(&out);
    }

    return result;
}

void session_memory_truncate_result_free(SessionMemoryTruncateResult *result) {
    if (!result) return;
    free(result->truncated_content);
    result->truncated_content = NULL;
    result->was_truncated = 0;
}

char *session_memory_truncate_for_display(const char *content) {
    if (!content) return strdup("");
    
    int line_count = 0;
    size_t byte_count = 0;
    const char *last_newline = NULL;
    const char *p = content;
    
    while (*p && byte_count < SESSION_MEMORY_MAX_BYTES) {
        if (*p == '\n') {
            line_count++;
            last_newline = p;
        }
        byte_count++;
        p++;
        
        if (line_count >= SESSION_MEMORY_MAX_LINES && last_newline) {
            break;
        }
    }
    
    int was_truncated = (*p != '\0' || line_count >= SESSION_MEMORY_MAX_LINES);
    
    size_t result_len = (size_t)(last_newline ? last_newline - content : byte_count);
    if (result_len > SESSION_MEMORY_MAX_BYTES) {
        result_len = SESSION_MEMORY_MAX_BYTES;
    }
    
    char *result = malloc(result_len + 256);
    memcpy(result, content, result_len);
    
    if (was_truncated) {
        snprintf(result + result_len, 256,
            "\n\n> WARNING: Session memory truncated. Only %d lines of %d shown. "
            "Full memory saved to session file.",
            line_count, SESSION_MEMORY_MAX_LINES);
    } else {
        result[result_len] = '\0';
    }
    
    return result;
}
