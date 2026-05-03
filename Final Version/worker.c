#include "logengine.h"

void *worker_thread(void *arg)
{
    WorkerArg *wa = (WorkerArg *)arg;
    Segment *seg = wa->seg;
    Config *cfg = wa->cfg;
    GlobalStats *gs = wa->gstats;
    sem_t *iosem = wa->io_sem;

    LocalStats ls;
    memset(&ls, 0, sizeof(ls));

    // I/O phase
    sem_wait(iosem);

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
        remaining -= r;
    }
    buf[total_read] = '\0';

    close(fd);
    sem_post(iosem);

    // Parse phase
    char *line_start = buf;
    char *ptr = buf;
    char *end = buf + total_read;
    long  local_line = 0;
    long  truncated = 0;

    while (ptr < end) {
        if (*ptr == '\n') {
            size_t line_len = (size_t)(ptr - line_start);

            if (line_len > 0) {
                if (line_len >= MAX_LINE_LEN)
                    truncated++;

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
                    }
                    else {
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
                ls.total_lines++;
                if (wa->progress_lines)
                    atomic_fetch_add(wa->progress_lines, 1);
            }

            local_line++;
            line_start = ptr + 1;
        }
        ptr++;
    }

    if (line_start < end) {
        size_t line_len = (size_t)(end - line_start);
        if (line_len >= MAX_LINE_LEN) {
            truncated++;
        }

        long global_line = seg->line_offset + local_line + 1;

        Severity sev = util_parse_severity(line_start);
        ls.sev_counts[sev]++;
        ls.total_lines++;

        for (int p = 0; p < cfg->num_patterns; p++) {
            PatternRecord *pr = &cfg->patterns[p];
            int matched = 0;

            if (pr->is_regex) {
                matched = (regexec(&pr->regex, line_start, 0, NULL, 0) == 0);
            }
            else {
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
                if (ls.pattern_first_line[p] == 0 || global_line < ls.pattern_first_line[p]) {
                    ls.pattern_first_line[p] = global_line;
                }
            }
        }

        if (wa->progress_lines)
            atomic_fetch_add(wa->progress_lines, 1);
    }

    ls.truncated_lines = truncated;

    free(buf);

    // Merge phase
    stats_merge(gs, &ls, cfg->num_patterns);

    pthread_exit(NULL);
}
