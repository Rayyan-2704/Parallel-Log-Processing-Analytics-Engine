#include "logengine.h"
#include <pthread.h>

/*
 * Fix applied: volatile int stop replaced with atomic_int.
 * Without a memory barrier, the write in progress_stop() is not guaranteed
 * to be visible to the progress thread before pthread_join() returns on
 * non-x86 architectures (ARM, POWER). atomic_store/atomic_load use
 * sequentially-consistent ordering by default, which provides the required
 * visibility guarantee.
 */

typedef struct {
    atomic_long   *counter;
    long           total;
    int            use_ansi;
    atomic_int     stop;   /* was: volatile int — now C11 atomic */
} ProgressCtx;

static ProgressCtx  g_pctx;
static pthread_t    g_prog_tid;

static void *progress_thread(void *arg)
{
    ProgressCtx *ctx = (ProgressCtx *)arg;
    int bar_width = 40;

    while (!atomic_load(&ctx->stop)) {
        long done = atomic_load(ctx->counter);
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
        long final = atomic_load(ctx->counter);
        printf("\r  Processing  [");
        printf(ANSI_GREEN);
        for (int i = 0; i < bar_width; i++) putchar('#');
        printf(ANSI_RESET "]  100%%  (%ld lines)\n", final);
        fflush(stdout);
    }

    return NULL;
}

void progress_start(atomic_long *counter, long total_lines_hint, int use_ansi)
{
    g_pctx.counter  = counter;
    g_pctx.total    = total_lines_hint;
    g_pctx.use_ansi = use_ansi;
    atomic_store(&g_pctx.stop, 0);
    pthread_create(&g_prog_tid, NULL, progress_thread, &g_pctx);
}

void progress_stop(void)
{
    atomic_store(&g_pctx.stop, 1);
    pthread_join(g_prog_tid, NULL);
}
