obj-m += edu.o

LINUX_ROOT=/root/git/linux

ifndef LINUX_ROOT
LINUX_ROOT=/lib/modules/$(shell uname -r)/build
endif

all:
	make -C $(LINUX_ROOT) M=$(PWD) modules

clean:
	make -C $(LINUX_ROOT) M=$(PWD) clean
