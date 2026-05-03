#include "logengine.h"

int segment_file(const char *path, int n, Segment *segs, int fd)
{
    (void)path;

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

    // Raw (unaligned) interior boundaries
    off_t raw[MAX_THREADS + 1];
    raw[0] = 0;
    for (int i = 1; i < n; i++)
        raw[i] = raw[i-1] + chunk;
    raw[n] = file_size;

    off_t aligned[MAX_THREADS + 1];
    long line_offsets[MAX_THREADS + 1];
    aligned[0] = 0;
    line_offsets[0] = 0;

    if (lseek(fd, 0, SEEK_SET) < 0) {
    	perror("lseek");
    	return -1;
    }

    int next_seg = 1;
    off_t pos = 0;
    long seg_lines = 0;
    char rbuf[65536];

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
                
                while (next_seg <= n - 1 && pos >= raw[next_seg]) {
                    aligned[next_seg] = pos;
                    line_offsets[next_seg] = line_offsets[next_seg - 1] + seg_lines;
                    seg_lines = 0;
                    next_seg++;
                }
            }
        }
    }

    while (next_seg <= n - 1) {
        aligned[next_seg] = file_size;
        line_offsets[next_seg] = line_offsets[next_seg - 1] + seg_lines;
        seg_lines = 0;
        next_seg++;
    }
    aligned[n] = file_size;
    line_offsets[n] = line_offsets[n - 1] + seg_lines;

    // Populate segment descriptors
    for (int i = 0; i < n; i++) {
        segs[i].thread_id = i;
        segs[i].start = aligned[i];
        segs[i].end = aligned[i + 1];
        segs[i].line_offset = line_offsets[i];
    }

    return 0;
}

void segment_free(Segment *segs, int n)
{
    (void)segs;
    (void)n;
}
