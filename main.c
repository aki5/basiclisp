
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
	LispApplier *apply;
	LispSetter *set;
	LispGetter *get;
};

struct Context {
	LispMachine m;
	LispRef buffer_symbol;
	LispRef len_symbol;
	LispRef cap_symbol;
	Type bufclass;
	Type buftype;
};

struct Buffer {
	size_t len;
	size_t cap;
	unsigned char buf[0];
};

static LispRef
bufferGet(void *ctx, void *obj, LispRef lispkey)
{
	Context *c = (Context *)ctx;
	Buffer *buf = (Buffer *)obj;
	if(lispIsSymbol(&c->m, lispkey)){
		if(lispkey == c->len_symbol){
			return lispNumber(&c->m, buf->len);
		} else if(lispkey == c->cap_symbol){
			return lispNumber(&c->m, buf->cap);
		}
	} else if(lispIsNumber(&c->m, lispkey)){
		size_t i = lispGetInt(&c->m, lispkey);
		if(i >= buf->len)
			return lispBuiltin(&c->m, LISP_BUILTIN_ERROR);
		return lispNumber(&c->m, buf->buf[i]);
	}
	return lispBuiltin(&c->m, LISP_BUILTIN_ERROR);
}

static LispRef
bufferSet(void *ctx, void *obj, LispRef lispkey, LispRef lispval)
{
	Context *c = (Context *)ctx;
	Buffer *buf = (Buffer *)obj;
	if(lispIsSymbol(&c->m, lispkey)){
		if(lispkey == c->len_symbol){
			buf->len = lispGetInt(&c->m, lispval);
			return lispval;
		}
	} else if(lispIsNumber(&c->m, lispkey)){
		size_t i = lispGetInt(&c->m, lispkey);
		if(i >= buf->len)
			return lispBuiltin(&c->m, LISP_BUILTIN_ERROR);
		unsigned char ch = lispGetInt(&c->m, lispval);
		buf->buf[i] = ch;
		return lispval;
	}
	return lispBuiltin(&c->m, LISP_BUILTIN_ERROR);
}

static LispRef
bufferNew(void *ctx, void *obj, LispRef args)
{
	Context *c = (Context *)ctx;
	if(lispIsPair(&c->m, args) && lispIsNumber(&c->m, lispCar(&c->m, args))){
		LispRef lispcap = lispCar(&c->m, args);
		size_t cap = lispGetInt(&c->m, lispcap);
		fprintf(stderr, "bufalloc %zu\n", cap);
		Buffer *buf = malloc(sizeof buf[0] + cap);
		if(buf == NULL)
			return lispBuiltin(&c->m, LISP_BUILTIN_ERROR);
		memset(buf, 0, sizeof buf[0] + cap);
		buf->cap = cap;
		buf->len = cap;
		LispRef extref = lispExtAlloc(&c->m);
		lispExtSet(&c->m, extref, (void*)buf, &c->buftype);
		return extref;
	}
	return lispBuiltin(&c->m, LISP_BUILTIN_ERROR);
}

struct slice {
	int unget;
	char *buf;
	size_t len;
};

int
sliceGetc(struct slice *sl)
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
sliceUngetc(int ch, struct slice *sl)
{
	assert(sl->unget == -1);
	sl->unget = ch;
}


int
main(int argc, char *argv[])
{
	Context c;

	memset(&c, 0, sizeof c);
	lispInit(&c.m);
	lispSetPort(&c.m, 0, (int(*)(int,void*))NULL, (int(*)(void*))getc, (int(*)(int,void*))ungetc, (void*)stdin);
	lispSetPort(&c.m, 1, (int(*)(int,void*))putc, (int(*)(void*))NULL, (int(*)(int,void*))NULL, (void*)stdout);

	c.bufclass.apply = bufferNew;
	c.buftype.set = bufferSet;
	c.buftype.get = bufferGet;

	c.buffer_symbol = lispSymbol(&c.m, "buffer");
	c.len_symbol = lispSymbol(&c.m, "len");
	c.cap_symbol = lispSymbol(&c.m, "cap");
	LispRef bufclass_ref = lispExtAlloc(&c.m);

	lispExtSet(&c.m, bufclass_ref, NULL, &c.bufclass);
	lispDefine(&c.m, c.buffer_symbol, bufclass_ref);

	for(size_t i = 1; i < (size_t)argc; i++){
		FILE *fp = fopen(argv[i], "rb");
		if(fp == NULL){
			fprintf(stderr, "cannot open %s\n", argv[i]);
			return 1;
		}
		lispSetPort(&c.m, 0, (int(*)(int,void*))NULL, (int(*)(void*))getc, (int(*)(int,void*))ungetc, (void*)fp);
		for(;;){
			c.m.expr = lispParse(&c.m, 1);
			if(lispIsError(&c.m, c.m.expr))
				break;
			lispCall(&c.m, LISP_STATE_RETURN, LISP_STATE_EVAL);
			while(lispStep(&c.m) == 1){
				c.m.value = lispBuiltin(&c.m, LISP_BUILTIN_ERROR); // default to error up front.
				LispRef first = lispCar(&c.m, c.m.expr);
				if(lispIsExtRef(&c.m, first)){
					// first element is an extref: it's an apply
					LispRef second = lispCdr(&c.m, c.m.expr);
					void *obj;
					Type *type;
					lispExtGet(&c.m, first, &obj, (void**)&type);
					if(type != NULL && type->apply != NULL)
						c.m.value = (*type->apply)(&c, obj, second);
				} else if(lispIsBuiltin(&c.m, first, LISP_BUILTIN_SET)){
					// it's a set.. ensure form is (set! ('prop extref) value)
					// and call setter.
					LispRef form = lispCdr(&c.m, c.m.expr);
					LispRef third = lispCdr(&c.m, form);
					form = lispCar(&c.m, form);
					if(lispIsPair(&c.m, form)){
						first = lispCar(&c.m, form);
						if(lispIsNumber(&c.m, first) || lispIsSymbol(&c.m, first)){
							LispRef second = lispCdr(&c.m, form);
							if(lispIsPair(&c.m, second) && lispIsPair(&c.m, third)){
								second = lispCar(&c.m, second);
								if(lispIsExtRef(&c.m, second)){
									third = lispCar(&c.m, third);
									void *obj;
									Type *type;
									lispExtGet(&c.m, second, &obj, (void**)&type);
									if(type != NULL && type->set != NULL)
										c.m.value = (*type->set)(&c, obj, first, third);
								}
							}
						}
					}
				} else if(lispIsNumber(&c.m, first) || lispIsSymbol(&c.m, first)){
					// it looks like a get, ensure form is ('prop extref) and
					// call the getter.
					LispRef second = lispCdr(&c.m, c.m.expr);
					if(lispIsPair(&c.m, second)){
						second = lispCar(&c.m, second);
						if(lispIsExtRef(&c.m, second)){
							void *obj;
							Type *type;
							lispExtGet(&c.m, second, &obj, (void**)&type);
							if(type != NULL && type->get != NULL)
								c.m.value = (*type->get)(&c, obj, first);
						}
					}
				} else {
					fprintf(stderr, "extcall: not sure what's going on: %x\n", first);
					for(LispRef np = c.m.expr; np != LISP_NIL; np = lispCdr(&c.m, np)){
						printf(" ");
						lispPrint1(&c.m, lispCar(&c.m, np), 1);
					}
					printf("\n");
				}
			}
			c.m.expr = LISP_NIL;
			c.m.value = LISP_NIL;
		}

		fclose(fp);
	}
	return 0;
}