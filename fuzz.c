
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <math.h>
#include <assert.h>
#include "basiclisp.h"

struct slice {
	int unget;
	const uint8_t *buf;
	size_t len;
};

int
slicegetc(struct slice *sl)
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
sliceungetc(int ch, struct slice *sl)
{
	assert(sl->unget == -1);
	sl->unget = ch;
}

int
LLVMFuzzerTestOneInput(const uint8_t *buf, size_t len)
{
	Mach m;
	ref_t list;
	size_t i;

	memset(&m, 0, sizeof m);
	lispinit(&m);

	struct slice sl = { -1, buf, len };
	lispsetport(&m, 0, (int(*)(int,void*))NULL, (int(*)(void*))slicegetc, (int(*)(int,void*))sliceungetc, (void*)&sl);
	m.expr = lispparse(&m, 1);
	if(lisperror(&m, m.expr))
		return -1;
	lispgc(&m);
#if 0
	lispcall(&m, INS_RETURN, INS_EVAL);
	while(lispstep(&m) == 1){
		fprintf(stderr, "call-external: ");
		m.value = lispload(&m, m.expr, 1);
		fprintf(stderr, "\n");
	}
	if(lisperror(&m, m.value))
		return 0;
	m.value = NIL;
	m.expr = NIL;
#endif
	return 0;
}