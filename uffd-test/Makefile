LDFLAGS = -lpthread
CFLAGS = -g

.PHONY: clean

all: uffd-test uffd-wp-perf

uffd-test: uffd-test.o
uffd-wp-perf: uffd-wp-perf.o

clean:
	@rm -f *.o uffd-test
