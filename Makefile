
basiclisp: basiclisp.o
	$(CC) $(LDFLAGS) -o $@ basiclisp.o

clean:
	rm -f basiclisp *.o
