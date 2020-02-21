
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

HFILES=\
	basiclisp.h\

OFILES=\
	main.$(O)\
	basiclisp.$(O)\

basiclisp$(EXE): $(OFILES)
	$(CC) $(LDFLAGS) -o $@ $(OFILES) $(LIBS)

fuzz$(EXE): fuzz.c basiclisp.c
	$(CC) -O2 -fsanitize=address,undefined,fuzzer -o $@ fuzz.c basiclisp.c

test: basiclisp$(EXE)
	./basiclisp$(EXE) stdlib.scm matrix.scm matrix-test.scm test-external.scm

linenoise.$O: linenoise/linenoise.c linenoise/linenoise.h
	$(CC) $(CFLAGS) -c -o $@ linenoise/linenoise.c

clean:
	$(RM) fuzz$(EXE) basiclisp$(EXE) *.$(O)

$(OFILES): $(HFILES)