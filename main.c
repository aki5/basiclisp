
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
typedef struct Expr Expr;

struct Type {
	LispApplier *apply;
	LispSetter *set;
	LispGetter *get;

	LispBinaryOp *add;
	LispBinaryOp *mul;
	LispBinaryOp *print;
};

struct Context {
	LispMachine m;
	LispRef lenSymbol;
	LispRef capSymbol;

	LispRef bufferSymbol;
	Type bufferClass;
	Type bufferType;

	LispRef exprSymbol;
	Type exprClass;
	Type exprType;
};

struct Buffer {
	size_t len;
	size_t cap;
	unsigned char buf[0];
};

struct Expr {
	Expr *left;
	Expr *right;
	unsigned op;
};

static LispRef
bufferGet(void *ctx, void *obj, LispRef lispkey)
{
	Context *c = (Context *)ctx;
	Buffer *buf = (Buffer *)obj;
	if(lispIsSymbol(&c->m, lispkey)){
		if(lispkey == c->lenSymbol){
			return lispNumber(&c->m, buf->len);
		} else if(lispkey == c->capSymbol){
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
		if(lispkey == c->lenSymbol){
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
		lispExtSet(&c->m, extref, (void*)buf, &c->bufferType);
		return extref;
	}
	return lispBuiltin(&c->m, LISP_BUILTIN_ERROR);
}


static LispRef
exprGet(void *ctx, void *obj, LispRef lispkey)
{
	Context *c = (Context *)ctx;
	Expr *expr = obj;
	return lispBuiltin(&c->m, LISP_BUILTIN_ERROR);
}

static LispRef
exprSet(void *ctx, void *obj, LispRef lispkey, LispRef lispval)
{
	Context *c = (Context *)ctx;
	Expr *expr = obj;
	return lispBuiltin(&c->m, LISP_BUILTIN_ERROR);
}

static LispRef
exprBinaryOp(void *ctx, char op, LispRef left, LispRef right)
{
	Context *c = ctx;

	if(lispIsExtRef(&c->m, left) && lispIsExtRef(&c->m, right)){
		Expr *leftExpr;
		Type *leftType;
		lispExtGet(&c->m, left, (void**)&leftExpr, (void**)&leftType);

		Expr *rightExpr;
		Type *rightType;
		lispExtGet(&c->m, right, (void**)&rightExpr, (void**)&rightType);

		if(leftType != rightType)
			goto error;

		Expr *expr = malloc(sizeof expr[0]);
		memset(expr, 0, sizeof expr[0]);
		expr->left = leftExpr;
		expr->right = rightExpr;
		expr->op = op;

		LispRef exprRef = lispExtAlloc(&c->m);
		lispExtSet(&c->m, exprRef, expr, leftType);

		return exprRef;
	}
error:
	return lispBuiltin(&c->m, LISP_BUILTIN_ERROR);
}

static LispRef
exprAdd(void *ctx, LispRef left, LispRef right)
{
	return exprBinaryOp(ctx, '+', left, right);
}

static LispRef
exprMul(void *ctx, LispRef left, LispRef right)
{
	return exprBinaryOp(ctx, '*', left, right);
}

void
exprPrint1(LispMachine *m, int port, Expr *expr)
{
	if(expr == NULL)
		return;
	if(expr->op == '+' || expr->op == '*'){
		exprPrint1(m, port, expr->left);
		m->ports[port].writebyte(expr->op, m->ports[port].context);
		exprPrint1(m, port, expr->right);
	} else {
		m->ports[port].writebyte(expr->op, m->ports[port].context);
	}
}

static LispRef
exprPrint(void *ctx, LispRef left, LispRef right)
{
	Context *c = ctx;
	if(lispIsNumber(&c->m, left) && lispIsExtRef(&c->m, right)){
		Expr *rightExpr;
		Type *rightType;
		lispExtGet(&c->m, right, (void**)&rightExpr, (void**)&rightType);
		exprPrint1(&c->m, lispGetInt(&c->m, left), rightExpr);
		return right;
	}
	return lispBuiltin(&c->m, LISP_BUILTIN_ERROR);
}

static LispRef
exprNew(void *ctx, void *obj, LispRef args)
{
	static int id = 'a';
	Context *c = (Context *)ctx;
	if(lispIsNull(&c->m, args)){
		Expr *expr = malloc(sizeof expr[0]);
		if(expr == NULL)
			return lispBuiltin(&c->m, LISP_BUILTIN_ERROR);
		memset(expr, 0, sizeof expr[0]);
		LispRef extref = lispExtAlloc(&c->m);
		expr->op = id++;
		lispExtSet(&c->m, extref, (void*)expr, &c->exprType);
		return extref;
	}
	return lispBuiltin(&c->m, LISP_BUILTIN_ERROR);
}

int
main(int argc, char *argv[])
{
	Context c;

	memset(&c, 0, sizeof c);
	lispInit(&c.m);
	lispSetPort(&c.m, 0, (int(*)(int,void*))NULL, (int(*)(void*))getc, (int(*)(int,void*))ungetc, (void*)stdin);
	lispSetPort(&c.m, 1, (int(*)(int,void*))putc, (int(*)(void*))NULL, (int(*)(int,void*))NULL, (void*)stdout);

	c.bufferClass.apply = bufferNew;
	c.bufferType.set = bufferSet;
	c.bufferType.get = bufferGet;

	c.bufferSymbol = lispSymbol(&c.m, "buffer");
	c.lenSymbol = lispSymbol(&c.m, "len");
	c.capSymbol = lispSymbol(&c.m, "cap");
	LispRef bufferClassRef = lispExtAlloc(&c.m);

	lispExtSet(&c.m, bufferClassRef, NULL, &c.bufferClass);
	lispDefine(&c.m, c.bufferSymbol, bufferClassRef);

	c.exprClass.apply = exprNew;
	c.exprType.add = exprAdd;
	c.exprType.mul = exprMul;
	c.exprType.print = exprPrint;
	c.exprSymbol = lispSymbol(&c.m, "expr");
	LispRef exprClassRef = lispExtAlloc(&c.m);
	lispExtSet(&c.m, exprClassRef, NULL, &c.exprClass);
	lispDefine(&c.m, c.exprSymbol, exprClassRef);


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
				} else if(lispIsBuiltin(&c.m, first, LISP_BUILTIN_PRINT1)){
					LispRef second = lispCdr(&c.m, c.m.expr);
					LispRef third = lispCdr(&c.m, second);
					second = lispCar(&c.m, second);
					third = lispCar(&c.m, third);
					if(lispIsExtRef(&c.m, third)){
						void *obj;
						Type *type;
						lispExtGet(&c.m, third, &obj, (void**)&type);
						if(type != NULL && type->print != NULL)
							c.m.value = (*type->print)(&c, second, third);
					} else {
						fprintf(stderr, "builtin-add fail\n");
					}
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
				} else if(lispIsBuiltin(&c.m, first, LISP_BUILTIN_ADD)){
					LispRef second = lispCdr(&c.m, c.m.expr);
					LispRef third = lispCdr(&c.m, second);
					second = lispCar(&c.m, second);
					third = lispCar(&c.m, third);
					if(lispIsExtRef(&c.m, second) && lispIsExtRef(&c.m, third)){
						void *obj;
						Type *type;
						lispExtGet(&c.m, second, &obj, (void**)&type);
						if(type != NULL && type->add != NULL)
							c.m.value = (*type->add)(&c, second, third);
					} else {
						fprintf(stderr, "builtin-add fail\n");
					}
				} else if(lispIsBuiltin(&c.m, first, LISP_BUILTIN_MUL)){
					LispRef second = lispCdr(&c.m, c.m.expr);
					LispRef third = lispCdr(&c.m, second);
					second = lispCar(&c.m, second);
					third = lispCar(&c.m, third);
					if(lispIsExtRef(&c.m, second) && lispIsExtRef(&c.m, third)){
						void *obj;
						Type *type;
						lispExtGet(&c.m, second, &obj, (void**)&type);
						if(type != NULL && type->mul != NULL)
							c.m.value = (*type->mul)(&c, second, third);
					} else {
						fprintf(stderr, "builtin-mul fail\n");
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