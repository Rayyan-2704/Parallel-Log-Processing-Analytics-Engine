#include "logengine.h"

/*
 * Divide the file into n byte-range segments.
 * Each boundary is nudged forward to the next newline so no log entry
 * is ever split across two threads.
 * Returns 0 on success, -1 on error.
 */
int segment_file(const char *path, int n, Segment *segs, int fd)
{
    struct stat st;
    if (fstat(fd, &st) < 0) {
        perror("fstat");
        return -1;
    }

    off_t file_size = st.st_size;
    if (file_size == 0) {
        fprintf(stderr, "Error: log file is empty\n");
        return -1;
    }

    off_t chunk = file_size / n;

    /* First pass: compute raw start/end for each segment */
    off_t boundaries[MAX_THREADS + 1];
    boundaries[0] = 0;
    for (int i = 1; i < n; i++) {
        boundaries[i] = boundaries[i-1] + chunk;
    }
    boundaries[n] = file_size;

    /* Second pass: align interior boundaries to next newline */
    char buf[1];
    for (int i = 1; i < n; i++) {
        off_t pos = boundaries[i];
        if (lseek(fd, pos, SEEK_SET) < 0) { perror("lseek"); return -1; }
        /* Scan forward until we find a newline */
        while (pos < file_size) {
            ssize_t r = read(fd, buf, 1);
            if (r <= 0) break;
            pos++;
            if (buf[0] == '\n') break;
        }
        boundaries[i] = pos;
    }

    /* Build segment descriptors */
    /* Pre-compute line offsets so each thread knows its global line base */
    /* We do a quick scan to count lines before each boundary */
    /* For large files this is cheap compared to parsing */
    long *line_offsets = calloc(n + 1, sizeof(long));
    if (!line_offsets) { perror("calloc"); return -1; }

    for (int i = 1; i <= n; i++) {
        /* Count newlines between boundaries[i-1] and boundaries[i] */
        off_t pos = boundaries[i-1];
        off_t end = boundaries[i];
        long count = 0;
        char rbuf[65536];
        if (lseek(fd, pos, SEEK_SET) < 0) { perror("lseek"); free(line_offsets); return -1; }
        while (pos < end) {
            size_t to_read = (size_t)(end - pos);
            if (to_read > sizeof(rbuf)) to_read = sizeof(rbuf);
            ssize_t r = read(fd, rbuf, to_read);
            if (r <= 0) break;
            for (ssize_t j = 0; j < r; j++) {
                if (rbuf[j] == '\n') count++;
            }
            pos += r;
        }
        line_offsets[i] = line_offsets[i-1] + count;
    }

    for (int i = 0; i < n; i++) {
        segs[i].thread_id   = i;
        segs[i].fd          = fd;
        segs[i].start       = boundaries[i];
        segs[i].end         = boundaries[i+1];
        segs[i].line_offset = line_offsets[i];
    }

    free(line_offsets);
    return 0;
}

void segment_free(Segment *segs, int n)
{
    (void)segs; (void)n;
    /* descriptors are stack/heap allocated by caller; nothing to free here */
}
