# Firehose telemetry monitor — Raspberry Pi Zero W (ARMv6, native build)
CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -g -pthread
LDFLAGS = -pthread
LDLIBS  = -lwebsockets -lcjson

OBJS = main.o ring_buffer.o producer.o consumer.o counters.o monitor.o

firehose_monitor: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS) $(LDLIBS)

%.o: %.c config.h ring_buffer.h producer.h consumer.h counters.h monitor.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o firehose_monitor

.PHONY: clean
