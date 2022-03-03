CFLAGS := -Wall -Werror -g -O0
CFLAGS += `pkg-config --cflags glib-2.0`
LDFLAGS += `pkg-config --libs glib-2.0`
TARBALL := /tmp/interval-tree.tgz

.PHONY: clean cscope FORCE

all: interval-tree.o tree-test cscope

interval-tree.o: interval-tree.c
	gcc -c $(CFLAGS) -o $@ $^

tree-test: interval-tree.o tree-test.c

$(TARBALL): FORCE
	@make clean
	@rm -f $@
	@tar zcvf $@ .

binrpm-pkg: $(TARBALL)
	rpmbuild -bb interval-tree.spec --define "tarball $(TARBALL)"

cscope:
	@cscope -bq *.[ch]

clean:
	@rm -f *.o tree-test core* cscope* *.tgz
