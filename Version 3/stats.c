#include "logengine.h"

void stats_init(GlobalStats *gs, int num_patterns)
{
    memset(gs, 0, sizeof(*gs));
    for (int i = 0; i < num_patterns; i++) {
        gs->pattern_first_line[i] = 0;   /* 0 = not matched yet */
    }
    pthread_mutex_init(&gs->lock, NULL);
}

/*
 * Two-phase merge: caller (worker thread) holds its local stats, acquires
 * the global mutex once, then adds everything atomically.
 */
void stats_merge(GlobalStats *gs, const LocalStats *ls, int num_patterns)
{
    pthread_mutex_lock(&gs->lock);

    gs->total_lines += ls->total_lines;

    for (int s = 0; s < SEV_COUNT; s++) {
        gs->sev_counts[s] += ls->sev_counts[s];
    }

    for (int p = 0; p < num_patterns; p++) {
        gs->pattern_matches[p] += ls->pattern_matches[p];
        /* keep the earliest first-line across all threads */
        if (ls->pattern_first_line[p] > 0) {
            if (gs->pattern_first_line[p] == 0 ||
                ls->pattern_first_line[p] < gs->pattern_first_line[p]) {
                gs->pattern_first_line[p] = ls->pattern_first_line[p];
            }
        }
    }

    pthread_mutex_unlock(&gs->lock);
}

void stats_destroy(GlobalStats *gs)
{
    pthread_mutex_destroy(&gs->lock);
}
