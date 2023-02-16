# sshttp Makefile

CXX?=c++
CXXSTD?=c++11
CXXFLAGS=-c -O2 -Wall -std=$(CXXSTD) -pedantic

# On BSD systems you either use gmake or you delete
# the ifeq's and the Linux def part.
#

# These defs have only relevance if you use sshttp as a
# SMTP/SSH multiplexer. The SSH_BANNER _must match exactly_
# what your real ssh server tells you, otherwise the ssh client
# will detect the handshake as tempered, and will abort the
# connection. Newlines will be added by sshttpd itself.
SMTP_DOMAIN=-DSMTP_DOMAIN=\"example.com\"
SSH_BANNER=-DSSH_BANNER=\"SSH-2.0-OpenSSH_5.8\"


ifeq ($(shell uname -o), GNU/Linux)
CXXFLAGS+=-DUSE_CAPS
CXXFLAGS+=-DLINUX26
LIBS=-lcap
else
CXXFLAGS+=-DFREEBSD
LIBS=
endif

LD=ld

all: socket.o main.o sshttp.o multicore.o
	$(CXX) *.o -o sshttpd $(LIBS)

clean:
	rm -rf *.o sshttpd


multicore.o: multicore.cc multicore.h
	$(CXX) $(CXXFLAGS) multicore.cc

sshttp.o: sshttp.cc sshttp.h
	$(CXX) $(CXXFLAGS) $(SMTP_DOMAIN) $(SSH_BANNER) sshttp.cc

main.o: main.cc
	$(CXX) $(CXXFLAGS) main.cc

socket.o: socket.cc socket.h
	$(CXX) $(CXXFLAGS) socket.cc

