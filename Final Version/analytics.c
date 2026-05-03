#include "logengine.h"

/* helper functions */
static void print_divider(int width, const char *color)
{
    printf("%s", color);
    for (int i = 0; i < width; i++) fputs("\xe2\x94\x80", stdout);
    printf(ANSI_RESET "\n");
}

static void print_header(const char *title, int width)
{
    int pad = (width - (int)strlen(title) - 2) / 2;
    printf(ANSI_BOLD ANSI_CYAN);
    for (int i = 0; i < pad; i++)
    	fputs("\xe2\x94\x80", stdout);
    	
    printf("  %s  ", title);
    
    for (int i = 0; i < pad; i++)
    	fputs("\xe2\x94\x80", stdout);
    	
    printf(ANSI_RESET "\n");
}

/* terminal analytics panel */

void analytics_render(const GlobalStats *gs, const Config *cfg, const BenchResult *bench, double elapsed_sec, long file_bytes)
{
    const int W = 68;

    printf("\n");
    print_header("PARALLEL LOG ANALYTICS ENGINE", W);
    printf("\n");

    // Summary
    printf(ANSI_BOLD "  %-22s" ANSI_RESET " %ld\n", "Total lines parsed:", gs->total_lines);
    printf(ANSI_BOLD "  %-22s" ANSI_RESET " %d\n",  "Worker threads:", cfg->num_threads);
    printf(ANSI_BOLD "  %-22s" ANSI_RESET " %.3f s\n","Wall-clock time:", elapsed_sec);
    printf(ANSI_BOLD "  %-22s" ANSI_RESET " %.0f lines/s\n", "Throughput:", elapsed_sec > 0 ? (double)gs->total_lines / elapsed_sec : 0.0);
    printf(ANSI_BOLD "  %-22s" ANSI_RESET " %.2f MB\n", "File size:", (double)file_bytes / (1024.0 * 1024.0));
    printf("\n");
    print_divider(W, ANSI_DIM);

    // Severity breakdown
    printf("\n" ANSI_BOLD "  SEVERITY BREAKDOWN\n" ANSI_RESET "\n");
    long total = gs->total_lines > 0 ? gs->total_lines : 1;
    for (int s = 0; s < SEV_COUNT; s++) {
        long cnt = gs->sev_counts[s];
        double pct = 100.0 * cnt / total;
        printf("  %s%-9s" ANSI_RESET " %7ld  (%5.1f%%)  [", cfg->use_ansi ? SEV_COLORS[s] : "", SEV_NAMES[s], cnt, pct);
        util_print_bar((int)cnt, (int)total, 28, cfg->use_ansi ? SEV_COLORS[s] : "");
        printf("]\n");
    }
    printf("\n");
    print_divider(W, ANSI_DIM);

    // Pattern matches
    if (cfg->num_patterns > 0) {
        printf("\n" ANSI_BOLD "  PATTERN MATCHES\n" ANSI_RESET "\n");
        for (int p = 0; p < cfg->num_patterns; p++) {
            printf("  " ANSI_YELLOW "%-28s" ANSI_RESET " %7ld match%s", cfg->patterns[p].pattern, gs->pattern_matches[p], gs->pattern_matches[p] == 1 ? " " : "es");
            if (gs->pattern_first_line[p] > 0) {
                printf("  (first at line %ld)", gs->pattern_first_line[p]);
            }
            else {
                printf("  (no match)");
            }
            printf("\n");
        }
        printf("\n");
        print_divider(W, ANSI_DIM);
    }

    // Benchmark comparison
    if (cfg->benchmark && bench) {
        printf("\n" ANSI_BOLD "  BENCHMARK: SINGLE vs MULTI-THREAD\n" ANSI_RESET "\n");
        printf("  %-28s  %9.3f s  (%7.0f lines/s)\n", "Single-thread baseline:", bench->single_thread_sec, bench->st_throughput);
        printf("  %-28s  %9.3f s  (%7.0f lines/s)\n", "Multi-thread engine:", bench->multi_thread_sec, bench->mt_throughput);
        printf("\n");
        printf(ANSI_BOLD "  Speedup factor: " ANSI_GREEN "%.2fx" ANSI_RESET "\n", bench->speedup);

        // Speedup bar
        printf("  Single  [");
        util_print_bar(1, (int)(bench->speedup + 0.5), 36, ANSI_DIM);
        printf("]\n  Multi   [");
        util_print_bar((int)(bench->speedup + 0.5), (int)(bench->speedup + 0.5), 36, ANSI_GREEN);
        printf("]\n\n");
        print_divider(W, ANSI_DIM);
    }

    printf("\n");
}


