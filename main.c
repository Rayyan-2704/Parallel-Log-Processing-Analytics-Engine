#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "logengine.h"

int main(int argc, char **argv)
{
    /* ── 1. Parse config ───────────────────────────────── */
    Config cfg;
    config_defaults(&cfg);
    if (config_parse_args(argc, argv, &cfg) < 0) return 1;

    /* ── 2. Open file ──────────────────────────────────── */
    int fd = open(cfg.logfile, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "Error: cannot open '%s': %s\n",
                cfg.logfile, strerror(errno));
        return 1;
    }

    struct stat st;
    if (fstat(fd, &st) < 0) { perror("fstat"); close(fd); return 1; }
    long file_bytes = (long)st.st_size;

    /* ── 3. Segment the file ───────────────────────────── */
    Segment segs[MAX_THREADS];
    if (segment_file(cfg.logfile, cfg.num_threads, segs, fd) < 0) {
        close(fd); return 1;
    }
    close(fd);  /* worker threads open their own fds */

    /* ── 4. Initialise global stats & semaphore ────────── */
    GlobalStats gstats;
    stats_init(&gstats, cfg.num_patterns);

    sem_t io_sem;
    sem_init(&io_sem, 0, IO_CONCURRENCY);

    volatile long progress_counter = 0;

    /* ── 5. Estimate total lines for progress bar ──────── */
    /* Quick estimate: file_size / avg_line_length_guess    */
    long line_estimate = file_bytes / 80;
    if (line_estimate < 1) line_estimate = 1;

    /* ── 6. Banner ─────────────────────────────────────── */
    if (cfg.use_ansi) {
        printf(ANSI_BOLD ANSI_CYAN
               "\n  ╔══════════════════════════════════════════════════╗\n"
               "  ║     Parallel Log Processing & Analytics Engine   ║\n"
               "  ║       CS-2006 Operating Systems — Project 7      ║\n"
               "  ╚══════════════════════════════════════════════════╝\n"
               ANSI_RESET "\n");
        printf("  File    : %s\n", cfg.logfile);
        printf("  Threads : %d\n", cfg.num_threads);
        printf("  Size    : %.2f MB\n\n", (double)file_bytes/(1024.0*1024.0));
    }

    /* ── 7. Optional single-thread benchmark baseline ──── */
    BenchResult bench;
    memset(&bench, 0, sizeof(bench));

    if (cfg.benchmark) {
        printf(ANSI_DIM "  Running single-thread baseline..." ANSI_RESET "\n");
        GlobalStats st_stats;
        bench.single_thread_sec = benchmark_single(cfg.logfile, &cfg, &st_stats);
        bench.st_throughput = bench.single_thread_sec > 0
            ? (double)st_stats.total_lines / bench.single_thread_sec : 0;
        stats_destroy(&st_stats);
        printf(ANSI_DIM "  Baseline done: %.3f s\n" ANSI_RESET,
               bench.single_thread_sec);
    }

    /* ── 8. Spawn worker threads ───────────────────────── */
    pthread_t     threads[MAX_THREADS];
    WorkerArg     wargs[MAX_THREADS];

    progress_start(&progress_counter, line_estimate, cfg.use_ansi);

    double t_start = util_now_sec();

    for (int i = 0; i < cfg.num_threads; i++) {
        wargs[i].seg             = &segs[i];
        wargs[i].cfg             = &cfg;
        wargs[i].gstats          = &gstats;
        wargs[i].io_sem          = &io_sem;
        wargs[i].progress_lines  = &progress_counter;

        if (pthread_create(&threads[i], NULL, worker_thread, &wargs[i]) != 0) {
            perror("pthread_create");
            /* Clean up already-started threads */
            for (int j = 0; j < i; j++) pthread_join(threads[j], NULL);
            progress_stop();
            stats_destroy(&gstats);
            sem_destroy(&io_sem);
            return 1;
        }
    }

    /* ── 9. Join all workers ───────────────────────────── */
    for (int i = 0; i < cfg.num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    double elapsed = util_now_sec() - t_start;

    progress_stop();

    /* ── 10. Fill benchmark result ─────────────────────── */
    if (cfg.benchmark) {
        bench.multi_thread_sec = elapsed;
        bench.mt_throughput = elapsed > 0
            ? (double)gstats.total_lines / elapsed : 0;
        bench.total_lines = gstats.total_lines;
        bench.speedup = bench.multi_thread_sec > 0
            ? bench.single_thread_sec / bench.multi_thread_sec : 0;
    }

    /* ── 11. Render analytics panel ────────────────────── */
    analytics_render(&gstats, &cfg,
                     cfg.benchmark ? &bench : NULL,
                     elapsed, file_bytes);

    /* ── 12. Export reports ────────────────────────────── */
    if (cfg.export_txt)
        analytics_export_txt(&gstats, &cfg,
                             cfg.benchmark ? &bench : NULL, elapsed);
    if (cfg.export_csv)
        analytics_export_csv(&gstats, &cfg,
                             cfg.benchmark ? &bench : NULL);

    /* ── 13. Clean up ──────────────────────────────────── */
    /* Free compiled regex patterns */
    for (int p = 0; p < cfg.num_patterns; p++) {
        if (cfg.patterns[p].is_regex)
            regfree(&cfg.patterns[p].regex);
    }

    stats_destroy(&gstats);
    sem_destroy(&io_sem);

    return 0;
}
