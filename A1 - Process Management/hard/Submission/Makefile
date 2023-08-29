obj-m += final.o 
pwd := $(CURDIR) 
CFLAGS = "-Wno-error=Wframe-larger-than="

all: 
	make -C /lib/modules/$(shell uname -r)/build M=$(pwd) modules 
clean: 
	make -C /lib/modules/$(shell uname -r)/build M=$(pwd) clean