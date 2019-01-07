
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <math.h>
#include <assert.h>
#include "basiclisp.h"
//#include "linenoise/linenoise.h"

typedef struct Context Context;
typedef struct Buffer Buffer;

struct Context {
	Mach m;
	lispref_t buffer_symbol;
	lispref_t len_symbol;
	lispref_t cap_symbol;
};

struct Buffer {
	size_t len;
	size_t cap;
	unsigned char buf[0];
};

static lispref_t
bufget(void *ctx, void *obj, lispref_t lispkey)
{
	Context *c = (Context *)ctx;
	Buffer *buf = (Buffer *)obj;
	if(lispsymbol(&c->m, lispkey)){
		if(lispkey == c->len_symbol){
			return lispint(&c->m, buf->len);
		} else if(lispkey == c->cap_symbol){
			return lispint(&c->m, buf->cap);
		}
	} else if(lispnumber(&c->m, lispkey)){
		size_t i = lispgetint(&c->m, lispkey);
		if(i >= buf->len)
			return LISP_TAG_ERROR;
		return lispint(&c->m, buf->buf[i]);
	}
	return LISP_TAG_ERROR;
}

static lispref_t
bufset(void *ctx, void *obj, lispref_t lispkey, lispref_t lispval)
{
	Context *c = (Context *)ctx;
	Buffer *buf = (Buffer *)obj;
	if(lispsymbol(&c->m, lispkey)){
		if(lispkey == c->len_symbol){
			buf->len = lispgetint(&c->m, lispval);
			return lispval;
		}
	} else if(lispnumber(&c->m, lispkey)){
		size_t i = lispgetint(&c->m, lispkey);
		if(i >= buf->len)
			return LISP_TAG_ERROR;
		unsigned char ch = lispgetint(&c->m, lispval);
		buf->buf[i] = ch;
		return lispval;
	}
	return LISP_TAG_ERROR;
}

static lispref_t
bufnew(void *ctx, void *obj, lispref_t args)
{
	Context *c = (Context *)ctx;
	if(lisppair(&c->m, args) && lispnumber(&c->m, lispcar(&c->m, args))){
		lispref_t lispcap = lispcar(&c->m, args);
		size_t cap = lispgetint(&c->m, lispcap);
		fprintf(stderr, "bufalloc %zu\n", cap);
		Buffer *buf = malloc(sizeof buf[0] + cap);
		if(buf == NULL)
			return LISP_TAG_ERROR;
		memset(buf, 0, sizeof buf[0] + cap);
		buf->cap = cap;
		buf->len = cap;
		lispref_t extref = lispextalloc(&c->m);
		lispextset(&c->m, extref, (void*)buf, bufnew, bufset, bufget);
		return extref;
	}
	return LISP_TAG_ERROR;
}

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
	Context c;

	memset(&c, 0, sizeof c);
	lispinit(&c.m);
	lispsetport(&c.m, 0, (int(*)(int,void*))NULL, (int(*)(void*))getc, (int(*)(int,void*))ungetc, (void*)stdin);
	lispsetport(&c.m, 1, (int(*)(int,void*))putc, (int(*)(void*))NULL, (int(*)(int,void*))NULL, (void*)stdout);

	c.buffer_symbol = lispstrtosymbol(&c.m, "buffer");
	c.len_symbol = lispstrtosymbol(&c.m, "len");
	c.cap_symbol = lispstrtosymbol(&c.m, "cap");
	lispref_t bufclass = lispextalloc(&c.m);

	lispextset(&c.m, bufclass, NULL, bufnew, NULL, NULL);
	lispdefine(&c.m, c.buffer_symbol, bufclass);

	for(size_t i = 1; i < (size_t)argc; i++){
		FILE *fp = fopen(argv[i], "rb");
		if(fp == NULL){
			fprintf(stderr, "cannot open %s\n", argv[i]);
			return 1;
		}
		lispsetport(&c.m, 0, (int(*)(int,void*))NULL, (int(*)(void*))getc, (int(*)(int,void*))ungetc, (void*)fp);
		for(;;){
			c.m.expr = lispparse(&c.m, 1);
			if(lisperror(&c.m, c.m.expr))
				break;
			lispcall(&c.m, LISP_STATE_RETURN, LISP_STATE_EVAL);
			while(lispstep(&c.m) == 1){
				lispref_t first = lispcar(&c.m, c.m.expr);
				if(lispextref(&c.m, first)){
					//printf("call/ext\n");
					lispref_t second = lispcdr(&c.m, c.m.expr);
					void *obj;
					lispapplier_t *apply;
					lispextget(&c.m, first, &obj, &apply, NULL, NULL);
					if(apply == NULL){
						c.m.value = LISP_TAG_ERROR;
					}
					c.m.value = (*apply)(&c, obj, second);
				} else if(lispbuiltin(&c.m, first, LISP_BUILTIN_SET)){
					lispref_t form = lispcdr(&c.m, c.m.expr);
					lispref_t third = lispcdr(&c.m, form);
					form = lispcar(&c.m, form);
					if(form != LISP_NIL){ // should check that it's a cons
						first = lispcar(&c.m, form);
						if(lispnumber(&c.m, first) || lispsymbol(&c.m, first)){
							lispref_t second = lispcdr(&c.m, form);
							if(second != LISP_NIL){
								second = lispcar(&c.m, second);
								third = lispcar(&c.m, third);
								lispsetter_t *set;
								void *obj;
								lispextget(&c.m, second, &obj, NULL, &set, NULL);
								c.m.value = (*set)(&c, obj, first, third);
							} else {
								c.m.value = LISP_TAG_ERROR;
							}
						} else {
							c.m.value = LISP_TAG_ERROR;
						}
					}
				} else if(lispnumber(&c.m, first) || lispsymbol(&c.m, first)){
					lispref_t second = lispcdr(&c.m, c.m.expr);
					if(second != LISP_NIL){
						second = lispcar(&c.m, second);
						lispgetter_t *get;
						void *obj;
						lispextget(&c.m, second, &obj, NULL, NULL, &get);
						c.m.value = (*get)(&c, obj, first);
					} else {
						c.m.value = LISP_TAG_ERROR;
					}
				} else {
					for(lispref_t np = c.m.expr; np != LISP_NIL; np = lispcdr(&c.m, np)){
						printf(" ");
						lispprint1(&c.m, lispcar(&c.m, np), 1);
					}
					printf("\n");
					fprintf(stderr, "extcall: not sure what's going on: %x\n", first);
					c.m.value = LISP_TAG_ERROR;
				}
			}
			c.m.expr = LISP_NIL;
			c.m.value = LISP_NIL;
		}

		fclose(fp);
	}
	return 0;
}