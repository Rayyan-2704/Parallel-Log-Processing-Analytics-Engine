# Parallel Log Processing & Analytics Engine
**CS-2006 Operating Systems — Project 7**

| Member | ID | Role |
|---|---|---|
| Rayyan Aamir | 24K-0687 | Lead Developer |
| Muhammad Usaid Khan | 24K-0832 | Sync & Stats |
| Muhammad Ahmed Shah Rashdi | 24K-0709 | Analytics & Output |

Platform: **Linux Ubuntu 24.04.3 LTS** | Language: **C (POSIX Threads, Semaphores, Mutexes)**

---

## Architecture Overview

```
main.c          — Entry point; orchestrates the full pipeline
config.c        — CLI argument parsing
segment.c       — Byte-range file segmentation (newline-aligned)
worker.c        — POSIX pthread worker: reads, parses, classifies
stats.c         — GlobalStats with pthread_mutex_t; two-phase merge
benchmark.c     — Single-thread baseline for speedup comparison
analytics.c     — ANSI terminal panel + .txt/.csv export
progress.c      — Background thread: live progress bar
util.c          — Severity parser, timing, ASCII bar charts
tables.c        — SEV_NAMES / SEV_COLORS string tables
generate_logs.c — Synthetic log file generator (testing)
```

### Execution Pipeline

```
Stage 1: Log Ingestion & Segmentation
  open() → fstat() → divide into N byte-range segments
  → scan backward from each boundary to align on '\n'

Stage 2: Concurrent Parsing  (N pthreads in parallel)
  sem_wait(io_sem)       ← limits simultaneous disk readers
  open() + lseek() + read() own fd per thread
  sem_post(io_sem)       ← release I/O slot after read
  parse lines → classify severity → match patterns (local buffers)

Stage 3: Synchronised Aggregation
  pthread_mutex_lock(&global.lock)
  merge local counters → global stats
  pthread_mutex_unlock(&global.lock)

Stage 4: Analytics & Reporting
  render ANSI panel → optionally export .txt / .csv
```

---

## Build

```bash
make          # builds ./logengine and ./generate_logs
make demo     # generates 200k-line demo.log and runs with -t 4 -b
make clean
```

Requirements: `gcc`, `make`, `libpthread` (standard on Ubuntu).

---

## Usage

```
./logengine [OPTIONS] <logfile>

Options:
  -t <n>          Worker threads (default: nproc)
  -p <pattern>    Pattern/regex to search (up to 16)
  -b              Benchmark mode (run single-thread baseline first)
  --export <name> Export <name>.txt and <name>.csv
  --no-color      Plain output (no ANSI codes)
  -h              Help
```

### Examples

```bash
# Minimal run
./logengine system.log

# 8 threads, benchmark, two patterns, export reports
./logengine -t 8 -b -p ERROR -p 'Out of memory' --export report system.log

# Regex patterns
./logengine -t 4 -p '(CRIT|ERROR)' -p '192\.168\.' app.log

# Generate a test file first
./generate_logs 1000000 big.log
./logengine -t 8 -b --export results big.log
```

---

## POSIX Synchronisation Details

| Primitive | Where used | Purpose |
|---|---|---|
| `pthread_mutex_t` | `stats.c` | Protect global statistics during merge |
| `sem_t` (unnamed) | `worker.c` | Cap simultaneous disk readers to `IO_CONCURRENCY` (4) |
| `pthread_create` / `pthread_join` | `main.c` | Worker thread lifecycle |
| `atomic_fetch_add` (C11) | `worker.c` | Lock-free progress counter increment |
| `clock_gettime(CLOCK_MONOTONIC)` | `util.c` | High-resolution wall-clock timing |

### Two-Phase Merge Strategy
Each worker thread accumulates counts into a **thread-local** `LocalStats` struct during its entire parse pass — no locking during parsing. Only when the parse is complete does the thread acquire `GlobalStats.lock` once to merge its counters. This minimises lock contention and maximises parallel throughput.

---

## Output

The terminal panel shows:
- Total lines, threads, wall-clock time, throughput, file size
- Severity breakdown with ANSI bar charts (DEBUG / INFO / WARNING / ERROR / CRITICAL)
- Per-pattern match counts and first-occurrence line number
- Benchmark comparison table with speedup factor (if `-b` used)

Exported files (when `--export` is used):
- `<name>.txt` — human-readable plain text report
- `<name>.csv` — machine-readable CSV for further analysis

---

## References

1. Kerrisk, M. (2010). *The Linux Programming Interface*. Chapters 29–33.
2. IEEE Std 1003.1 (POSIX.1-2017). `pthreads(7)`, `sem_overview(7)`.
3. Silberschatz et al. (2018). *Operating System Concepts*, 10th ed. Chapters 4 & 6.
4. GNU C Library Manual. POSIX Semaphores, File I/O, `regex.h`.
5. Butenhof, D. R. (1997). *Programming with POSIX Threads*.
