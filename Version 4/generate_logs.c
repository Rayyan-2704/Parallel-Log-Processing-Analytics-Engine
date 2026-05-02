/*
 * generate_logs.c  —  generates a realistic synthetic log file for testing
 * Usage: ./generate_logs <num_lines> <output_file>
 * Example: ./generate_logs 500000 test.log
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *LEVELS[] = {
    "DEBUG", "DEBUG", "DEBUG",
    "INFO",  "INFO",  "INFO",  "INFO",
    "WARNING", "WARNING",
    "ERROR",
    "CRITICAL"
};
#define N_LEVELS 11

static const char *SOURCES[] = {
    "kernel", "auth", "sshd", "nginx", "mysql",
    "cron", "systemd", "NetworkManager", "app.main", "app.worker"
};
#define N_SOURCES 10

static const char *MESSAGES[] = {
    "Connection established from 192.168.1.%d",
    "User %d logged in successfully",
    "Failed password for user%d from 10.0.0.%d",
    "Disk usage at %d%% on /dev/sda1",
    "Out of memory: Kill process %d",
    "Segmentation fault in process %d",
    "Service %d restarted successfully",
    "Database query took %d ms",
    "SSL certificate will expire in %d days",
    "Rate limit exceeded for client 172.16.%d.%d",
    "Null pointer dereference at address 0x%08x",
    "File descriptor limit reached: %d open files",
    "Backup completed in %d seconds",
    "Thread pool exhausted, queueing request %d",
    "Config reload triggered by SIGHUP",
    "Heap allocation failed for %d bytes",
    "Connection timeout after %d ms from host %d",
    "Authentication token expired for session %d",
    "Critical: watchdog timer expired, restarting subsystem %d",
    "WARNING: swap usage at %d%%"
};
#define N_MSGS 20

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <num_lines> <output_file>\n", argv[0]);
        return 1;
    }

    long n = atol(argv[1]);
    if (n <= 0) { fprintf(stderr, "Error: num_lines must be positive\n"); return 1; }

    FILE *f = fopen(argv[2], "w");
    if (!f) { perror("fopen"); return 1; }

    srand((unsigned)time(NULL));

    time_t base_time = time(NULL) - n;  /* start n seconds ago */

    for (long i = 0; i < n; i++) {
        time_t t = base_time + i;
        struct tm *tm = localtime(&t);
        char ts[32];
        strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", tm);

        const char *level  = LEVELS[rand() % N_LEVELS];
        const char *source = SOURCES[rand() % N_SOURCES];
        const char *msg_fmt= MESSAGES[rand() % N_MSGS];

        int a = rand() % 256;
        int b = rand() % 256;

        char msg[256];
        snprintf(msg, sizeof(msg), msg_fmt, a, b);

        fprintf(f, "%s [%s] %s: %s\n", ts, level, source, msg);
    }

    fclose(f);
    printf("Generated %ld lines -> %s\n", n, argv[2]);
    return 0;
}
