all:	aesdsocket
aesdsocket : aesdsocket.o
	# $(CROSS_COMPILE)gcc -o aesdsocket aesdsocket.o 
	$(GCC) -o aesdsocket aesdsocket.o 
aesdsocket.o : aesdsocket.c 
	# $(CROSS_COMPILE)gcc -c aesdsocket.c
	$(GCC) -c aesdsocket.c

clean :
	rm -f aesdsocket aesdsocket.o
	
