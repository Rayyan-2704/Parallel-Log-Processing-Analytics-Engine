#include "logengine.h"
#include <string.h>

/* Return current wall-clock time in seconds */
double util_now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/*
 * Parse the severity level from a log line.
 *
 * Fix applied: the old implementation used strstr() across the entire
 * uppercased line, so a line like "Recovered from previous ERROR state"
 * would be tagged ERROR even if it was actually an INFO entry. The fix:
 *
 *   Step 1. Look for a bracketed tag [LEVEL] (e.g. [ERROR], [WARNING]).
 *           Only scan the text inside the brackets. This matches the most
 *           common syslog / application log formats.
 *   Step 2. If no bracketed tag is found, fall back to a bare-word scan of
 *           the full line (original behaviour, kept for unstructured logs).
 *
 * The keyword table is checked in priority order (CRITICAL before ERROR, etc.)
 * to ensure the highest severity wins when multiple keywords are present.
 */
Severity util_parse_severity(const char *line)
{
    static const struct { const char *kw; Severity sev; } table[] = {
        { "CRITICAL", SEV_CRITICAL },
        { "CRIT",     SEV_CRITICAL },
        { "ERROR",    SEV_ERROR    },
        { "ERR",      SEV_ERROR    },
        { "WARNING",  SEV_WARNING  },
        { "WARN",     SEV_WARNING  },
        { "INFO",     SEV_INFO     },
        { "DEBUG",    SEV_DEBUG    },
        { "DBG",      SEV_DEBUG    },
    };
    const size_t NTABLE = sizeof(table)/sizeof(table[0]);

    /* Uppercase the whole line once */
    char upper[MAX_LINE_LEN];
    size_t len = strlen(line);
    if (len >= MAX_LINE_LEN) len = MAX_LINE_LEN - 1;
    for (size_t i = 0; i < len; i++)
        upper[i] = (char)toupper((unsigned char)line[i]);
    upper[len] = '\0';

    /* ── Step 1: bracketed tag [LEVEL] ─────────────────── */
    const char *bracket = strchr(upper, '[');
    while (bracket) {
        const char *close = strchr(bracket + 1, ']');
        if (!close) break;

        /* Extract text inside the brackets */
        size_t tag_len = (size_t)(close - bracket - 1);
        char tag[64];
        if (tag_len >= sizeof(tag)) tag_len = sizeof(tag) - 1;
        memcpy(tag, bracket + 1, tag_len);
        tag[tag_len] = '\0';

        for (size_t t = 0; t < NTABLE; t++) {
            /* Exact match only inside brackets, not substring,
             * to avoid [RECOVERY_ERROR] matching CRIT. */
            if (strcmp(tag, table[t].kw) == 0) {
                return table[t].sev;
            }
        }

        /* Advance past this bracket pair and try the next one */
        bracket = strchr(close + 1, '[');
    }

    /* ── Step 2: bare-word fallback (full-line strstr) ──── */
    for (size_t t = 0; t < NTABLE; t++) {
        if (strstr(upper, table[t].kw)) {
            return table[t].sev;
        }
    }

    return SEV_UNKNOWN;
}

/*
 * Print an ASCII bar of `val` out of `max`, padded to `width` chars.
 */
void util_print_bar(int val, int max, int width, const char *color)
{
    int filled = (max > 0) ? (val * width / max) : 0;
    if (filled > width) filled = width;
    printf("%s", color);
    for (int i = 0; i < filled; i++)   putchar('#');
    printf(ANSI_RESET);
    for (int i = filled; i < width; i++) putchar('.');
}
