#include "logengine.h"

void config_defaults(Config *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->num_threads = (int)sysconf(_SC_NPROCESSORS_ONLN);
    if (cfg->num_threads < 1)  cfg->num_threads = 2;
    if (cfg->num_threads > MAX_THREADS) cfg->num_threads = MAX_THREADS;
    cfg->use_ansi    = 1;
    cfg->benchmark   = 0;
    cfg->export_txt  = 0;
    cfg->export_csv  = 0;
    strncpy(cfg->export_path, "report", MAX_FILENAME_LEN - 1);
}

void config_print_usage(const char *prog)
{
    fprintf(stderr,
        "\nUsage: %s [OPTIONS] <logfile>\n\n"
        "Options:\n"
        "  -t <n>           Number of worker threads (default: CPU count)\n"
        "  -p <pattern>     Add a keyword or regex pattern to search (up to %d)\n"
        "  -b               Enable benchmark mode (runs single-thread baseline)\n"
        "  --export <name>  Export reports; generates <name>.txt and <name>.csv\n"
        "  --no-color       Disable ANSI colour output\n"
        "  -h               Show this help\n\n"
        "Examples:\n"
        "  %s -t 8 -b -p ERROR -p 'segfault' --export out system.log\n"
        "  %s -t 4 system.log\n\n",
        prog, MAX_PATTERNS, prog, prog);
}

int config_parse_args(int argc, char **argv, Config *cfg)
{
    if (argc < 2) {
        config_print_usage(argv[0]);
        return -1;
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            config_print_usage(argv[0]);
            return -1;
        } else if (strcmp(argv[i], "-t") == 0) {
            if (++i >= argc) { fprintf(stderr, "Error: -t requires an argument\n"); return -1; }
            cfg->num_threads = atoi(argv[i]);
            if (cfg->num_threads < 1 || cfg->num_threads > MAX_THREADS) {
                fprintf(stderr, "Error: thread count must be 1-%d\n", MAX_THREADS);
                return -1;
            }
        } else if (strcmp(argv[i], "-p") == 0) {
            if (++i >= argc) { fprintf(stderr, "Error: -p requires an argument\n"); return -1; }
            if (cfg->num_patterns >= MAX_PATTERNS) {
                fprintf(stderr, "Warning: max patterns (%d) reached, ignoring '%s'\n",
                        MAX_PATTERNS, argv[i]);
                continue;
            }
            PatternRecord *pr = &cfg->patterns[cfg->num_patterns];
            strncpy(pr->pattern, argv[i], MAX_PATTERN_LEN - 1);
            /* try to compile as regex */
            if (regcomp(&pr->regex, argv[i], REG_EXTENDED | REG_NOSUB | REG_ICASE) == 0) {
                pr->is_regex = 1;
            } else {
                pr->is_regex = 0;
            }
            pr->match_count  = 0;
            pr->first_line   = 0;
            cfg->num_patterns++;
        } else if (strcmp(argv[i], "-b") == 0 ||
                   strcmp(argv[i], "--benchmark") == 0) {
            cfg->benchmark = 1;
        } else if (strcmp(argv[i], "--export") == 0) {
            if (++i >= argc) { fprintf(stderr, "Error: --export requires a filename\n"); return -1; }
            strncpy(cfg->export_path, argv[i], MAX_FILENAME_LEN - 1);
            cfg->export_txt = 1;
            cfg->export_csv = 1;
        } else if (strcmp(argv[i], "--no-color") == 0) {
            cfg->use_ansi = 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Error: unknown option '%s'\n", argv[i]);
            config_print_usage(argv[0]);
            return -1;
        } else {
            /* positional: logfile */
            if (cfg->logfile[0] != '\0') {
                fprintf(stderr, "Error: multiple log files specified\n");
                return -1;
            }
            strncpy(cfg->logfile, argv[i], MAX_FILENAME_LEN - 1);
        }
    }

    if (cfg->logfile[0] == '\0') {
        fprintf(stderr, "Error: no log file specified\n");
        config_print_usage(argv[0]);
        return -1;
    }
    return 0;
}
