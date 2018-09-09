
# the following is truly awful, but it allows having
# one makefile for both win32 and unix \
!ifndef 0 # \
RM=del # \
O=obj # \
EXE=.exe # \
!else
RM=rm -f
O=o
EXE=
LIBS=-lm
# \
!endif

OFILES=\
	basiclisp.$(O)\

basiclisp$(EXE): main.$(O) $(OFILES)
	$(CC) $(LDFLAGS) -o $@ main.$(O) $(OFILES) $(LIBS)

fuzz$(EXE): fuzz.c basiclisp.c
	$(CC) -O2 -fsanitize=address,undefined,fuzzer -o $@ fuzz.c basiclisp.c

linenoise.$O: linenoise/linenoise.c linenoise/linenoise.h
	$(CC) $(CFLAGS) -c -o $@ linenoise/linenoise.c

clean:
	$(RM) fuzz$(EXE) basiclisp$(EXE) *.$(O)
