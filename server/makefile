# GCC ?= gcc
CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?=
TARGET ?= aesdsocket
LDFLAGS ?= -lpthread -lrt 

all:	$(TARGET)
$(TARGET) : $(TARGET).o
# $(CROSS_COMPILE)gcc -o aesdsocket aesdsocket.o 
# $(GCC) -o aesdsocket aesdsocket.o 
	$(CC) -o $(TARGET) $(LDFLAGS) $(TARGET).o 
$(TARGET).o : $(TARGET).c 
# $(CROSS_COMPILE)gcc -c aesdsocket.c
	$(CC) -c $(CFLAGS) $(TARGET).c

clean :
	rm -f $(TARGET) $(TARGET).o
	
