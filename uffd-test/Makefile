LDFLAGS = -lpthread
CFLAGS = -g

.PHONY: clean

all: uffd-test uffd-wp-perf uffd-poison uffd-wp-async

uffd-test: uffd-test.o
uffd-wp-perf: uffd-wp-perf.o
uffd-poison: uffd-poison.o libuffd.o
uffd-wp-async: uffd-wp-async.o libuffd.o libpagemap.o

clean:
	@rm -f *.o uffd-test uffd-wp-perf uffd-poison uffd-wp-async
