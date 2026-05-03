CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -D_GNU_SOURCE -g
LDFLAGS = -lpthread -lm

TARGET  = logengine
GEN     = generate_logs

SRCS    = main.c config.c segment.c worker.c stats.c \
          analytics.c benchmark.c progress.c util.c tables.c

OBJS    = $(SRCS:.c=.o)

.PHONY: all clean run demo

all: $(TARGET) $(GEN)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(GEN): generate_logs.c
	$(CC) $(CFLAGS) -o $@ $<

%.o: %.c logengine.h
	$(CC) $(CFLAGS) -c -o $@ $<

# Quick demo: generate 200k lines and run with 4 threads + benchmark
demo: all
	./$(GEN) 200000 demo.log
	./$(TARGET) -t 4 -b \
	    -p ERROR -p CRITICAL -p 'segfault' -p 'Out of memory' \
	    --export demo_report \
	    demo.log

# Run with default thread count, no benchmark
run: all
	./$(TARGET) $(ARGS)

clean:
	rm -f $(OBJS) $(TARGET) $(GEN) *.o demo.log demo_report.*
