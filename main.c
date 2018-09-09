
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <math.h>
#include <assert.h>
#include "basiclisp.h"
//#include "linenoise/linenoise.h"

struct slice {
	int unget;
	char *buf;
	size_t len;
};

int
slice_readbyte(struct slice *sl)
{
	if(sl->unget != -1){
		int ch = sl->unget;
		sl->unget = -1;
		return ch;
	}
	if(sl->len == 0)
		return -1;
	int ch = sl->buf[0];
	sl->len--;
	sl->buf++;
	return ch;
}

void
slice_unreadbyte(int ch, struct slice *sl)
{
	assert(sl->unget == -1);
	sl->unget = ch;
}

int
main(int argc, char *argv[])
{
	Mach m;
	lispref_t list;
	size_t i;

	memset(&m, 0, sizeof m);
	lispinit(&m);
	lispsetport(&m, 0, (int(*)(int,void*))NULL, (int(*)(void*))getc, (int(*)(int,void*))ungetc, (void*)stdin);
	lispsetport(&m, 1, (int(*)(int,void*))putc, (int(*)(void*))NULL, (int(*)(int,void*))NULL, (void*)stdout);

	for(i = 1; i < (size_t)argc; i++){
		FILE *fp = fopen(argv[i], "rb");
		if(fp == NULL){
			fprintf(stderr, "cannot open %s\n", argv[i]);
			return 1;
		}
		lispsetport(&m, 0, (int(*)(int,void*))NULL, (int(*)(void*))getc, (int(*)(int,void*))ungetc, (void*)fp);
		for(;;){
			m.expr = lispparse(&m, 1);
			if(lisperror(&m, m.expr))
				break;
			lispcall(&m, LISP_STATE_RETURN, LISP_STATE_EVAL);
			while(lispstep(&m) == 1)
				;
			m.expr = LISP_NIL;
			m.value = LISP_NIL;
		}

		fclose(fp);
	}

#if 0
	if(argc == 1){
		for(;;){
			printf("> "); fflush(stdout);
			lispsetport(&m, 0, (int(*)(int,void*))putc, (int(*)(void*))_readbyte, (int(*)(int,void*))un_readbyte, (void*)stdin);
			m.expr = lispparse(&m, 1);
			if(lisperror(&m, m.expr))
				break;
			lispcall(&m, LISP_STATE_RETURN, LISP_STATE_EVAL);
			while(lispstep(&m) == 1){
				fprintf(stderr, "call-external: ");
				m.value = lispload(&m, m.expr, 1);
				fprintf(stderr, "\n");
			}
			if(lisperror(&m, m.value))
				break;
			m.value = LISP_NIL;
			m.expr = LISP_NIL;
		}
	}

	for(;;){
		char *ln = linenoise("> ");
		if(ln == NULL){
			fprintf(stderr, "bye!\n");
			break;
		}
		linenoiseHistoryAdd(ln);

		struct slice sl = { -1, ln, strlen(ln) };
		lispsetport(&m, 0, (int(*)(int,void*))NULL, (int(*)(void*))slice_readbyte, (int(*)(int,void*))slice_unreadbyte, (void*)&sl);
		m.expr = lispparse(&m, 1);
		if(lisperror(&m, m.expr))
			break;
		linenoiseFree(ln);

		lispcall(&m, LISP_STATE_RETURN, LISP_STATE_EVAL);
		while(lispstep(&m) == 1){
			fprintf(stderr, "call-external: ");
			m.value = lispload(&m, m.expr, 1);
			fprintf(stderr, "\n");
		}
		if(lisperror(&m, m.value))
			break;
		m.value = LISP_NIL;
		m.expr = LISP_NIL;
	}
#endif

	return 0;
}