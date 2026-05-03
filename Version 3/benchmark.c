#include "logengine.h"

/*
 * Run a single-threaded parse of the same file with the same config.
 * Populates gs_out so results can be validated against multi-thread output.
 * Returns wall-clock seconds elapsed.
 */
double benchmark_single(const char *path, const Config *cfg, GlobalStats *gs_out)
{
    stats_init(gs_out, cfg->num_patterns);

    FILE *f = fopen(path, "r");
    if (!f) { perror("benchmark_single fopen"); return -1.0; }

    double t_start = util_now_sec();

    char line[MAX_LINE_LEN];
    long lineno = 0;

    while (fgets(line, sizeof(line), f)) {
        lineno++;

        /* Severity */
        Severity sev = util_parse_severity(line);
        gs_out->sev_counts[sev]++;
        gs_out->total_lines++;

        /* Patterns */
        for (int p = 0; p < cfg->num_patterns; p++) {
            const PatternRecord *pr = &cfg->patterns[p];
            int matched = 0;

            if (pr->is_regex) {
                matched = (regexec(&pr->regex, line, 0, NULL, 0) == 0);
            } else {
                char upper_line[MAX_LINE_LEN];
                char upper_pat[MAX_PATTERN_LEN];
                size_t ll = strlen(line);
                if (ll >= MAX_LINE_LEN) ll = MAX_LINE_LEN - 1;
                for (size_t i = 0; i < ll; i++)
                    upper_line[i] = (char)toupper((unsigned char)line[i]);
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
    fclose(f);
    return elapsed;
}
