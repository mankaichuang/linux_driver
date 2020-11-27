KERNELDIR		:= /home/mankc/linux/IMX6LL/linux/nxp_linux
CURRENT_PATH	:= $(shell pwd)

obj-m			:= key.o

build: kernel_modules

kernel_modules:
	$(MAKE) -C $(KERNELDIR) M=$(CURRENT_PATH) modules
clean:
	$(MAKE) -C $(KERNELDIR) M=$(CURRENT_PATH) clean