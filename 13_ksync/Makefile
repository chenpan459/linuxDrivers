ifneq ($(KERNELRELEASE),)
#obj-m += wait_event_interruptible_hrtimer.o
#obj-m += wait_event_interruptible_timeout.o
#obj-m += wait_event_interruptible.o
#obj-m += wait_for_completion_interruptible_timeout.o
obj-m += semaphore.o
obj-m += spinlock.o
obj-m += mutex_unlock.o
obj-m += atomic.o
obj-m += rwlock.o
obj-m += barrier.o

else
PWD := $(shell pwd)
#KDIR:=/home/jinxin/linux-4.9.229
#KDIR:= /lib/modules/4.4.0-31-generic/build
KDIR := /lib/modules/`uname -r`/build
all:
	make -C $(KDIR) M=$(PWD)
clean:	
	rm -rf *.o *.ko *.mod.c *.symvers *.c~ *~
endif
