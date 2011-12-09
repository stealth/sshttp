# sshttp Makefile

CXX=c++
CFLAGS=-c -O2 -Wall
CFLAGS+=-DUSE_CAPS
LD=ld
LIBS=-lcap

all: socket.o main.o sshttp.o multicore.o
	$(CXX) *.o $(LIBS) -o sshttpd

clean:
	rm -rf *.o sshttpd


multicore.o: multicore.cc multicore.h
	$(CXX) $(CFLAGS) multicore.cc

sshttp.o: sshttp.cc sshttp.h
	$(CXX) $(CFLAGS) -ansi -pedantic sshttp.cc

main.o: main.cc
	$(CXX) $(CFLAGS) -ansi -pedantic main.cc

socket.o: socket.cc socket.h
	$(CXX) -DLINUX26 $(CFLAGS) -ansi -pedantic socket.cc