/* plain-text export */

void analytics_export_txt(const GlobalStats *gs, const Config *cfg, const BenchResult *bench, double elapsed_sec)
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
        fprintf(f, "  %-9s : %ld (%.1f%%)\n", SEV_NAMES[s], gs->sev_counts[s], 100.0 * gs->sev_counts[s] / total);
    }

    if (cfg->num_patterns > 0) {
        fprintf(f, "\n--- Pattern Matches ---\n");
        for (int p = 0; p < cfg->num_patterns; p++) {
            fprintf(f, "  %-30s : %ld match%s", cfg->patterns[p].pattern, gs->pattern_matches[p], gs->pattern_matches[p] == 1 ? "" : "es");
            if (gs->pattern_first_line[p] > 0)
                fprintf(f, "  (first at line %ld)", gs->pattern_first_line[p]);
            fprintf(f, "\n");
        }
    }

    if (cfg->benchmark && bench) {
        fprintf(f, "\n--- Benchmark ---\n");
        fprintf(f, "  Single-thread : %.4f s  (%.0f lines/s)\n", bench->single_thread_sec, bench->st_throughput);
        fprintf(f, "  Multi-thread  : %.4f s  (%.0f lines/s)\n",  bench->multi_thread_sec, bench->mt_throughput);
        fprintf(f, "  Speedup       : %.2fx\n", bench->speedup);
    }

    fclose(f);
    printf(ANSI_GREEN "  Report saved  → %s\n" ANSI_RESET, path);
}

/* CSV export */

