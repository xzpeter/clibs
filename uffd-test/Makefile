LDFLAGS = -lpthread
CFLAGS = -g

.PHONY: clean

all: uffd-test uffd-wp-perf uffd-poison

uffd-test: uffd-test.o
uffd-wp-perf: uffd-wp-perf.o
uffd-poison: uffd-poison.o libuffd.o

clean:
	@rm -f *.o uffd-test
