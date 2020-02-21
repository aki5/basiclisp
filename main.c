
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
typedef struct Type Type;

struct Type {
	lispapplier_t *apply;
	lispsetter_t *set;
	lispgetter_t *get;
};

struct Context {
	Mach m;
	lispref_t buffer_symbol;
	lispref_t len_symbol;
	lispref_t cap_symbol;
	Type bufclass;
	Type buftype;
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
			return lispmknumber(&c->m, buf->len);
		} else if(lispkey == c->cap_symbol){
			return lispmknumber(&c->m, buf->cap);
		}
	} else if(lispnumber(&c->m, lispkey)){
		size_t i = lispgetint(&c->m, lispkey);
		if(i >= buf->len)
			return lispmkbuiltin(&c->m, LISP_BUILTIN_ERROR);
		return lispmknumber(&c->m, buf->buf[i]);
	}
	return lispmkbuiltin(&c->m, LISP_BUILTIN_ERROR);
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
			return lispmkbuiltin(&c->m, LISP_BUILTIN_ERROR);
		unsigned char ch = lispgetint(&c->m, lispval);
		buf->buf[i] = ch;
		return lispval;
	}
	return lispmkbuiltin(&c->m, LISP_BUILTIN_ERROR);
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
			return lispmkbuiltin(&c->m, LISP_BUILTIN_ERROR);
		memset(buf, 0, sizeof buf[0] + cap);
		buf->cap = cap;
		buf->len = cap;
		lispref_t extref = lispextalloc(&c->m);
		lispextset(&c->m, extref, (void*)buf, &c->buftype);
		return extref;
	}
	return lispmkbuiltin(&c->m, LISP_BUILTIN_ERROR);
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

	c.bufclass.apply = bufnew;
	c.buftype.set = bufset;
	c.buftype.get = bufget;

	c.buffer_symbol = lispmksymbol(&c.m, "buffer");
	c.len_symbol = lispmksymbol(&c.m, "len");
	c.cap_symbol = lispmksymbol(&c.m, "cap");
	lispref_t bufclass_ref = lispextalloc(&c.m);

	lispextset(&c.m, bufclass_ref, NULL, &c.bufclass);
	lispdefine(&c.m, c.buffer_symbol, bufclass_ref);

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
				c.m.value = lispmkbuiltin(&c.m, LISP_BUILTIN_ERROR); // default to error up front.
				lispref_t first = lispcar(&c.m, c.m.expr);
				if(lispextref(&c.m, first)){
					// first element is an extref: it's an apply
					lispref_t second = lispcdr(&c.m, c.m.expr);
					void *obj;
					Type *type;
					lispextget(&c.m, first, &obj, (void**)&type);
					if(type != NULL && type->apply != NULL)
						c.m.value = (*type->apply)(&c, obj, second);
				} else if(lispbuiltin(&c.m, first, LISP_BUILTIN_SET)){
					// it's a set.. ensure form is (set! ('prop extref) value)
					// and call setter.
					lispref_t form = lispcdr(&c.m, c.m.expr);
					lispref_t third = lispcdr(&c.m, form);
					form = lispcar(&c.m, form);
					if(lisppair(&c.m, form)){
						first = lispcar(&c.m, form);
						if(lispnumber(&c.m, first) || lispsymbol(&c.m, first)){
							lispref_t second = lispcdr(&c.m, form);
							if(lisppair(&c.m, second) && lisppair(&c.m, third)){
								second = lispcar(&c.m, second);
								if(lispextref(&c.m, second)){
									third = lispcar(&c.m, third);
									void *obj;
									Type *type;
									lispextget(&c.m, second, &obj, (void**)&type);
									if(type != NULL && type->set != NULL)
										c.m.value = (*type->set)(&c, obj, first, third);
								}
							}
						}
					}
				} else if(lispnumber(&c.m, first) || lispsymbol(&c.m, first)){
					// it looks like a get, ensure form is ('prop extref) and
					// call the getter.
					lispref_t second = lispcdr(&c.m, c.m.expr);
					if(lisppair(&c.m, second)){
						second = lispcar(&c.m, second);
						if(lispextref(&c.m, second)){
							void *obj;
							Type *type;
							lispextget(&c.m, second, &obj, (void**)&type);
							if(type != NULL && type->get != NULL)
								c.m.value = (*type->get)(&c, obj, first);
						}
					}
				} else {
					for(lispref_t np = c.m.expr; np != LISP_NIL; np = lispcdr(&c.m, np)){
						printf(" ");
						lispprint1(&c.m, lispcar(&c.m, np), 1);
					}
					printf("\n");
					fprintf(stderr, "extcall: not sure what's going on: %x\n", first);
				}
			}
			c.m.expr = LISP_NIL;
			c.m.value = LISP_NIL;
		}

		fclose(fp);
	}
	return 0;
}