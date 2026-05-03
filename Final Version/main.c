#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "logengine.h"


#define SAMPLE_BYTES (65536)

static long estimate_line_count(int fd, long file_bytes)
{
    char sample[SAMPLE_BYTES];
    if (lseek(fd, 0, SEEK_SET) < 0) return file_bytes / 80;
    ssize_t r = read(fd, sample, sizeof(sample));
    if (r <= 0) return file_bytes / 80;

    long newlines = 0;
    for (ssize_t i = 0; i < r; i++)
        if (sample[i] == '\n') newlines++;

    if (newlines == 0) return file_bytes / 80;
    long avg_len = (long)r / newlines;
    if (avg_len < 1) avg_len = 1;
    return file_bytes / avg_len;
}

int main(int argc, char **argv)
{
    Config cfg;
    config_defaults(&cfg);
    if (config_parse_args(argc, argv, &cfg) < 0) return 1;

    int fd = open(cfg.logfile, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "Error: cannot open '%s': %s\n",
                cfg.logfile, strerror(errno));
        return 1;
    }

    struct stat st;
    if (fstat(fd, &st) < 0) { perror("fstat"); close(fd); return 1; }
    long file_bytes = (long)st.st_size;

    long line_estimate = estimate_line_count(fd, file_bytes);

    Segment segs[MAX_THREADS];
    if (segment_file(cfg.logfile, cfg.num_threads, segs, fd) < 0) {
        close(fd); return 1;
    }
    
    close(fd);

    GlobalStats gstats;
    stats_init(&gstats, cfg.num_patterns);

    sem_t io_sem;
    sem_init(&io_sem, 0, IO_CONCURRENCY);

    atomic_long progress_counter;
    atomic_init(&progress_counter, 0);

    if (cfg.use_ansi) {
        printf(ANSI_BOLD ANSI_CYAN
               "\n  \xe2\x95\x94\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x97\n"
               "  \xe2\x95\x91     Parallel Log Processing & Analytics Engine   \xe2\x95\x91\n"
               "  \xe2\x95\x91       CS-2006 Operating Systems \xe2\x80\x94 Project        \xe2\x95\x91\n"
               "  \xe2\x95\x9a\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x9d\n"
               ANSI_RESET "\n");
        printf("  File    : %s\n", cfg.logfile);
        printf("  Threads : %d\n", cfg.num_threads);
        printf("  Size    : %.2f MB\n\n", (double)file_bytes/(1024.0*1024.0));
    }

    BenchResult bench;
    memset(&bench, 0, sizeof(bench));

    if (cfg.benchmark) {
        printf(ANSI_DIM "  Running single-thread baseline (bulk-read method)..."
               ANSI_RESET "\n");
        GlobalStats st_stats;
        bench.single_thread_sec = benchmark_single(cfg.logfile, &cfg, &st_stats);
        bench.st_throughput = bench.single_thread_sec > 0
            ? (double)st_stats.total_lines / bench.single_thread_sec : 0;
        stats_destroy(&st_stats);
        printf(ANSI_DIM "  Baseline done: %.3f s\n" ANSI_RESET,
               bench.single_thread_sec);
    }

    pthread_t  threads[MAX_THREADS];
    WorkerArg  wargs[MAX_THREADS];

    progress_start(&progress_counter, line_estimate, cfg.use_ansi);

    double t_start = util_now_sec();

    for (int i = 0; i < cfg.num_threads; i++) {
        wargs[i].seg            = &segs[i];
        wargs[i].cfg            = &cfg;
        wargs[i].gstats         = &gstats;
        wargs[i].io_sem         = &io_sem;
        wargs[i].progress_lines = &progress_counter;

        if (pthread_create(&threads[i], NULL, worker_thread, &wargs[i]) != 0) {
            perror("pthread_create");
            for (int j = 0; j < i; j++) pthread_join(threads[j], NULL);
            progress_stop();
            stats_destroy(&gstats);
            sem_destroy(&io_sem);
            return 1;
        }
    }

    for (int i = 0; i < cfg.num_threads; i++)
        pthread_join(threads[i], NULL);

    double elapsed = util_now_sec() - t_start;
    progress_stop();

    if (gstats.truncated_lines > 0) {
        fprintf(stderr,
                ANSI_YELLOW "  Warning: %ld line(s) exceeded MAX_LINE_LEN (%d bytes) "
                "and were truncated during parsing. Pattern matches on those "
                "lines may be understated.\n" ANSI_RESET,
                gstats.truncated_lines, MAX_LINE_LEN);
    }

    if (cfg.benchmark) {
        bench.multi_thread_sec = elapsed;
        bench.mt_throughput = elapsed > 0
            ? (double)gstats.total_lines / elapsed : 0;
        bench.total_lines = gstats.total_lines;
        bench.speedup = bench.multi_thread_sec > 0
            ? bench.single_thread_sec / bench.multi_thread_sec : 0;
    }

    analytics_render(&gstats, &cfg,
                     cfg.benchmark ? &bench : NULL,
                     elapsed, file_bytes);

    if (cfg.export_txt)
        analytics_export_txt(&gstats, &cfg,
                             cfg.benchmark ? &bench : NULL, elapsed);
    if (cfg.export_csv)
        analytics_export_csv(&gstats, &cfg,
                             cfg.benchmark ? &bench : NULL);

    if (cfg.plot) {
        if (!cfg.export_csv) {
            
            fprintf(stderr,
                    ANSI_YELLOW "  Note: --plot used without --export; "
                    "charts will be written to '%s_severity.png'"
                    " (and '%s_speedup.png' if -b).\n" ANSI_RESET,
                    cfg.export_path, cfg.export_path);
        }
        analytics_plot(&gstats, &cfg,
                       cfg.benchmark ? &bench : NULL);
    }

    
    for (int p = 0; p < cfg.num_patterns; p++) {
        if (cfg.patterns[p].is_regex)
            regfree(&cfg.patterns[p].regex);
    }

    stats_destroy(&gstats);
    sem_destroy(&io_sem);

    return 0;
}
