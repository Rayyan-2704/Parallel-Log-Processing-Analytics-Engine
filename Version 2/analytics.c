#include "logengine.h"

/* ─── helpers ─────────────────────────────────────────────── */

static void print_divider(int width, const char *color)
{
    printf("%s", color);
    for (int i = 0; i < width; i++) fputs("\xe2\x94\x80", stdout); /* UTF-8 BOX DRAWINGS LIGHT HORIZONTAL */
    printf(ANSI_RESET "\n");
}

static void print_header(const char *title, int width)
{
    int pad = (width - (int)strlen(title) - 2) / 2;
    printf(ANSI_BOLD ANSI_CYAN);
    for (int i = 0; i < pad; i++) fputs("\xe2\x94\x80", stdout);
    printf("  %s  ", title);
    for (int i = 0; i < pad; i++) fputs("\xe2\x94\x80", stdout);
    printf(ANSI_RESET "\n");
}

/* ─── terminal analytics panel ───────────────────────────── */

void analytics_render(const GlobalStats *gs,
                      const Config      *cfg,
                      const BenchResult *bench,
                      double             elapsed_sec,
                      long               file_bytes)
{
    const int W = 68;

    printf("\n");
    print_header("PARALLEL LOG ANALYTICS ENGINE", W);
    printf("\n");

    /* ── Summary ── */
    printf(ANSI_BOLD "  %-22s" ANSI_RESET " %ld\n", "Total lines parsed:", gs->total_lines);
    printf(ANSI_BOLD "  %-22s" ANSI_RESET " %d\n",  "Worker threads:", cfg->num_threads);
    printf(ANSI_BOLD "  %-22s" ANSI_RESET " %.3f s\n","Wall-clock time:", elapsed_sec);
    printf(ANSI_BOLD "  %-22s" ANSI_RESET " %.0f lines/s\n",
           "Throughput:",
           elapsed_sec > 0 ? (double)gs->total_lines / elapsed_sec : 0.0);
    printf(ANSI_BOLD "  %-22s" ANSI_RESET " %.2f MB\n", "File size:",
           (double)file_bytes / (1024.0 * 1024.0));
    printf("\n");
    print_divider(W, ANSI_DIM);

    /* ── Severity breakdown ── */
    printf("\n" ANSI_BOLD "  SEVERITY BREAKDOWN\n" ANSI_RESET "\n");
    long total = gs->total_lines > 0 ? gs->total_lines : 1;
    for (int s = 0; s < SEV_COUNT; s++) {
        long cnt = gs->sev_counts[s];
        double pct = 100.0 * cnt / total;
        printf("  %s%-9s" ANSI_RESET " %7ld  (%5.1f%%)  [",
               cfg->use_ansi ? SEV_COLORS[s] : "",
               SEV_NAMES[s], cnt, pct);
        util_print_bar((int)cnt, (int)total, 28,
                       cfg->use_ansi ? SEV_COLORS[s] : "");
        printf("]\n");
    }
    printf("\n");
    print_divider(W, ANSI_DIM);

    /* ── Pattern matches ── */
    if (cfg->num_patterns > 0) {
        printf("\n" ANSI_BOLD "  PATTERN MATCHES\n" ANSI_RESET "\n");
        for (int p = 0; p < cfg->num_patterns; p++) {
            printf("  " ANSI_YELLOW "%-28s" ANSI_RESET
                   " %7ld match%s",
                   cfg->patterns[p].pattern,
                   gs->pattern_matches[p],
                   gs->pattern_matches[p] == 1 ? " " : "es");
            if (gs->pattern_first_line[p] > 0) {
                printf("  (first at line %ld)", gs->pattern_first_line[p]);
            } else {
                printf("  (no match)");
            }
            printf("\n");
        }
        printf("\n");
        print_divider(W, ANSI_DIM);
    }

    /* ── Benchmark comparison ── */
    if (cfg->benchmark && bench) {
        printf("\n" ANSI_BOLD "  BENCHMARK: SINGLE vs MULTI-THREAD\n" ANSI_RESET "\n");
        printf("  %-28s  %9.3f s  (%7.0f lines/s)\n",
               "Single-thread baseline:",
               bench->single_thread_sec,
               bench->st_throughput);
        printf("  %-28s  %9.3f s  (%7.0f lines/s)\n",
               "Multi-thread engine:",
               bench->multi_thread_sec,
               bench->mt_throughput);
        printf("\n");
        printf(ANSI_BOLD "  Speedup factor: " ANSI_GREEN "%.2fx" ANSI_RESET "\n",
               bench->speedup);

        /* Speedup bar */
        printf("  Single  [");
        util_print_bar(1, (int)(bench->speedup + 0.5), 36, ANSI_DIM);
        printf("]\n  Multi   [");
        util_print_bar((int)(bench->speedup + 0.5),
                       (int)(bench->speedup + 0.5), 36, ANSI_GREEN);
        printf("]\n\n");
        print_divider(W, ANSI_DIM);
    }

    printf("\n");
}

