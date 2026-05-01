#include "logengine.h"

/*
 * Worker thread — the core of the parallel engine.
 *
 * Fixes applied:
 *  1. Blank-line overcount: the old loop ran while (ptr <= end), so when a
 *     file ends with '\n' the loop body fired one extra time for the empty
 *     "line" after the final newline, inflating total_lines by 1. Fixed by
 *     stopping at ptr < end (exclusive) and handling the final unterminated
 *     line separately if needed.
 *  2. atomic_long progress counter: replaced __sync_fetch_and_add (GCC
 *     built-in, not strictly POSIX) with C11 atomic_fetch_add.
 *  3. Line truncation warning: if a line exceeds MAX_LINE_LEN bytes, a
 *     per-thread counter is incremented and reported after the merge so the
 *     user knows pattern matches may be understated on those lines.
 */
void *worker_thread(void *arg)
{
    WorkerArg   *wa   = (WorkerArg *)arg;
    Segment     *seg  = wa->seg;
    Config      *cfg  = wa->cfg;
    GlobalStats *gs   = wa->gstats;
    sem_t       *iosem= wa->io_sem;

    LocalStats ls;
    memset(&ls, 0, sizeof(ls));

    /* ── I/O phase ─────────────────────────────────────── */
    sem_wait(iosem);

    /* Each worker opens its own private fd — the original fd in main.c is
     * closed before workers start, so we must not use seg->fd (not stored). */
    int fd = open(cfg->logfile, O_RDONLY);
    if (fd < 0) {
        perror("worker open");
        sem_post(iosem);
        pthread_exit(NULL);
    }

    if (lseek(fd, seg->start, SEEK_SET) < 0) {
        perror("worker lseek");
        close(fd);
        sem_post(iosem);
        pthread_exit(NULL);
    }

    off_t seg_size = seg->end - seg->start;
    char *buf = malloc((size_t)seg_size + 1);
    if (!buf) {
        close(fd);
        sem_post(iosem);
        pthread_exit(NULL);
    }

    ssize_t total_read = 0;
    ssize_t remaining  = (ssize_t)seg_size;
    while (remaining > 0) {
        ssize_t r = read(fd, buf + total_read, (size_t)remaining);
        if (r <= 0) break;
        total_read += r;
        remaining  -= r;
    }
    buf[total_read] = '\0';

    close(fd);
    sem_post(iosem);

    /* ── Parse phase ───────────────────────────────────── */
    char *line_start  = buf;
    char *ptr         = buf;
    char *end         = buf + total_read;
    long  local_line  = 0;
    long  truncated   = 0;  /* lines that exceeded MAX_LINE_LEN */

    /*
     * Loop invariant: ptr walks forward one byte at a time.
     * We stop at ptr < end (strictly less), so when the segment ends with
     * '\n' we do NOT fire an extra iteration for the empty string after it,
     * which was the blank-line overcount bug.
     *
     * After the loop, if there are bytes left between line_start and end
     * (i.e. the segment's last line has no terminating newline), we process
     * that final partial line explicitly.
     */
    while (ptr < end) {
        if (*ptr == '\n') {
            /* Process the line [line_start, ptr) */
            size_t line_len = (size_t)(ptr - line_start);

            if (line_len > 0) {
                /* Warn on truncation (the line content beyond MAX_LINE_LEN-1
                 * bytes will be missed by the pattern scan). */
                if (line_len >= MAX_LINE_LEN) {
                    truncated++;
                }

                char saved = *ptr;
                *ptr = '\0';

                long global_line = seg->line_offset + local_line + 1;

                Severity sev = util_parse_severity(line_start);
                ls.sev_counts[sev]++;
                ls.total_lines++;

                for (int p = 0; p < cfg->num_patterns; p++) {
                    PatternRecord *pr = &cfg->patterns[p];
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
                        ls.pattern_matches[p]++;
                        if (ls.pattern_first_line[p] == 0 ||
                            global_line < ls.pattern_first_line[p]) {
                            ls.pattern_first_line[p] = global_line;
                        }
                    }
                }

                if (wa->progress_lines)
                    atomic_fetch_add(wa->progress_lines, 1);

                *ptr = saved;
            } else {
                /* Genuine blank line */
                ls.total_lines++;
                local_line++;
                if (wa->progress_lines)
                    atomic_fetch_add(wa->progress_lines, 1);
                line_start = ptr + 1;
                ptr++;
                continue;
            }

            local_line++;
            line_start = ptr + 1;
        }
        ptr++;
    }

    /* Handle final line that has no trailing '\n'
     * (only process if there is actual content). */
    if (line_start < end) {
        size_t line_len = (size_t)(end - line_start);
        if (line_len >= MAX_LINE_LEN) {
            truncated++;
        }
        /* end is buf+total_read which is already NUL-terminated */
        long global_line = seg->line_offset + local_line + 1;

        Severity sev = util_parse_severity(line_start);
        ls.sev_counts[sev]++;
        ls.total_lines++;

        for (int p = 0; p < cfg->num_patterns; p++) {
            PatternRecord *pr = &cfg->patterns[p];
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
                ls.pattern_matches[p]++;
                if (ls.pattern_first_line[p] == 0 ||
                    global_line < ls.pattern_first_line[p]) {
                    ls.pattern_first_line[p] = global_line;
                }
            }
        }

        if (wa->progress_lines)
            atomic_fetch_add(wa->progress_lines, 1);
    }

    free(buf);

    /* Report truncated lines under the global lock — add to a shared counter
     * so the main thread can warn the user. */
    if (truncated > 0) {
        pthread_mutex_lock(&gs->lock);
        gs->truncated_lines += truncated;
        pthread_mutex_unlock(&gs->lock);
    }

    /* ── Merge phase ───────────────────────────────────── */
    stats_merge(gs, &ls, cfg->num_patterns);

    pthread_exit(NULL);
}
