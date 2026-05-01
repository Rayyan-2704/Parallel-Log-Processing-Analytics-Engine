#include "logengine.h"

/*
 * Run a single-threaded parse of the entire file using the SAME bulk-read
 * + pointer-scan strategy as worker_thread(), so the benchmark measures
 * parallelism benefit rather than I/O method differences.
 *
 * Fix applied: the old implementation used fgets() (line-by-line via a
 * FILE* buffer). That incurs stdio locking and extra syscall overhead
 * compared to the workers' bulk read-then-scan approach, making the
 * single-thread baseline artificially slow and inflating the speedup figure.
 *
 * NOTE on regex patterns: the cfg->patterns array contains already-compiled
 * regex_t objects (compiled in config.c). This function uses them read-only
 * and does NOT call regcomp() or regfree() — those are the caller's
 * responsibility (main.c calls regfree() exactly once at cleanup).
 *
 * Returns wall-clock seconds elapsed, or -1.0 on error.
 */
double benchmark_single(const char *path, const Config *cfg, GlobalStats *gs_out)
{
    stats_init(gs_out, cfg->num_patterns);

    int fd = open(path, O_RDONLY);
    if (fd < 0) { perror("benchmark_single open"); return -1.0; }

    struct stat st;
    if (fstat(fd, &st) < 0) { perror("fstat"); close(fd); return -1.0; }

    off_t file_size = st.st_size;
    char *buf = malloc((size_t)file_size + 1);
    if (!buf) { close(fd); return -1.0; }

    /* Bulk read */
    ssize_t total_read = 0, remaining = (ssize_t)file_size;
    while (remaining > 0) {
        ssize_t r = read(fd, buf + total_read, (size_t)remaining);
        if (r <= 0) break;
        total_read += r;
        remaining  -= r;
    }
    buf[total_read] = '\0';
    close(fd);

    double t_start = util_now_sec();

    /* Pointer-scan — identical logic to worker_thread() */
    char *line_start = buf;
    char *ptr        = buf;
    char *end        = buf + total_read;
    long  lineno     = 0;

    while (ptr < end) {
        if (*ptr == '\n') {
            size_t line_len = (size_t)(ptr - line_start);
            if (line_len > 0) {
                char saved = *ptr;
                *ptr = '\0';

                lineno++;
                Severity sev = util_parse_severity(line_start);
                gs_out->sev_counts[sev]++;
                gs_out->total_lines++;

                for (int p = 0; p < cfg->num_patterns; p++) {
                    const PatternRecord *pr = &cfg->patterns[p];
                    int matched = 0;
                    if (pr->is_regex) {
                        matched = (regexec(&pr->regex, line_start, 0, NULL, 0) == 0);
                    } else {
                        char upper_line[MAX_LINE_LEN];
                        char upper_pat[MAX_PATTERN_LEN];
                        size_t ll = strlen(line_start);
                        if (ll >= MAX_LINE_LEN) ll = MAX_LINE_LEN - 1;
                        for (size_t i = 0; i < ll; i++)
                            upper_line[i] = (char)toupper((unsigned char)line_start[i]);
                        upper_line[ll] = '\0';
                        size_t pl = strlen(pr->pattern);
                        if (pl >= MAX_PATTERN_LEN) pl = MAX_PATTERN_LEN - 1;
                        for (size_t i = 0; i < pl; i++)
                            upper_pat[i] = (char)toupper((unsigned char)pr->pattern[i]);
                        upper_pat[pl] = '\0';
                        matched = (strstr(upper_line, upper_pat) != NULL);
                    }
                    if (matched) {
                        gs_out->pattern_matches[p]++;
                        if (gs_out->pattern_first_line[p] == 0)
                            gs_out->pattern_first_line[p] = lineno;
                    }
                }

                *ptr = saved;
            } else {
                /* blank line */
                gs_out->total_lines++;
                lineno++;
            }
            line_start = ptr + 1;
        }
        ptr++;
    }

    /* Final unterminated line */
    if (line_start < end) {
        lineno++;
        Severity sev = util_parse_severity(line_start);
        gs_out->sev_counts[sev]++;
        gs_out->total_lines++;

        for (int p = 0; p < cfg->num_patterns; p++) {
            const PatternRecord *pr = &cfg->patterns[p];
            int matched = 0;
            if (pr->is_regex) {
                matched = (regexec(&pr->regex, line_start, 0, NULL, 0) == 0);
            } else {
                char upper_line[MAX_LINE_LEN];
                char upper_pat[MAX_PATTERN_LEN];
                size_t ll = strlen(line_start);
                if (ll >= MAX_LINE_LEN) ll = MAX_LINE_LEN - 1;
                for (size_t i = 0; i < ll; i++)
                    upper_line[i] = (char)toupper((unsigned char)line_start[i]);
                upper_line[ll] = '\0';
                size_t pl = strlen(pr->pattern);
                if (pl >= MAX_PATTERN_LEN) pl = MAX_PATTERN_LEN - 1;
                for (size_t i = 0; i < pl; i++)
                    upper_pat[i] = (char)toupper((unsigned char)pr->pattern[i]);
                upper_pat[pl] = '\0';
                matched = (strstr(upper_line, upper_pat) != NULL);
            }
            if (matched) {
                gs_out->pattern_matches[p]++;
                if (gs_out->pattern_first_line[p] == 0)
                    gs_out->pattern_first_line[p] = lineno;
            }
        }
    }

    double elapsed = util_now_sec() - t_start;
    free(buf);
    return elapsed;
}
