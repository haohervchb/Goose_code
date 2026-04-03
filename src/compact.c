#include "compact.h"
#include "util/json_util.h"
#include "util/strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *NO_TOOLS_PREAMBLE =
    "CRITICAL: Respond with TEXT ONLY. Do NOT call any tools.\n\n"
    "- Do NOT use Read, Bash, Grep, Glob, Edit, Write, or ANY other tool.\n"
    "- You already have all the context you need in the conversation above.\n"
    "- Tool calls will be rejected and will waste your only turn.\n"
    "- Your entire response must be plain text: an <analysis> block followed by a <summary> block.\n\n";

static const char *COMPACT_TEMPLATE =
    "Your task is to create a detailed summary of the conversation so far, paying close attention to the user's explicit requests and your previous actions.\n\n"
    "Before providing your final summary, wrap your analysis in <analysis> tags. In your analysis process:\n"
    "1. Chronologically analyze each message and section of the conversation.\n"
    "2. Identify explicit user requests, key technical concepts, files touched, errors, decisions, pending tasks, and current work.\n"
    "3. Pay special attention to user feedback or corrections.\n\n"
    "Your summary should include these sections:\n"
    "1. Primary Request and Intent\n"
    "2. Key Technical Concepts\n"
    "3. Files and Code Sections\n"
    "4. Errors and Fixes\n"
    "5. Problem Solving\n"
    "6. All User Messages\n"
    "7. Pending Tasks\n"
    "8. Current Work\n"
    "9. Optional Next Step\n\n"
    "Respond using:\n<analysis>...</analysis>\n<summary>...</summary>\n";

static const char *PARTIAL_COMPACT_TEMPLATE =
    "Your task is to create a detailed summary of the RECENT portion of the conversation shown above. Earlier retained context will remain intact and does NOT need to be summarized. Focus only on the messages provided here.\n\n"
    "Before providing your final summary, wrap your analysis in <analysis> tags. In your analysis process:\n"
    "1. Analyze the recent messages chronologically.\n"
    "2. Identify explicit user requests, key technical concepts, files touched, errors, pending tasks, and current work.\n"
    "3. Pay special attention to user feedback or corrections.\n\n"
    "Your summary should include these sections:\n"
    "1. Primary Request and Intent\n"
    "2. Key Technical Concepts\n"
    "3. Files and Code Sections\n"
    "4. Errors and Fixes\n"
    "5. Problem Solving\n"
    "6. All User Messages\n"
    "7. Pending Tasks\n"
    "8. Current Work\n"
    "9. Optional Next Step\n\n"
    "Respond using:\n<analysis>...</analysis>\n<summary>...</summary>\n";

static const char *PARTIAL_COMPACT_UP_TO_TEMPLATE =
    "Your task is to create a detailed summary of this earlier portion of the conversation. This summary will be placed before newer preserved messages, so someone reading the summary and then those newer messages can continue the work accurately.\n\n"
    "Before providing your final summary, wrap your analysis in <analysis> tags. In your analysis process:\n"
    "1. Analyze the conversation portion shown above chronologically.\n"
    "2. Identify explicit user requests, key technical concepts, files touched, errors, work completed, pending tasks, and context needed for continuing work.\n"
    "3. Pay special attention to user feedback or corrections.\n\n"
    "Your summary should include these sections:\n"
    "1. Primary Request and Intent\n"
    "2. Key Technical Concepts\n"
    "3. Files and Code Sections\n"
    "4. Errors and Fixes\n"
    "5. Problem Solving\n"
    "6. All User Messages\n"
    "7. Pending Tasks\n"
    "8. Work Completed\n"
    "9. Context for Continuing Work\n\n"
    "Respond using:\n<analysis>...</analysis>\n<summary>...</summary>\n";

static const char *NO_TOOLS_TRAILER =
    "\n\nREMINDER: Do NOT call any tools. Respond with plain text only - an <analysis> block followed by a <summary> block.";

char *compact_get_prompt(void) {
    StrBuf out = strbuf_new();
    strbuf_append(&out, NO_TOOLS_PREAMBLE);
    strbuf_append(&out, COMPACT_TEMPLATE);
    strbuf_append(&out, NO_TOOLS_TRAILER);
    return strbuf_detach(&out);
}

char *compact_get_partial_prompt(CompactPartialDirection direction) {
    StrBuf out = strbuf_new();
    strbuf_append(&out, NO_TOOLS_PREAMBLE);
    strbuf_append(&out, direction == COMPACT_PARTIAL_UP_TO ? PARTIAL_COMPACT_UP_TO_TEMPLATE : PARTIAL_COMPACT_TEMPLATE);
    strbuf_append(&out, NO_TOOLS_TRAILER);
    return strbuf_detach(&out);
}

