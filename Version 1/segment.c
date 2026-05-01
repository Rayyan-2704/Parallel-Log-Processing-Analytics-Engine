#include "logengine.h"

/*
 * Divide the file into n byte-range segments.
 *
 * Algorithm:
 *   1. Compute evenly-spaced raw byte boundaries.
 *   2. Make a SINGLE forward pass over the file, scanning from byte 0 to EOF.
 *      At each interior boundary we nudge it to the next '\n' (so no log
 *      entry is split across threads) and simultaneously count newlines to
 *      compute the global line offset of every segment.
 *      This replaces the old two-pass approach (boundary scan + newline
 *      count loop) with a single sequential read.
 *
 * Fix applied: seg->fd is NOT stored. The original fd is closed by main.c
 * immediately after this function returns; keeping it in the struct would
 * leave a dangling descriptor. Workers open their own private fds.
 *
 * Returns 0 on success, -1 on error.
 */
int segment_file(const char *path, int n, Segment *segs, int fd)
{
    (void)path; /* workers re-open by name stored in Config */

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

    /* Raw (unaligned) interior boundaries */
    off_t raw[MAX_THREADS + 1];
    raw[0] = 0;
    for (int i = 1; i < n; i++)
        raw[i] = raw[i-1] + chunk;
    raw[n] = file_size;

    /*
     * Single forward pass: walk the file byte by byte (in large chunks).
     * Track:
     *   pos        — current byte offset (1-past the last byte read)
     *   seg_lines  — newlines counted since the last boundary was committed
     *   next_seg   — index of the next interior boundary we need to place
     *
     * When pos crosses raw[next_seg] AND we hit a '\n', that newline's
     * position becomes the aligned boundary for segment next_seg.
     */
    off_t  aligned[MAX_THREADS + 1];
    long   line_offsets[MAX_THREADS + 1];
    aligned[0]      = 0;
    line_offsets[0] = 0;

    if (lseek(fd, 0, SEEK_SET) < 0) { perror("lseek"); return -1; }

    int   next_seg  = 1;
    off_t pos       = 0;
    long  seg_lines = 0;
    char  rbuf[65536];

    while (pos < file_size && next_seg <= n) {
        size_t to_read = sizeof(rbuf);
        if ((off_t)to_read > file_size - pos)
            to_read = (size_t)(file_size - pos);

        ssize_t r = read(fd, rbuf, to_read);
        if (r <= 0) break;

        for (ssize_t j = 0; j < r; j++) {
            pos++;
            if (rbuf[j] == '\n') {
                seg_lines++;
                /*
                 * If pos has passed (or exactly reached) the raw boundary
                 * for next_seg, this newline is the alignment point.
                 * Commit it and advance to the following boundary.
                 */
                while (next_seg <= n - 1 && pos >= raw[next_seg]) {
                    aligned[next_seg]      = pos;
                    line_offsets[next_seg] = line_offsets[next_seg - 1] + seg_lines;
                    seg_lines = 0;
                    next_seg++;
                }
            }
        }
    }

    /* Any remaining interior boundaries that were never committed
     * (file ended before finding their alignment newline). */
    while (next_seg <= n - 1) {
        aligned[next_seg]      = file_size;
        line_offsets[next_seg] = line_offsets[next_seg - 1] + seg_lines;
        seg_lines = 0;
        next_seg++;
    }
    aligned[n]      = file_size;
    line_offsets[n] = line_offsets[n - 1] + seg_lines; /* unused; kept for clarity */

    /* Populate segment descriptors */
    for (int i = 0; i < n; i++) {
        segs[i].thread_id   = i;
        /* fd intentionally NOT stored — caller closes it before workers start */
        segs[i].start       = aligned[i];
        segs[i].end         = aligned[i + 1];
        segs[i].line_offset = line_offsets[i];
    }

    return 0;
}

void segment_free(Segment *segs, int n)
{
    (void)segs; (void)n;
    /* nothing heap-allocated inside Segment */
}
