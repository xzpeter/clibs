LDFLAGS = -lpthread
CFLAGS = -g

.PHONY: clean

all: uffd-test uffd-remap-stall-flag uffd-hugetlb-share-pmd

uffd-test: uffd-test.o

uffd-remap-stall-flag: uffd-remap-stall-flag.o

uffd-hugetlb-share-pmd: uffd-hugetlb-share-pmd.o

clean:
	@rm -f *.o uffd-test uffd-remap-stall-flag uffd-hugetlb-share-pmd
