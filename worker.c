#include "logengine.h"

/*
 * Each worker thread:
 *   1. Acquires the I/O semaphore (limits concurrent disk readers)
 *   2. Reads its assigned byte range line-by-line
 *   3. Releases the I/O semaphore as soon as its read is done
 *   4. Classifies severity and matches patterns on each line
 *   5. Accumulates into local stats (no locking during parsing)
 *   6. Merges local stats into global stats (single mutex acquisition)
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
    sem_wait(iosem);    /* acquire I/O slot */

    /* Open a private file descriptor to avoid seeking conflicts */
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

    /* Allocate a read buffer for the segment */
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
        total_read  += r;
        remaining   -= r;
    }
    buf[total_read] = '\0';

    close(fd);
    sem_post(iosem);    /* release I/O slot */

    /* ── Parse phase ───────────────────────────────────── */
    char *line_start = buf;
    char *ptr        = buf;
    char *end        = buf + total_read;
    long  local_line = 0;   /* line index within this segment */

    while (ptr <= end) {
        if (ptr == end || *ptr == '\n') {
            if (ptr > line_start) {
                /* Temporarily NUL-terminate the line */
                char saved = *ptr;
                *ptr = '\0';

                long global_line = seg->line_offset + local_line + 1; /* 1-based */

                /* Severity classification */
                Severity sev = util_parse_severity(line_start);
                ls.sev_counts[sev]++;
                ls.total_lines++;

                /* Pattern matching */
                for (int p = 0; p < cfg->num_patterns; p++) {
                    PatternRecord *pr = &cfg->patterns[p];
                    int matched = 0;

                    if (pr->is_regex) {
                        matched = (regexec(&pr->regex, line_start, 0, NULL, 0) == 0);
                    } else {
                        /* Case-insensitive substring search */
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

                /* Update shared progress counter (atomic-ish; slight drift is OK) */
                if (wa->progress_lines) {
                    __sync_fetch_and_add(wa->progress_lines, 1);
                }

                *ptr = saved;
                local_line++;
            } else {
                /* blank line still counts */
                ls.total_lines++;
                local_line++;
                if (wa->progress_lines) {
                    __sync_fetch_and_add(wa->progress_lines, 1);
                }
            }
            line_start = ptr + 1;
        }
        ptr++;
    }

    free(buf);

    /* ── Merge phase ───────────────────────────────────── */
    stats_merge(gs, &ls, cfg->num_patterns);

    pthread_exit(NULL);
}