/* ─── Plain-text export ───────────────────────────────────── */

void analytics_export_txt(const GlobalStats *gs,
                          const Config      *cfg,
                          const BenchResult *bench,
                          double             elapsed_sec)
{
    char path[MAX_FILENAME_LEN + 8];
    snprintf(path, sizeof(path), "%s.txt", cfg->export_path);

    FILE *f = fopen(path, "w");
    if (!f) { perror("export txt fopen"); return; }

    fprintf(f, "=== Parallel Log Processing & Analytics Engine ===\n\n");
    fprintf(f, "Log file        : %s\n", cfg->logfile);
    fprintf(f, "Threads         : %d\n", cfg->num_threads);
    fprintf(f, "Total lines     : %ld\n", gs->total_lines);
    fprintf(f, "Wall-clock time : %.4f s\n", elapsed_sec);
    if (elapsed_sec > 0)
        fprintf(f, "Throughput      : %.0f lines/s\n",
                (double)gs->total_lines / elapsed_sec);

    fprintf(f, "\n--- Severity Breakdown ---\n");
    long total = gs->total_lines > 0 ? gs->total_lines : 1;
    for (int s = 0; s < SEV_COUNT; s++) {
        fprintf(f, "  %-9s : %ld (%.1f%%)\n",
                SEV_NAMES[s], gs->sev_counts[s],
                100.0 * gs->sev_counts[s] / total);
    }

    if (cfg->num_patterns > 0) {
        fprintf(f, "\n--- Pattern Matches ---\n");
        for (int p = 0; p < cfg->num_patterns; p++) {
            fprintf(f, "  %-30s : %ld match%s",
                    cfg->patterns[p].pattern,
                    gs->pattern_matches[p],
                    gs->pattern_matches[p] == 1 ? "" : "es");
            if (gs->pattern_first_line[p] > 0)
                fprintf(f, "  (first at line %ld)", gs->pattern_first_line[p]);
            fprintf(f, "\n");
        }
    }

    if (cfg->benchmark && bench) {
        fprintf(f, "\n--- Benchmark ---\n");
        fprintf(f, "  Single-thread : %.4f s  (%.0f lines/s)\n",
                bench->single_thread_sec, bench->st_throughput);
        fprintf(f, "  Multi-thread  : %.4f s  (%.0f lines/s)\n",
                bench->multi_thread_sec, bench->mt_throughput);
        fprintf(f, "  Speedup       : %.2fx\n", bench->speedup);
    }

    fclose(f);
    printf(ANSI_GREEN "  Report saved  → %s\n" ANSI_RESET, path);
}

/* ─── CSV export ──────────────────────────────────────────── */

void analytics_export_csv(const GlobalStats *gs,
                          const Config      *cfg,
                          const BenchResult *bench)
{
    char path[MAX_FILENAME_LEN + 8];
    snprintf(path, sizeof(path), "%s.csv", cfg->export_path);

    FILE *f = fopen(path, "w");
    if (!f) { perror("export csv fopen"); return; }

    /* Severity section */
    fprintf(f, "section,key,value\n");
    fprintf(f, "summary,logfile,%s\n", cfg->logfile);
    fprintf(f, "summary,threads,%d\n", cfg->num_threads);
    fprintf(f, "summary,total_lines,%ld\n", gs->total_lines);

    for (int s = 0; s < SEV_COUNT; s++) {
        fprintf(f, "severity,%s,%ld\n", SEV_NAMES[s], gs->sev_counts[s]);
    }

    for (int p = 0; p < cfg->num_patterns; p++) {
        fprintf(f, "pattern,%s,%ld\n",
                cfg->patterns[p].pattern,
                gs->pattern_matches[p]);
    }

    if (cfg->benchmark && bench) {
        fprintf(f, "benchmark,single_thread_sec,%.6f\n", bench->single_thread_sec);
        fprintf(f, "benchmark,multi_thread_sec,%.6f\n",  bench->multi_thread_sec);
        fprintf(f, "benchmark,speedup,%.4f\n", bench->speedup);
        fprintf(f, "benchmark,st_throughput,%.0f\n", bench->st_throughput);
        fprintf(f, "benchmark,mt_throughput,%.0f\n", bench->mt_throughput);
    }

    fclose(f);
    printf(ANSI_GREEN "  CSV saved     → %s\n" ANSI_RESET, path);
}
