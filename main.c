
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
	LispBinaryOp *equal;
	LispBinaryOp *less;

	LispTernaryOp *cond;
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
	Expr *cond;
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

static LispRef
exprEqual(void *ctx, LispRef left, LispRef right)
{
	Context *c = ctx;
	if(lispIsExtRef(&c->m, left) && lispIsExtRef(&c->m, right)){
		return exprBinaryOp(ctx, '=', left, right);
	}
	return lispBuiltin(&c->m, LISP_BUILTIN_FALSE);
}

static LispRef
exprLess(void *ctx, LispRef left, LispRef right)
{
	return exprBinaryOp(ctx, '<', left, right);
}

void lispEvaluate(Context *context);

static LispRef
exprCond(void *ctx, LispRef cond, LispRef left, LispRef right)
{
	Context *context = ctx;
	LispMachine *m = &context->m;

	// call both branches via the system stack. there should
	// be a way to do this using the LispMachine stack instead, but I am
	// going to postpone that for the time being.
	LispRef *condValue = lispRegister(m, cond);
	LispRef *elseReg = lispRegister(m, right);
	m->expr = left;
	lispEvaluate(context);
	LispRef *thenValue = lispRegister(m, m->value);
	m->expr = *elseReg;
	lispRelease(m, elseReg);
	lispEvaluate(context);
	LispRef *elseValue = lispRegister(m, m->value);

	// all need to be extrefs..
	if(lispIsExtRef(m, *condValue) && lispIsExtRef(m, *thenValue) && lispIsExtRef(m, *elseValue)){
		Expr *condExpr;
		Type *condType;
		lispExtGet(m, *condValue, (void**)&condExpr, (void**)&condType);

		Expr *leftExpr;
		Type *leftType;
		lispExtGet(m, *thenValue, (void**)&leftExpr, (void**)&leftType);

		if(condType != leftType) // type of left must match cond
			goto error;

		Expr *rightExpr;
		Type *rightType;
		lispExtGet(m, *elseValue, (void**)&rightExpr, (void**)&rightType);

		if(condType != rightType) // type of right must match cond (and left)
			goto error;

		// allocate and initialize node for the condition
		Expr *expr = malloc(sizeof expr[0]);
		memset(expr, 0, sizeof expr[0]);
		expr->cond = condExpr;
		expr->left = leftExpr;
		expr->right = rightExpr;
		expr->op = '?';

		LispRef exprRef = lispExtAlloc(m);
		lispExtSet(m, exprRef, expr, leftType);

		lispRelease(m, condValue);
		lispRelease(m, elseValue);
		lispRelease(m, thenValue);

		return exprRef;
	}
error:
	lispRelease(m, condValue);
	lispRelease(m, elseValue);
	lispRelease(m, thenValue);
	fprintf(stderr, "exprCond: fail\n");
	return lispBuiltin(m, LISP_BUILTIN_ERROR);
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

static void
extrefBinaryOp(Context *c)
{
	LispRef second = lispCdr(&c->m, c->m.expr);
	LispRef third = lispCdr(&c->m, second);
	second = lispCar(&c->m, second);
	third = lispCar(&c->m, third);
	void *obj;
	Type *type;
	if(lispExtGet(&c->m, second, &obj, (void**)&type) == 0 || lispExtGet(&c->m, third, &obj, (void**)&type) == 0){
		if(type != NULL && type->equal != NULL)
			c->m.value = (*type->equal)(&c, second, third);
	} else {
		c->m.value = lispBuiltin(&c->m, LISP_BUILTIN_FALSE);
	}
}

void
lispEvaluate(Context *context)
{
	LispMachine *m = &context->m;
	lispCall(m, LISP_STATE_RETURN, LISP_STATE_EVAL);
	while(lispStep(m) == 1){
		LispRef first = lispCar(m, m->expr);
		if(lispIsExtRef(m, first)){
			// first element is an extref: it's an apply
			LispRef second = lispCdr(m, m->expr);
			void *obj;
			Type *type;
			lispExtGet(m, first, &obj, (void**)&type);
			if(type != NULL && type->apply != NULL)
				m->value = (*type->apply)(context, obj, second);
		} else if(lispIsBuiltin(m, first, LISP_BUILTIN_PRINT1)){
			LispRef second = lispCdr(m, m->expr);
			LispRef third = lispCdr(m, second);
			second = lispCar(m, second);
			third = lispCar(m, third);
			if(lispIsExtRef(m, third)){
				void *obj;
				Type *type;
				lispExtGet(m, third, &obj, (void**)&type);
				if(type != NULL && type->print != NULL)
					m->value = (*type->print)(context, second, third);
			} else {
				fprintf(stderr, "builtin-add fail\n");
			}
		} else if(lispIsBuiltin(m, first, LISP_BUILTIN_SET)){
			// it's a set.. ensure form is (set! ('prop extref) value)
			// and call setter.
			LispRef form = lispCdr(m, m->expr);
			LispRef third = lispCdr(m, form);
			form = lispCar(m, form);
			if(lispIsPair(m, form)){
				first = lispCar(m, form);
				if(lispIsNumber(m, first) || lispIsSymbol(m, first)){
					LispRef second = lispCdr(m, form);
					if(lispIsPair(m, second) && lispIsPair(m, third)){
						second = lispCar(m, second);
						if(lispIsExtRef(m, second)){
							third = lispCar(m, third);
							void *obj;
							Type *type;
							lispExtGet(m, second, &obj, (void**)&type);
							if(type != NULL && type->set != NULL)
								m->value = (*type->set)(context, obj, first, third);
						}
					}
				}
			}
		} else if(lispIsNumber(m, first) || lispIsSymbol(m, first)){
			// it looks like a get, ensure form is ('prop extref) and
			// call the getter.
			LispRef second = lispCdr(m, m->expr);
			if(lispIsPair(m, second)){
				second = lispCar(m, second);
				if(lispIsExtRef(m, second)){
					void *obj;
					Type *type;
					lispExtGet(m, second, &obj, (void**)&type);
					if(type != NULL && type->get != NULL)
						m->value = (*type->get)(context, obj, first);
				}
			}
		} else if(lispIsBuiltin(m, first, LISP_BUILTIN_IF)){
			// we come here with condition evaluated (to an extref type!) while
			// then and else are still un-evaluated. we call ->cond on the
			// extref type, which may do whatever it wants (including
			// evaluating both then and else)
			LispRef cond = m->value;
			LispRef left = lispCdr(m, lispCdr(m, m->expr)); // if -> cond -> then
			LispRef right = lispCdr(m, left); // -> else
			left = lispCar(m, left);
			right = lispCar(m, right);
			void *obj;
			Type *type;
			if(lispExtGet(m, cond, &obj, (void**)&type) == 0){
				if(type != NULL && type->cond != NULL)
					m->value = (*type->cond)(context, cond, left, right);
			} else {
				m->value = lispBuiltin(m, LISP_BUILTIN_FALSE);
			}
		} else if(lispIsBuiltin(m, first, LISP_BUILTIN_ISEQUAL)){
			LispRef second = lispCdr(m, m->expr);
			LispRef third = lispCdr(m, second);
			second = lispCar(m, second);
			third = lispCar(m, third);
			void *obj;
			Type *type;
			if(lispExtGet(m, second, &obj, (void**)&type) == 0 || lispExtGet(m, third, &obj, (void**)&type) == 0){
				if(type != NULL && type->equal != NULL)
					m->value = (*type->equal)(context, second, third);
			} else {
				m->value = lispBuiltin(m, LISP_BUILTIN_FALSE);
			}
		} else if(lispIsBuiltin(m, first, LISP_BUILTIN_ISLESS)){
			LispRef second = lispCdr(m, m->expr);
			LispRef third = lispCdr(m, second);
			second = lispCar(m, second);
			third = lispCar(m, third);
			void *obj;
			Type *type;
			if(lispExtGet(m, second, &obj, (void**)&type) == 0 || lispExtGet(m, third, &obj, (void**)&type) == 0){
				if(type != NULL && type->less != NULL)
					m->value = (*type->less)(context, second, third);
			} else {
				m->value = lispBuiltin(m, LISP_BUILTIN_FALSE);
			}
		} else if(lispIsBuiltin(m, first, LISP_BUILTIN_ADD)){
			LispRef second = lispCdr(m, m->expr);
			LispRef third = lispCdr(m, second);
			second = lispCar(m, second);
			third = lispCar(m, third);
			if(lispIsExtRef(m, second) && lispIsExtRef(m, third)){
				void *obj;
				Type *type;
				lispExtGet(m, second, &obj, (void**)&type);
				if(type != NULL && type->add != NULL)
					m->value = (*type->add)(context, second, third);
			} else {
				fprintf(stderr, "builtin-add fail\n");
			}
		} else if(lispIsBuiltin(m, first, LISP_BUILTIN_MUL)){
			LispRef second = lispCdr(m, m->expr);
			LispRef third = lispCdr(m, second);
			second = lispCar(m, second);
			third = lispCar(m, third);
			if(lispIsExtRef(m, second) && lispIsExtRef(m, third)){
				void *obj;
				Type *type;
				lispExtGet(m, second, &obj, (void**)&type);
				if(type != NULL && type->mul != NULL)
					m->value = (*type->mul)(context, second, third);
			} else {
				fprintf(stderr, "builtin-mul fail\n");
			}
		} else {
			fprintf(stderr, "extcall: not sure what's going on: %x\n", first);
			for(LispRef np = m->expr; np != LISP_NIL; np = lispCdr(m, np)){
				printf(" ");
				lispPrint1(m, lispCar(m, np), 1);
			}
			printf("\n");
		}
	}
	m->expr = LISP_NIL;
	//m->value = LISP_NIL;
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
	c.exprType.equal = exprEqual;
	c.exprType.less = exprLess;
	c.exprType.print = exprPrint;
	c.exprType.cond = exprCond;
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
			lispEvaluate(&c);
		}

		fclose(fp);
	}
	return 0;
}
