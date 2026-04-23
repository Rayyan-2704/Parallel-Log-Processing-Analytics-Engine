#include "logengine.h"
#include <pthread.h>

typedef struct {
    volatile long *counter;
    long           total;
    int            use_ansi;
    volatile int   stop;
} ProgressCtx;

static ProgressCtx  g_pctx;
static pthread_t    g_prog_tid;

static void *progress_thread(void *arg)
{
    ProgressCtx *ctx = (ProgressCtx *)arg;
    int bar_width = 40;

    while (!ctx->stop) {
        long done = *ctx->counter;
        long tot  = ctx->total;

        if (ctx->use_ansi) {
            int pct = (tot > 0) ? (int)((done * 100) / tot) : 0;
            if (pct > 100) pct = 100;
            int filled = pct * bar_width / 100;

            printf("\r" ANSI_CYAN "  Processing  [" ANSI_RESET);
            printf(ANSI_GREEN);
            for (int i = 0; i < filled; i++) putchar('#');
            printf(ANSI_RESET ANSI_DIM);
            for (int i = filled; i < bar_width; i++) putchar('.');
            printf(ANSI_RESET ANSI_CYAN "]" ANSI_RESET
                   "  %3d%%  (%ld lines)", pct, done);
            fflush(stdout);
        }

        struct timespec ts = { .tv_sec = 0, .tv_nsec = 100000000L }; /* 100 ms */
        nanosleep(&ts, NULL);
    }

    if (ctx->use_ansi) {
        /* Final bar at 100% */
        printf("\r  Processing  [");
        printf(ANSI_GREEN);
        for (int i = 0; i < bar_width; i++) putchar('#');
        printf(ANSI_RESET "]  100%%  (%ld lines)\n", *ctx->counter);
        fflush(stdout);
    }

    return NULL;
}

void progress_start(volatile long *counter, long total_lines_hint, int use_ansi)
{
    g_pctx.counter  = counter;
    g_pctx.total    = total_lines_hint;
    g_pctx.use_ansi = use_ansi;
    g_pctx.stop     = 0;
    pthread_create(&g_prog_tid, NULL, progress_thread, &g_pctx);
}

void progress_stop(void)
{
    g_pctx.stop = 1;
    pthread_join(g_prog_tid, NULL);
}
