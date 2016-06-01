obj-m := sys_mycall.o
KID := /home/mihawk/GitHub/Android_scheduler/kernel/goldfish
CROSS_COMPILE = arm-linux-androideabi-
CC = $(CROSS_COMPILE)gcc
LD = $(CROSS_COMPILE)ld

all:
	make -C $(KID) ARCH=arm CROSS_COMPILE=$(CROSS_COMPILE) M=$(shell pwd) modules

clean:
	rm -rf *.ko *.o *.mod.c *.order *.symvers *.ko.mod *.mod.o.cmd *.c~
