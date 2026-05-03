#include "logengine.h"

/* Return current wall-clock time in seconds */
double util_now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/*
 * Parse the severity level from a log line.
 * Checks for bracketed tags first ([ERROR]), then bare words.
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

    char upper[MAX_LINE_LEN];
    size_t len = strlen(line);
    if (len >= MAX_LINE_LEN) len = MAX_LINE_LEN - 1;
    for (size_t i = 0; i < len; i++) upper[i] = (char)toupper((unsigned char)line[i]);
    upper[len] = '\0';

    for (size_t t = 0; t < sizeof(table)/sizeof(table[0]); t++) {
        if (strstr(upper, table[t].kw)) {
            return table[t].sev;
        }
    }
    return SEV_UNKNOWN;
}

/*
 * Print an ASCII bar of `val` out of `max`, padded to `width` chars.
 * Colour is an ANSI escape string (or "" for no colour).
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
