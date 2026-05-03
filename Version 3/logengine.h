#ifndef LOGENGINE_H
#define LOGENGINE_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <regex.h>
#include <stdint.h>
#include <stdarg.h>
#include <errno.h>
#include <ctype.h>

/* ── Limits ─────────────────────────────────────────────── */
#define MAX_THREADS        64
#define MAX_PATTERNS       16
#define MAX_PATTERN_LEN   128
#define MAX_LINE_LEN      4096
#define MAX_FILENAME_LEN   512
#define IO_CONCURRENCY      4   /* max simultaneous disk readers */

/* ── Severity levels ─────────────────────────────────────── */
typedef enum {
    SEV_DEBUG    = 0,
    SEV_INFO     = 1,
    SEV_WARNING  = 2,
    SEV_ERROR    = 3,
    SEV_CRITICAL = 4,
    SEV_UNKNOWN  = 5,
    SEV_COUNT    = 6
} Severity;

extern const char *SEV_NAMES[SEV_COUNT];
extern const char *SEV_COLORS[SEV_COUNT];

#define ANSI_RESET  "\033[0m"
#define ANSI_BOLD   "\033[1m"
#define ANSI_DIM    "\033[2m"
#define ANSI_CYAN   "\033[36m"
#define ANSI_WHITE  "\033[97m"
#define ANSI_BLUE   "\033[34m"
#define ANSI_GREEN  "\033[32m"
#define ANSI_RED    "\033[31m"
#define ANSI_YELLOW "\033[33m"

/* ── Pattern record ──────────────────────────────────────── */
typedef struct {
    char    pattern[MAX_PATTERN_LEN];
    regex_t regex;
    int     is_regex;           /* 1 = regex, 0 = plain keyword */
    long    match_count;
    long    first_line;         /* 1-based; 0 = no match yet    */
} PatternRecord;

/* ── Per-thread local statistics ─────────────────────────── */
typedef struct {
    long sev_counts[SEV_COUNT];
    long total_lines;
    long pattern_matches[MAX_PATTERNS];
    long pattern_first_line[MAX_PATTERNS];  /* local first-line per pattern */
} LocalStats;

/* ── Global aggregated statistics ───────────────────────── */
typedef struct {
    long           sev_counts[SEV_COUNT];
    long           total_lines;
    long           pattern_matches[MAX_PATTERNS];
    long           pattern_first_line[MAX_PATTERNS];
    pthread_mutex_t lock;
} GlobalStats;

/* ── Segment descriptor ──────────────────────────────────── */
typedef struct {
    int     thread_id;
    int     fd;
    off_t   start;
    off_t   end;            /* exclusive byte boundary */
    long    line_offset;    /* global line number of first line in segment */
} Segment;

/* ── Engine configuration ────────────────────────────────── */
typedef struct {
    char          logfile[MAX_FILENAME_LEN];
    int           num_threads;
    int           benchmark;        /* run single-thread baseline? */
    int           export_txt;
    int           export_csv;
    char          export_path[MAX_FILENAME_LEN];
    PatternRecord patterns[MAX_PATTERNS];
    int           num_patterns;
    int           use_ansi;         /* colour output on/off        */
} Config;

/* ── Worker thread argument ──────────────────────────────── */
typedef struct {
    Segment      *seg;
    Config       *cfg;
    GlobalStats  *gstats;
    sem_t        *io_sem;
    volatile long *progress_lines;  /* shared progress counter     */
} WorkerArg;

/* ── Timing ──────────────────────────────────────────────── */
typedef struct {
    double single_thread_sec;
    double multi_thread_sec;
    double speedup;
    long   total_lines;
    double mt_throughput;   /* lines/sec */
    double st_throughput;
} BenchResult;

/* ── Function prototypes ─────────────────────────────────── */

/* config.c */
void     config_defaults(Config *cfg);
int      config_parse_args(int argc, char **argv, Config *cfg);
void     config_print_usage(const char *prog);

/* segment.c */
int      segment_file(const char *path, int n, Segment *segs, int fd);
void     segment_free(Segment *segs, int n);

/* worker.c */
void    *worker_thread(void *arg);

/* stats.c */
void     stats_init(GlobalStats *gs, int num_patterns);
void     stats_merge(GlobalStats *gs, const LocalStats *ls,
                     int num_patterns);
void     stats_destroy(GlobalStats *gs);

/* analytics.c */
void     analytics_render(const GlobalStats *gs, const Config *cfg,
                          const BenchResult *bench, double elapsed_sec,
                          long file_bytes);
void     analytics_export_txt(const GlobalStats *gs, const Config *cfg,
                              const BenchResult *bench, double elapsed_sec);
void     analytics_export_csv(const GlobalStats *gs, const Config *cfg,
                              const BenchResult *bench);

/* benchmark.c */
double   benchmark_single(const char *path, const Config *cfg,
                          GlobalStats *gs_out);

/* progress.c */
void     progress_start(volatile long *counter, long total_lines_hint,
                        int use_ansi);
void     progress_stop(void);

/* util.c */
Severity util_parse_severity(const char *line);
double   util_now_sec(void);
void     util_print_bar(int val, int max, int width, const char *color);

#endif /* LOGENGINE_H */