void analytics_export_csv(const GlobalStats *gs, const Config *cfg, const BenchResult *bench)
{
    char path[MAX_FILENAME_LEN + 8];
    snprintf(path, sizeof(path), "%s.csv", cfg->export_path);

    FILE *f = fopen(path, "w");
    if (!f) { perror("export csv fopen"); return; }

    // Severity section
    fprintf(f, "section,key,value\n");
    fprintf(f, "summary,logfile,%s\n", cfg->logfile);
    fprintf(f, "summary,threads,%d\n", cfg->num_threads);
    fprintf(f, "summary,total_lines,%ld\n", gs->total_lines);

    for (int s = 0; s < SEV_COUNT; s++) {
        fprintf(f, "severity,%s,%ld\n", SEV_NAMES[s], gs->sev_counts[s]);
    }

    for (int p = 0; p < cfg->num_patterns; p++) {
        fprintf(f, "pattern,%s,%ld\n", cfg->patterns[p].pattern, gs->pattern_matches[p]);
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

/* Gnuplot chart generation */

/*
 * analytics_plot() generates two publication-quality PNG charts:
 *
 *   <export_path>_severity.png  — horizontal bar chart of line counts per
 *                                  severity level, colour-coded to match the
 *                                  terminal panel.
 *   <export_path>_speedup.png   — grouped bar chart comparing single-thread
 *                                  vs multi-thread throughput (only when -b).
 *
 * Implementation:
 *   1. Write a gnuplot script to a temp file (<export_path>_severity.gp /
 *      <export_path>_speedup.gp).
 *   2. Call system("gnuplot <script>") — gnuplot reads the script and writes
 *      the PNG directly, with no X display required (pngcairo terminal).
 *   3. Remove the temp script files on success.
 *
 * If gnuplot is not installed, a clear warning is printed and the function
 * returns gracefully without touching any other output.
 */
void analytics_plot(const GlobalStats *gs, const Config *cfg, const BenchResult *bench)
{
    /* ── Probe for gnuplot ──────────────────────────────── */
    if (system("gnuplot --version > /dev/null 2>&1") != 0) {
        fprintf(stderr,
                ANSI_YELLOW "  Warning: gnuplot not found — skipping chart "
                "generation. Install with: sudo apt install gnuplot\n"
                ANSI_RESET);
        return;
    }

    char gp_path[MAX_FILENAME_LEN + 32];
    char png_path[MAX_FILENAME_LEN + 32];
    char cmd[MAX_FILENAME_LEN + 64];

    /* Chart 1: Severity breakdown horizontal bar chart */
    snprintf(gp_path,  sizeof(gp_path),  "%s_severity.gp",  cfg->export_path);
    snprintf(png_path, sizeof(png_path), "%s_severity.png", cfg->export_path);

    FILE *gp = fopen(gp_path, "w");
    if (!gp) { perror("plot: fopen severity.gp"); return; }

    /*
     * Gnuplot script — severity bar chart.
     *
     * Compatibility: written for gnuplot 4.6+ (avoids 'set horizontal',
     * '$DATA' named blocks, and 'boxxyerror' which require 5.0+).
     *
     * Approach: vertical bars with rotated x-axis labels, one 'boxes' plot
     * per severity level so each can have its own colour. Data is written
     * to a temporary inline data file embedded in the script via the
     * classic '-' (stdin) mechanism — compatible with all gnuplot versions.
     *
     * Colours match the terminal ANSI panel:
     *   DEBUG=cyan, INFO=green, WARNING=yellow, ERROR=red,
     *   CRITICAL=magenta, UNKNOWN=grey.
     */

    static const char *SEV_GP_COLORS[SEV_COUNT] = {
        "#00bcd4",   // DEBUG
        "#4caf50",   // INFO
        "#e6b800",   // WARNING  (darker yellow — visible on white bg)
        "#f44336",   // ERROR
        "#9c27b0",   // CRITICAL
        "#9e9e9e"    // UNKNOWN
    };

    // Header and layout
    fprintf(gp,
        "set terminal pngcairo enhanced font 'Helvetica,11' size 860,480\n"
        "set output '%s'\n"
        "\n"
        "set title 'Log Severity Breakdown\\n{/:Italic %s}'"
        " font 'Helvetica-Bold,13'\n"
        "set ylabel 'Number of Lines'\n"
        "set xlabel ''\n"
        "\n"
        "set style fill solid 0.85 border -1\n"
        "set boxwidth 0.6\n"
        "set yrange [0:*]\n"
        "set xrange [-0.5:%d.5]\n"
        "set xtics nomirror rotate by -30 scale 0\n"
        "set ytics nomirror\n"
        "set grid ytics lc rgb '#cccccc' lw 1\n"
        "set border 3\n"
        "set key off\n"
        "\n"
        // x-axis tick labels: severity names at integer positions
        "set xtics (", png_path, cfg->logfile, SEV_COUNT - 1);

    for (int s = 0; s < SEV_COUNT; s++) {
        fprintf(gp, "\"%s\" %d%s",
                SEV_NAMES[s], s, s < SEV_COUNT - 1 ? ", " : "");
    }
    fprintf(gp, ")\n\n");

    /* Each bar is its own 'plot' or 'replot' call so it gets a distinct
     	colour.  We use a single-point inline data block ('-' ... 'e') with
     	'using 1:2' where col1=x-position and col2=count.
     	Value labels: a matching 'with labels' pass prints the count centred
     	just above the top of each bar (offset 0,1 in character units).
     */
    fprintf(gp, "plot \\\n");
    for (int s = 0; s < SEV_COUNT; s++) {
        // bar
        fprintf(gp,
            "  '-' using 1:2 with boxes lc rgb '%s' notitle, \\\n"
            "  '-' using 1:2:(sprintf('%%g',$2)) with labels"
            " offset 0,0.5 center font 'Helvetica,9'"
            " tc rgb '#333333' notitle%s\n",
            SEV_GP_COLORS[s],
            s < SEV_COUNT - 1 ? ", \\" : "");
    }

    // Inline data blocks: two per severity (box + label), same values
    fprintf(gp, "\n");
    for (int s = 0; s < SEV_COUNT; s++) {
        // data for the box
        fprintf(gp, "%d %ld\ne\n", s, gs->sev_counts[s]);
        // data for the label
        fprintf(gp, "%d %ld\ne\n", s, gs->sev_counts[s]);
    }

    fclose(gp);

    snprintf(cmd, sizeof(cmd), "gnuplot \"%s\"", gp_path);
    if (system(cmd) == 0) {
        printf(ANSI_GREEN "  Chart saved   → %s\n" ANSI_RESET, png_path);
        remove(gp_path);   /* clean up temp script */
    }
    else {
        fprintf(stderr, ANSI_RED
                "  Error: gnuplot failed on severity chart — "
                "script left at %s for inspection\n" ANSI_RESET, gp_path);
    }

    /* Chart 2: Single-thread vs Multi-thread throughput
	(only generated when benchmark mode was run) */
    if (!cfg->benchmark || !bench || bench->single_thread_sec <= 0.0)
        return;

    snprintf(gp_path,  sizeof(gp_path),  "%s_speedup.gp",  cfg->export_path);
    snprintf(png_path, sizeof(png_path), "%s_speedup.png", cfg->export_path);

    gp = fopen(gp_path, "w");
    if (!gp) { perror("plot: fopen speedup.gp"); return; }

    /* Grouped bar chart: two bars side by side.
       Left bar  = single-thread throughput (lines/sec)
       Right bar = multi-thread throughput  (lines/sec)
       A second y-axis panel (inset or annotation) shows the speedup factor. */
    fprintf(gp,
        "set terminal pngcairo enhanced font 'Helvetica,11' size 800,500\n"
        "set output '%s'\n"
        "\n"
        "set title 'Throughput: Single-thread vs %d Threads\\n"
        "{/:Italic Speedup factor: %.2fx}' font 'Helvetica-Bold,13'\n"
        "\n"
        "set ylabel 'Throughput (lines / second)'\n"
        "set yrange [0:*]\n"
        "set xrange [-0.5:1.5]\n"
        "set xtics ('Single-thread' 0, 'Multi-thread (%d)' 1) nomirror\n"
        "set ytics nomirror format '%%.0f'\n"
        "set grid ytics lc rgb '#cccccc' lw 1\n"
        "set border 3\n"
        "set key off\n"
        "set boxwidth 0.5\n"
        "set style fill solid 0.85 border -1\n"
        "\n"
        /* Bake the label strings in C so gnuplot never needs sprintf.
         * gnuplot's sprintf rejects %ld and trailing literals after %f
         * (e.g. '%.2fx speedup' is invalid). Compute the strings in C
         * and write them as quoted literals into the script. */
        "set label 1 '%.0f' at 0, %.0f*1.02 center font 'Helvetica,10'\n"
        "set label 2 '%.0f' at 1, %.0f*1.02 center font 'Helvetica,10'\n"
        "\n"
        /* Speedup annotation: '2.74x speedup' as a plain quoted string */
        "set label 3 '%.2fx speedup' "
        "at 0.5, %.0f*0.85 center font 'Helvetica-Bold,11' tc rgb '#1565c0'\n"
        "set arrow 1 from 0.25, %.0f to 0.75, %.0f lw 2 lc rgb '#1565c0' filled\n"
        "\n"
        /* Use '-' inline data — compatible with gnuplot 4.6+.
         * ($DATA << EOD named blocks require gnuplot 5.0+.) */
        "plot '-' using 1:2:(0.5) with boxes lc rgb '#5c6bc0' notitle\n"
        "0 %.0f\n"
        "1 %.0f\n"
        "e\n",

        /* output path */      png_path,
        /* title */            cfg->num_threads, bench->speedup,
        /* xtic label */       cfg->num_threads,
        /* label 1 */          bench->st_throughput, bench->st_throughput,
        /* label 2 */          bench->mt_throughput, bench->mt_throughput,
        /* speedup annotation */ bench->speedup,
                               bench->mt_throughput > bench->st_throughput
                                   ? bench->mt_throughput : bench->st_throughput,
        /* arrow from st→mt */ bench->st_throughput, bench->mt_throughput,
        /* data */             bench->st_throughput, bench->mt_throughput
    );

    fclose(gp);

    snprintf(cmd, sizeof(cmd), "gnuplot \"%s\"", gp_path);
    if (system(cmd) == 0) {
        printf(ANSI_GREEN "  Chart saved   → %s\n" ANSI_RESET, png_path);
        remove(gp_path);
    } 
    else {
        fprintf(stderr, ANSI_RED
                "  Error: gnuplot failed on speedup chart — "
                "script left at %s for inspection\n" ANSI_RESET, gp_path);
    }
}
