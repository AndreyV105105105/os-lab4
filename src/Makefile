CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -O2 -D_WIN32_WINNT=0x0600
OBJS = common.o ipc.o

all: parent.exe child.exe

parent.exe: parent.o $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

child.exe: child.o $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c common.h ipc.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	$(RM) *.o *.exe

.PHONY: all clean
