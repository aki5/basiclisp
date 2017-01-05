
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

basiclisp$(EXE): $(OFILES)
	$(CC) $(LDFLAGS) -o $@ $(OFILES) $(LIBS)

clean:
	$(RM) basiclisp$(EXE) *.$(O)
