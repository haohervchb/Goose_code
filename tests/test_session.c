#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <ctype.h>

// Minimal test for session ID format and summary functions
// We'll test the logic directly without full session dependencies

#define SUMMARY_MAX_LEN 60

// Copy of the generate_random_suffix function
static void generate_random_suffix(char *buf, size_t len) {
    static const char chars[] = "abcdef0123456789";
    for (size_t i = 0; i < len - 1; i++) {
        buf[i] = chars[rand() % (sizeof(chars) - 1)];
    }
    buf[len - 1] = '\0';
}

// Copy of the session ID generation logic
void generate_session_id(char *id, size_t len) {
    char ts[64];
    struct timespec ts_now;
    struct tm *tm_info;
    clock_gettime(CLOCK_REALTIME, &ts_now);
    tm_info = localtime(&ts_now.tv_sec);
    snprintf(ts, sizeof(ts), "%04d%02d%02d_%02d%02d%02d",
             tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
             tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
    
    char suffix[8];
    generate_random_suffix(suffix, sizeof(suffix));
    
    snprintf(id, len, "%s_%s", ts, suffix);
}

// Copy of the summary truncation logic
char* truncate_summary(const char *summary) {
    if (!summary || !summary[0]) return NULL;
    
    size_t len = strlen(summary);
    if (len > SUMMARY_MAX_LEN) {
        char *truncated = malloc(SUMMARY_MAX_LEN + 1);
        strncpy(truncated, summary, SUMMARY_MAX_LEN - 3);
        truncated[SUMMARY_MAX_LEN - 3] = '\0';
        strcat(truncated, "...");
        return truncated;
    }
    return strdup(summary);
}

// Copy of datetime parsing logic
int parse_session_id_datetime(const char *id, struct tm *tm_out) {
    if (!id || !tm_out) return -1;
    
    // Try new format: YYYYMMDD_HHMMSS_XXXXXX
    if (strlen(id) >= 15 && id[8] == '_') {
        int y, m, d, h, min, s;
        if (sscanf(id, "%4d%2d%2d_%2d%2d%2d", &y, &m, &d, &h, &min, &s) == 6) {
            tm_out->tm_year = y - 1900;
            tm_out->tm_mon = m - 1;
            tm_out->tm_mday = d;
            tm_out->tm_hour = h;
            tm_out->tm_min = min;
            tm_out->tm_sec = s;
            return 0;
        }
    }
    
    // Try old format: timestamp_nanoseconds
    if (strlen(id) > 10) {
        long ts = strtol(id, NULL, 10);
        if (ts > 0) {
            time_t t = ts;
            struct tm *tm = localtime(&t);
            if (tm) {
                *tm_out = *tm;
                return 0;
            }
        }
    }
    
    return -1;
}

int main() {
    int passed = 0;
    int failed = 0;
    
    printf("=== Test 1: Session ID Format ===\n");
    char id1[64], id2[64], id3[64];
    generate_session_id(id1, sizeof(id1));
    generate_session_id(id2, sizeof(id2));
    generate_session_id(id3, sizeof(id3));
    
    printf("Generated IDs:\n");
    printf("  1: %s\n", id1);
    printf("  2: %s\n", id2);
    printf("  3: %s\n", id3);
    
    // Check format: YYYYMMDD_HHMMSS_xxxxxx (23 chars)
    // Format: 20260408_143339_103db94
    //         0---------8---------16--------23
    int format_ok = 1;
    if (strlen(id1) != 23) {
        printf("  Expected length 23, got %zu: %s\n", strlen(id1), id1);
        format_ok = 0;
    }
    if (id1[8] != '_' || id1[15] != '_') {
        printf("  Underscore positions wrong: pos8='%c' pos15='%c'\n", id1[8], id1[15]);
        format_ok = 0;
    }
    for (int i = 0; i < 8; i++) if (!isdigit(id1[i])) { printf("  char %d not digit: %c\n", i, id1[i]); format_ok = 0; }
    for (int i = 9; i < 15; i++) if (!isdigit(id1[i])) { printf("  char %d not digit: %c\n", i, id1[i]); format_ok = 0; }
    for (int i = 16; i < 23; i++) if (!isxdigit(id1[i])) { printf("  char %d not hex: %c\n", i, id1[i]); format_ok = 0; }
    
    if (format_ok) {
        printf("✓ ID format is correct\n");
        passed++;
    } else {
        printf("✗ ID format is incorrect\n");
        failed++;
    }
    
    // Check uniqueness
    if (strcmp(id1, id2) != 0 && strcmp(id2, id3) != 0 && strcmp(id1, id3) != 0) {
        printf("✓ IDs are unique\n");
        passed++;
    } else {
        printf("✗ IDs are not unique\n");
        failed++;
    }
    
    printf("\n=== Test 2: Datetime Parsing ===\n");
    struct tm tm;
    if (parse_session_id_datetime(id1, &tm) == 0) {
        char buf[64];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
        printf("Parsed datetime: %s\n", buf);
        printf("✓ Datetime parsing works for new format\n");
        passed++;
    } else {
        printf("✗ Failed to parse datetime\n");
        failed++;
    }
    
    // Test old format
    if (parse_session_id_datetime("1744060800_123456789", &tm) == 0) {
        char buf[64];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
        printf("Old format parsed: %s\n", buf);
        printf("✓ Datetime parsing works for old format\n");
        passed++;
    } else {
        printf("✗ Failed to parse old format\n");
        failed++;
    }
    
    printf("\n=== Test 3: Summary Truncation ===\n");
    char *short_summary = truncate_summary("Short summary");
    if (short_summary && strcmp(short_summary, "Short summary") == 0) {
        printf("✓ Short summary preserved: %s\n", short_summary);
        passed++;
        free(short_summary);
    } else {
        printf("✗ Short summary not preserved\n");
        failed++;
    }
    
    char *long_summary = truncate_summary("This is a very long summary that should be truncated because it exceeds the maximum length of 60 characters");
    if (long_summary && strlen(long_summary) <= 60) {
        printf("✓ Long summary truncated: %s (len=%zu)\n", long_summary, strlen(long_summary));
        passed++;
        free(long_summary);
    } else {
        printf("✗ Long summary not truncated properly\n");
        if (long_summary) free(long_summary);
        failed++;
    }
    
    printf("\n=== Test 4: NULL/Empty Summary ===\n");
    char *null_summary = truncate_summary(NULL);
    if (null_summary == NULL) {
        printf("✓ NULL summary handled correctly\n");
        passed++;
    } else {
        printf("✗ NULL summary not handled\n");
        free(null_summary);
        failed++;
    }
    
    char *empty_summary = truncate_summary("");
    if (empty_summary == NULL) {
        printf("✓ Empty summary handled correctly\n");
        passed++;
    } else {
        printf("✗ Empty summary not handled\n");
        free(empty_summary);
        failed++;
    }
    
    printf("\n=== Summary ===\n");
    printf("Passed: %d\n", passed);
    printf("Failed: %d\n", failed);
    
    return failed > 0 ? 1 : 0;
}