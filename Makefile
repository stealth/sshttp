# sshttp Makefile

CFLAGS=-c -O2 -Wall

# On BSD systems you either use gmake or you delete
# the ifeq's and the Linux def part.
#

ifeq ($(shell uname -o), GNU/Linux)
CFLAGS+=-DUSE_CAPS
CFLAGS+=-DLINUX26
LIBS=-lcap
else
CFLAGS+=-DFREEBSD
LIBS=
endif

CXX=c++
LD=ld

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
	$(CXX) $(CFLAGS) -ansi -pedantic socket.cc