char *compact_format_summary(const char *summary) {
    if (!summary) return strdup("");

    const char *start = summary;
    const char *analysis_open = strstr(summary, "<analysis>");
    const char *analysis_close = strstr(summary, "</analysis>");
    if (analysis_open && analysis_close && analysis_close > analysis_open) {
        start = analysis_close + strlen("</analysis>");
    }

    const char *summary_open = strstr(start, "<summary>");
    const char *summary_close = strstr(start, "</summary>");
    if (summary_open && summary_close && summary_close > summary_open) {
        summary_open += strlen("<summary>");
        StrBuf out = strbuf_from("Summary:\n");
        strbuf_append_len(&out, summary_open, (size_t)(summary_close - summary_open));
        return strbuf_detach(&out);
    }

    return strdup(start);
}

char *compact_build_user_summary_message(const char *summary, int recent_messages_preserved) {
    StrBuf out = strbuf_from("<system-reminder>\n");
    strbuf_append(&out, "This session is being continued from a previous conversation that ran out of context. The summary below covers the earlier portion of the conversation.\n\n");
    char *formatted = compact_format_summary(summary ? summary : "");
    strbuf_append(&out, formatted);
    free(formatted);
    if (recent_messages_preserved) {
        strbuf_append(&out, "\n\nRecent messages are preserved verbatim.");
    }
    strbuf_append(&out, "\n</system-reminder>");
    return strbuf_detach(&out);
}

char *compact_generate_summary(const ApiConfig *cfg, const cJSON *messages) {
    char *prompt = compact_get_prompt();
    cJSON *system_msg = json_build_message("system", prompt);
    free(prompt);

    cJSON *conversation_json = cJSON_Duplicate((cJSON *)messages, 1);
    char *conversation_text = json_to_string(conversation_json);
    cJSON_Delete(conversation_json);
    if (!conversation_text) {
        cJSON_Delete(system_msg);
        return NULL;
    }

    cJSON *user_msg = json_build_message("user", conversation_text);
    free(conversation_text);
    cJSON *req_messages = cJSON_CreateArray();
    cJSON_AddItemToArray(req_messages, system_msg);
    cJSON_AddItemToArray(req_messages, user_msg);

    ApiResponse resp = api_send_message(cfg, req_messages, NULL);
    cJSON_Delete(req_messages);
    if (resp.status != API_OK) {
        api_response_free(&resp);
        return NULL;
    }

    char *formatted = compact_format_summary(resp.text_content.data);
    api_response_free(&resp);
    return formatted;
}

char *compact_generate_partial_summary(const ApiConfig *cfg, const cJSON *messages,
                                      CompactPartialDirection direction) {
    char *prompt = compact_get_partial_prompt(direction);
    cJSON *system_msg = json_build_message("system", prompt);
    free(prompt);

    cJSON *conversation_json = cJSON_Duplicate((cJSON *)messages, 1);
    char *conversation_text = json_to_string(conversation_json);
    cJSON_Delete(conversation_json);
    if (!conversation_text) {
        cJSON_Delete(system_msg);
        return NULL;
    }

    cJSON *user_msg = json_build_message("user", conversation_text);
    free(conversation_text);
    cJSON *req_messages = cJSON_CreateArray();
    cJSON_AddItemToArray(req_messages, system_msg);
    cJSON_AddItemToArray(req_messages, user_msg);

    ApiResponse resp = api_send_message(cfg, req_messages, NULL);
    cJSON_Delete(req_messages);
    if (resp.status != API_OK) {
        api_response_free(&resp);
        return NULL;
    }

    char *formatted = compact_format_summary(resp.text_content.data);
    api_response_free(&resp);
    return formatted;
}

char *compact_summarize(const char *messages_json, int keep_recent) {
    (void)keep_recent;
    StrBuf summary = strbuf_from("[Previous conversation summarized: ");
    const char *scan = messages_json ? messages_json : "";

    for (int seen = 0; seen < 6; seen++) {
        const char *user = strstr(scan, "\"role\":\"user\"");
        const char *assistant = strstr(scan, "\"role\":\"assistant\"");
        const char *next = NULL;
        const char *label = NULL;
        if (user && (!assistant || user < assistant)) {
            next = strstr(user, "\"content\":\"");
            label = "User";
        } else if (assistant) {
            next = strstr(assistant, "\"content\":\"");
            label = "Assistant";
        }
        if (!next || !label) break;
        next += strlen("\"content\":\"");
        const char *end = strchr(next, '"');
        if (!end) break;
        strbuf_append_fmt(&summary, "%s: %.80s... ", label, next);
        scan = end + 1;
    }

    strbuf_append(&summary, "]");
    return strbuf_detach(&summary);
}
