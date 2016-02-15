
obj-m += modproclist.o

all:
	gcc -g -Wall -o indexgen indexgen.c -lrt 
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	rm -fr *~   indexgen
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean 
