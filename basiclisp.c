
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <assert.h>
#include "basiclisp.h"

// external object system

// cmp(extref, extref) -> {LISP_BUILTIN_LESS, LISP_BUILTIN_EQUAL, LISP_BUILTIN_GREATER}
// (extref ...) -> extref apply extref to arguments, typically a constructor.
// (sym extref extref) -> binary operator, sym can be one of +, -, *, / etc.

// byte values are "inlined" to the symbol and are parsed as #0 #1 ... #255
// long symbols are offsets to a name table
// quoted symbol evaluates to symbol
// symbol evaluates to variable in current environment
// a reference always points to a pair
// a pair has two things, car and cdr.

// when a symbol is applied to a function (product of lambda), a variable
// corresponding to that symbol is returned from the function's closure.

// (let (vector3 x y z) (closure(x y z)))
// (let origin (vector3 '0 '0 '0))
// (print! 'x ('x origin) 'y ('y origin) 'z ('z origin))

// any function producing a sequence of symbols when called repeatedly is an input.
// - end of stream is indicated by returning #nil
// - error is indicated by returning (#error ...)


#define nelem(x) (sizeof(x)/sizeof(x[0]))

static char *bltnames[] = {

[LISP_BUILTIN_IF] = "if",
[LISP_BUILTIN_FUNCTION] = "function",
[LISP_BUILTIN_CONTINUE] = "continue",
[LISP_BUILTIN_LET] = "let",
[LISP_BUILTIN_SCOPE] = "scope",
[LISP_BUILTIN_LAMBDA] = "lambda",
[LISP_BUILTIN_QUOTE] = "quote",
[LISP_BUILTIN_CALLCC] = "call/cc",
[LISP_BUILTIN_SET] = "set!",
[LISP_BUILTIN_SETCAR] = "set-car!",
[LISP_BUILTIN_SETCDR] = "set-cdr!",
[LISP_BUILTIN_CONS] = "cons",
[LISP_BUILTIN_CAR] = "car",
[LISP_BUILTIN_CDR] = "cdr",
[LISP_BUILTIN_EVAL] = "eval",

// this is implemented on symbols and string constants, and produces a
// lexicographic ordering of unicode codepoints. external objects can implement
// it too. it returns #below, #equal or #above.
[LISP_BUILTIN_COMPARE] = "compare",

// these will get eliminated. arithmetic can be implemented between external
// objects using symbol references to whatever operators the application sees
// fit. all numbers will be constructed as external objects using notation like
//	(int 10) or (float 10.1).
// the parser will be modified to interpret number constants as symbols.
[LISP_BUILTIN_ADD] = "+",
[LISP_BUILTIN_SUB] = "-",
[LISP_BUILTIN_MUL] = "*",
[LISP_BUILTIN_DIV] = "/",

[LISP_BUILTIN_ISPAIR] = "pair?",
[LISP_BUILTIN_ISEQUAL] = "equal?",

// less? becomes
//	(let (less? a b)
//		(equal? (compare a b) #less))
[LISP_BUILTIN_ISLESS] = "less?",

// error? becomes
//	(if (pair? err)
//		(equal? (car err) #error)
//		#f)
[LISP_BUILTIN_ISERROR] = "error?",

// should be (fmt obj) -> '(1 0)
[LISP_BUILTIN_PRINT1] = "print1",

// error should return a list (#error message context) instead.. obviously this
// won't work for OOM errors, so we should just preallocate that one.
[LISP_BUILTIN_ERROR] = "#error",

// this shall be the list terminator etc. also the empty list.
[LISP_BUILTIN_NIL] = "#nil",

// more readable than the standard #t, #f
[LISP_BUILTIN_TRUE] = "#true",
[LISP_BUILTIN_FALSE] = "#false",

// compare must return one of these
[LISP_BUILTIN_ABOVE] = "#above",
[LISP_BUILTIN_BELOW] = "#below",
[LISP_BUILTIN_EQUAL] = "#equal",

};

LispRef *
lispRegister(LispMachine *m, LispRef val)
{
	uint32_t reguse = m->reguse;
	for(size_t i = 0; i < nelem(m->regs); i++){
		if((reguse & (1<<i)) == 0){
			LispRef *regp = &m->regs[i];
			m->reguse = reguse | (1<<i);
			*regp = val;
			m->regCount++;
			if(m->regCount > m->maxRegCount){
				m->maxRegCount = m->regCount;
				fprintf(stderr, "max regcount %d\n", m->maxRegCount);
			}
			return regp;
		}
	}
	fprintf(stderr, "lispRegister: ran out of registers\n");
	abort();
	return NULL;
}

void
lispRelease(LispMachine *m, LispRef *regp)
{
	size_t i = regp - m->regs;
	if(i >= nelem(m->regs))
		abort();
	*regp = LISP_NIL;
	m->regCount--;
	m->reguse &= ~(1<<i);
}

static LispRef
mkref(int val, int tag)
{
	LispRef ref = LISP_TAG_BIT >> tag;
	return ref | ((ref - 1) & val);
}

static int
reftag(LispRef ref)
{
	LispRef nlz;
	__asm__("lzcnt %[ref], %[nlz]"
		: [nlz]"=r"(nlz)
		: [ref]"r"(ref)
		: "cc"
	);
	return (int)nlz;
}

static intptr_t
refval(LispRef ref)
{
	return (LISP_VAL_MASK >> reftag(ref)) & ref;
}

static size_t
urefval(LispRef ref)
{
	return (LISP_VAL_MASK >> reftag(ref)) & ref;
}

LispRef
lispBuiltin(LispMachine *m, int val)
{
	return mkref(val, LISP_TAG_BUILTIN);
}

static LispRef *
lispCellPointer(LispMachine *m, LispRef ref)
{
	size_t off = urefval(ref);
	if(off <= 0 || off >= m->mem.len){
		fprintf(stderr, "dereferencing an out of bounds reference: %x\n", ref);
		abort();
	}
	assert((off&1) == 0);
	return m->mem.ref + off;
}

static char *
lispStringPointer(LispMachine *m, LispRef ref)
{
	size_t off = urefval(ref);
	if(off < LISP_INLINE_SYMBOL || off >= LISP_INLINE_SYMBOL+m->strings.len){
		fprintf(stderr, "dereferencing an out of bounds string reference: %x off: %zu %c\n", ref, off, off);
		abort();
	}
	return m->strings.p + off - LISP_INLINE_SYMBOL;
}

static LispRef
lispSetCar(LispMachine *m, LispRef base, LispRef obj)
{
	LispRef *p = lispCellPointer(m, base);
	p[LISP_CAR_OFFSET] = obj;
	return obj;
}

static LispRef
lispSetCdr(LispMachine *m, LispRef base, LispRef obj)
{
	LispRef *p = lispCellPointer(m, base);
	p[LISP_CDR_OFFSET] = obj;
	return obj;
}

static void
lispMemSet(LispRef *mem, LispRef val, size_t len)
{
	for(size_t i = 0; i < len; i++)
		mem[i] = val;
}

static int
lispIsAtom(LispMachine *m, LispRef a)
{
	int tag = reftag(a);
	return tag != LISP_TAG_SYMBOL && tag != LISP_TAG_PAIR;
}

LispRef
lispCar(LispMachine *m, LispRef base)
{
	LispRef *p = lispCellPointer(m, base);
	return p[LISP_CAR_OFFSET];
}

LispRef
lispCdr(LispMachine *m, LispRef base)
{
	LispRef *p = lispCellPointer(m, base);
	return p[LISP_CDR_OFFSET];
}

int
lispIsSymbol(LispMachine *m, LispRef a)
{
	return reftag(a) == LISP_TAG_SYMBOL;
}

int
lispIsNumber(LispMachine *m, LispRef a)
{
	return reftag(a) == LISP_TAG_INTEGER;
}

int
lispGetInt(LispMachine *m, LispRef ref)
{
	if(lispIsNumber(m, ref))
		return refval(ref);
	abort();
	return -1;
}

int
lispIsBuiltinTag(LispMachine *m, LispRef ref)
{
	return reftag(ref) == LISP_TAG_BUILTIN;
}

int
lispGetBuiltin(LispMachine *m, LispRef ref)
{
	if(lispIsBuiltinTag(m, ref))
		return urefval(ref);
	abort();
	return -1;
}

int
lispIsBuiltin(LispMachine *m, LispRef ref, int builtin)
{
	return lispIsBuiltinTag(m, ref) && lispGetBuiltin(m, ref) == builtin;
}

int
lispIsExtRef(LispMachine *mach, LispRef a)
{
	return reftag(a) == LISP_TAG_EXTREF;
}

// pair, can be nil.
int
lispIsList(LispMachine *m, LispRef a)
{
	return reftag(a) == LISP_TAG_PAIR;
}

int
lispIsNull(LispMachine *m, LispRef a)
{
	return reftag(a) == LISP_TAG_PAIR && a == LISP_NIL;
}

// a non-nil pair (car and cdr will work)
int
lispIsPair(LispMachine *m, LispRef a)
{
	return reftag(a) == LISP_TAG_PAIR && a != LISP_NIL;
}

int
lispIsError(LispMachine *m, LispRef a)
{
	return lispIsBuiltin(m, a, LISP_BUILTIN_ERROR);
}

static int
isWhitespace(int c)
{
	switch(c){
	case '\t': case '\n': case '\v': case '\f': case '\r': case ' ':
		return 1;
	}
	return 0;
}

static int
isBreak(int c)
{
	switch(c){
	case ';':
	case '(': case ')':
	case '\'': case ',':
	case '\t': case '\n': case '\v': case '\f': case '\r': case ' ':
		return 1;
	}
	return 0;
}

static int
isHexadecimal(int c)
{
	switch(c){
	case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
		return 1;
	}
	return 0;
}

static int
isDecimal(int c)
{
	switch(c){
	case '0': case '1': case '2': case '3': case '4':
	case '5': case '6': case '7': case '8': case '9':
	case 'x':
		return 1;
	}
	return 0;
}

static void
tokenAppend(LispMachine *m, int ch)
{
	if(m->token.len == m->token.cap){
		m->token.cap = (m->token.cap == 0) ? 256 : 2*m->token.cap;
		void *p = realloc(m->token.buf, m->token.cap);
		if(p == NULL){
			fprintf(stderr, "tokenAppend: realloc failed\n");
			abort();
		}
		m->token.buf = p;
	}
	m->token.buf[m->token.len] = ch;
	m->token.len++;
}

static void
tokenClear(LispMachine *m)
{
	m->token.len = 0;
}

static int
lispLex(LispMachine *m)
{
	int isinteger, ishex;
	int ch, peekc;

again:
	tokenClear(m);
	if((ch = m->ports[0].readbyte(m->ports[0].context)) == -1)
		return -1;
	if(isWhitespace(ch)){
		if(ch == '\n')
			m->lineno++;
		goto again;
	}
	switch(ch){
	// skip over comments
	case ';':
		while((ch = m->ports[0].readbyte(m->ports[0].context)) != -1){
			if(ch == '\n'){
				m->lineno++;
				goto again;
			}
		}
		return -1;

	// single character tokens just represent themselves. watch out for
	// collisions with LISP_TOK_XXXX.. should never be a problem to have tokens
	// in the ascii control character range (<32) since we have so few.
	case '(': case ')':
	case '\'': case ',':
	case '.':
	caseself:
		return ch;

	case '0': case '1': case '2': case '3': case '4':
	case '5': case '6': case '7': case '8': case '9':
		ishex = 0;
		while(ch != -1){
			if(!isDecimal(ch) && !(ishex && isHexadecimal(ch))){
				if(isBreak(ch))
					break;
				goto casesym;
			}
			if(ch == 'x')
				ishex = 1;
			tokenAppend(m, ch);
			ch = m->ports[0].readbyte(m->ports[0].context);
		}
		if(ch != -1)
			m->ports[0].unreadbyte(ch, m->ports[0].context);
		return LISP_TOK_INTEGER;

	// string constant, detect and interpret standard escapes like in c.
	case '"':
		ch = m->ports[0].readbyte(m->ports[0].context);
		if(ch == '\n')
			m->lineno++;
		while(ch != -1 && ch != '"'){
			if(ch == '\\'){
				int code;
				ch = m->ports[0].readbyte(m->ports[0].context);
				if(ch == -1)
					continue;
				switch(ch){
				default:
					break;
				case '\n':
					m->lineno++;
					break;
				// expand octal code (ie. \012) to byte value.
				case '0': case '1': case '2': case '3':
				case '4': case '5': case '6': case '7':
					code = 0;
					do {
						code *= 8;
						code += ch - '0';
						ch = m->ports[0].readbyte(m->ports[0].context);
					} while(ch >= '0' && ch <= '7');
					if(ch != -1) m->ports[0].unreadbyte(ch, m->ports[0].context);
					ch = code;
					break;
				case 'a': ch = '\a'; break;
				case 'b': ch = '\b'; break;
				case 'n': ch = '\n'; break;
				case 't': ch = '\t'; break;
				case 'r': ch = '\r'; break;
				case 'v': ch = '\v'; break;
				case 'f': ch = '\f'; break;
				}
			}
			tokenAppend(m, ch);
			ch = m->ports[0].readbyte(m->ports[0].context);
			if(ch == '\n')
				m->lineno++;
		}
		return LISP_TOK_STRING;

	// symbol is any string of nonbreak characters not starting with a number
	default:
	casesym:
		while(ch != -1 && !isBreak(ch)){
			tokenAppend(m, ch);
			ch = m->ports[0].readbyte(m->ports[0].context);
		}
		m->ports[0].unreadbyte(ch, m->ports[0].context);
		return LISP_TOK_SYMBOL;
	}
	return -1;
}

static size_t
nextPow2(size_t v)
{
	v = v | (v >> 1);
	v = v | (v >> 2);
	v = v | (v >> 4);
	v = v | (v >> 8);
#if SIZE_MAX > 65535
	v = v | (v >> 16);
#endif
#if SIZE_MAX > 4294967295
	v = v | (v >> 32);
#endif
	return v + 1;
}

int
utf8Decode(unsigned char *ch, unsigned len, unsigned *code)
{
	if(len < 1)
		return -1;
	unsigned ch0 = ch[0];
	if(ch0 < 0x80){/* 0xxx xxxx */
		*code = ch0;
		return 1;
	}
	if(ch0 < 0xc0){/* 10xx xxxx */
		// sequence seems to be starting in the middle of a sequence.
		// return error code and advance by one.
		return -1;
	}
	if(len < 2)
		return -1;
	unsigned ch1 = ch[1];
	if(ch0 < 0xe0){/* 110x xxxx */
		*code = ((ch0 & 0x1f) << 6) | (ch1 & 0x3f);
		return 2;
	}
	if(len < 3)
		return -1;
	unsigned ch2 = ch[2];
	if(ch0 < 0xf0){/* 1110 xxxx */
		*code = ((ch0 & 0x0f) << 12) | ((ch1 & 0x3f) << 6) | (ch2 & 0x3f);
		return 3;
	}
	if(len < 4)
		return -1;
	unsigned ch3 = ch[3];
	if(ch0 < 0xf8){/* 1111 0xxx */
		*code = ((ch0 & 0x07) << 18) | ((ch1 & 0x3f) << 12) | ((ch2 & 0x3f) << 6) | (ch3 & 0x3f);
		return 4;
	}
	assert(!"utf8Decode: > 21 bit codes are not standard");
	return -1;
}

static LispRef
lispAllocSymbol(LispMachine *m, char *str)
{
	// don't return nil by accident
	if(m->strings.len == 0)
		m->strings.len = 1;
	size_t slen = strlen(str)+1;
	while(m->strings.len+slen >= m->strings.cap){
		m->strings.cap = nextPow2(m->strings.len+slen);
		void *p = realloc(m->strings.p, m->strings.cap * sizeof m->strings.p[0]);
		if(p == NULL){
			fprintf(stderr, "lispAllocSymbol: realloc failed\n");
			abort();
		}
		m->strings.p = p;
		memset(m->strings.p + m->strings.len, 0, m->strings.cap - m->strings.len);
	}
	LispRef ref = mkref(LISP_INLINE_SYMBOL + m->strings.len, LISP_TAG_SYMBOL);
	memcpy(m->strings.p + m->strings.len, str, slen);
	m->strings.len += slen;
	return ref;
}

static LispRef
lispAllocate(LispMachine *m)
{
	LispRef ref;
	size_t num = 2;
	int didgc = 0;
	// first, try gc.
	if(!m->gclock && (m->mem.cap - m->mem.len) < num){
		lispCollect(m);
		didgc = 1;
	}
recheck:
	if((didgc && 4*m->mem.len >= 3*m->mem.cap) || (m->mem.cap - m->mem.len) < num){
		m->mem.cap = (m->mem.cap == 0) ? 256 : 2*m->mem.cap;
		void *p = realloc(m->mem.ref, m->mem.cap * sizeof m->mem.ref[0]);
		if(p == NULL){
			fprintf(stderr, "lispAllocate: realloc failed (after gc)\n");
			abort();
		}
		m->mem.ref = p;
		lispMemSet(m->mem.ref + m->mem.len, LISP_NIL, m->mem.cap - m->mem.len);
		goto recheck;
	}
	// never return LISP_NIL by accident.
	if(m->mem.len == 0){
		m->mem.len += 2;
		goto recheck;
	}
	ref = mkref(m->mem.len, LISP_TAG_PAIR);
	if(urefval(ref) != m->mem.len || reftag(ref) != LISP_TAG_PAIR){
		fprintf(stderr, "out of address bits in ref: want %zx.%x got %zx.%x\n",
			m->mem.len, LISP_TAG_PAIR, urefval(ref), reftag(ref));
		exit(1);
	}
	m->mem.len += num;
	lispSetCar(m, ref, LISP_NIL);
	lispSetCdr(m, ref, LISP_NIL);
	return ref;
}

static uint32_t
fnv32a(char *str, uint32_t hval)
{
	unsigned char *s;

	for(s = (unsigned char *)str; *s != '\0'; s++){
		hval ^= (uint32_t)*s;
		hval += (hval<<1) + (hval<<4) + (hval<<7) + (hval<<8) + (hval<<24);
	}
	return hval;
}

static int
indexInsert1(LispMachine *m, uint32_t hash, LispRef ref)
{
	size_t i, off;

	for(i = 0; i < m->stringIndex.cap; i++){
		LispRef nref;
		off = (hash + i) & (m->stringIndex.cap - 1);
		nref = m->stringIndex.ref[off];
		if(nref == LISP_NIL){
			m->stringIndex.ref[off] = ref;
			m->stringIndex.len++;
			return 0;
		}
	}
	return -1;
}

static int
indexInsert(LispMachine *m, uint32_t hash, LispRef ref)
{
	size_t i;

	if(3*(m->stringIndex.len/2) >= m->stringIndex.cap){
		LispRef *old;
		size_t oldcap;

		old = m->stringIndex.ref;
		oldcap = m->stringIndex.cap;

		m->stringIndex.cap = m->stringIndex.cap < 16 ? 16 : 2*m->stringIndex.cap;
		m->stringIndex.ref = malloc(m->stringIndex.cap * sizeof m->stringIndex.ref[0]);
		lispMemSet(m->stringIndex.ref, LISP_NIL, m->stringIndex.cap);
		m->stringIndex.len = 0;
		if(old != NULL){
			for(i = 0; i < oldcap; i++){
				LispRef oldref = old[i];
				if(oldref != LISP_NIL){
					uint32_t oldhash;
					oldhash = fnv32a((char *)lispStringPointer(m, oldref), 0);
					if(indexInsert1(m, oldhash, oldref) == -1)
						abort();
				}
			}
			free(old);
		}
	}
	if(indexInsert1(m, hash, ref) == -1)
		abort();
	return 0;
}

static LispRef
indexLookup(LispMachine *m, uint32_t hash, char *str)
{
	LispRef ref;
	size_t i, off;

	for(i = 0; i < m->stringIndex.cap; i++){
		off = (hash + i) & (m->stringIndex.cap - 1);
		ref = m->stringIndex.ref[off];
		if(ref == LISP_NIL)
			break;
		if(!strcmp(str, (char *)lispStringPointer(m, ref))){
			return ref;
		}
	}
	return LISP_NIL;
}

LispRef
lispNumber(LispMachine *m, int v)
{
	LispRef ref = mkref(v, LISP_TAG_INTEGER);
	if((int)refval(ref) == v)
		return ref;
	fprintf(stderr, "lispNumber: integer overflow %d\n", v);
	abort();
}

LispRef
lispCons(LispMachine *m, LispRef a, LispRef d)
{
	LispRef ref;
	LispRef *areg = lispRegister(m, a);
	LispRef *dreg = lispRegister(m, d);
	ref = lispAllocate(m);
	lispSetCar(m, ref, *areg);
	lispSetCdr(m, ref, *dreg);
	lispRelease(m, areg);
	lispRelease(m, dreg);
	return ref;
}

LispRef
lispSymbol(LispMachine *m, char *str)
{
	// skip the string table for one codepoint strings
	int strLen = strlen(str);
	unsigned code;
	int len = utf8Decode(str, strLen+1, &code);
	if(len == strLen && code < LISP_INLINE_SYMBOL)
		return mkref(code, LISP_TAG_SYMBOL);

	// it's not a single codepoint, so look it up in the name table
	LispRef ref;
	uint32_t hash = fnv32a(str, 0);
	if((ref = indexLookup(m, hash, str)) == LISP_NIL){
		// since it's a new name, put it in the name table.
		ref = lispAllocSymbol(m, str);
		indexInsert(m, hash, ref);
	}
	return ref;
}

LispRef
lispParse(LispMachine *m, int justone)
{
	LispRef list = LISP_NIL, prev = LISP_NIL, cons, nval;
	int ltok;
	int dot = 0;

m->gclock++;
	while((ltok = lispLex(m)) != -1){
		tokenAppend(m, '\0');
		switch(ltok){
		default:
			//fprintf(stderr, "unknown token %d: '%c' '%s'\n", ltok, ltok, m->tok);
			list = lispBuiltin(m, LISP_BUILTIN_ERROR);
			goto done;
		case ')':
			goto done;
		case '(':
			nval = lispParse(m, 0);
			goto append;
		case '\'':
			nval = lispParse(m, 1);
			nval = lispCons(m, lispBuiltin(m, LISP_BUILTIN_QUOTE), lispCons(m, nval, LISP_NIL));
			goto append;
		case '.':
			dot++;
			break;
		case ',':
			//fprintf(stderr, "TODO: backquote not implemented yet\n");
			list = lispBuiltin(m, LISP_BUILTIN_ERROR);
			goto done;
		case LISP_TOK_INTEGER:
			nval = lispNumber(m, strtol(m->token.buf, NULL, 0));
			goto append;
		case LISP_TOK_STRING:
			nval = lispCons(m, lispBuiltin(m, LISP_BUILTIN_QUOTE),
				lispCons(m, lispSymbol(m, m->token.buf),
				LISP_NIL));
			goto append;
		case LISP_TOK_SYMBOL:
			nval = lispSymbol(m, m->token.buf);
		append:
			if(justone){
				list = nval;
				goto done;
			}
			if(dot == 0){
				cons = lispCons(m, nval, LISP_NIL);
				if(prev != LISP_NIL)
					lispSetCdr(m, prev, cons);
				else
					list = cons;
				prev = cons;
			} else if(dot == 1){
				if(prev != LISP_NIL){
					lispSetCdr(m, prev, nval);
					dot++;
				} else {
					//fprintf(stderr, "malformed s-expression, bad use of '.'\n");
					list = lispBuiltin(m, LISP_BUILTIN_ERROR);
					goto done;
				}
			} else {
				//fprintf(stderr, "malformed s-expression, bad use of '.'\n");
				list = lispBuiltin(m, LISP_BUILTIN_ERROR);
				goto done;
			}
			break;
		}
	}
	if(justone && ltok == -1)
		list = lispBuiltin(m, LISP_BUILTIN_ERROR);
done:
m->gclock--;
	return list;
}

static int
lispWrite(LispMachine *m, LispPort port, unsigned char *buf, size_t len)
{
	for(size_t i = 0; i < len; i++)
		if(m->ports[port].writebyte(buf[i], m->ports[port].context) == -1)
			return i == 0 ? -1 : (int)i;
	return len;
}

static int
lispRead(LispMachine *m, LispPort port)
{
	return m->ports[port].readbyte(m->ports[port].context);
}

int
lispPrint1(LispMachine *m, LispRef aref, LispPort port)
{
	char buf[128];
	int tag = reftag(aref);
	switch(tag){
	default:
		snprintf(buf, sizeof buf, "print1: unknown atom: val:%zx tag:%x\n", refval(aref), reftag(aref));
		break;
	case LISP_TAG_BUILTIN:
		if(refval(aref) >= 0 && refval(aref) < LISP_NUM_BUILTINS)
			snprintf(buf, sizeof buf, "%s", bltnames[refval(aref)]);
		else
			snprintf(buf, sizeof buf, "blt-0x%zx", refval(aref));
		break;
	case LISP_TAG_EXTREF:
		snprintf(buf, sizeof buf, "extref(#%zx)", refval(aref));
		break;
	case LISP_TAG_PAIR:
		if(aref == LISP_NIL)
			snprintf(buf, sizeof buf, "()");
		else
			snprintf(buf, sizeof buf, "cons(#x%x)", aref);
		break;
	case LISP_TAG_INTEGER:
		snprintf(buf, sizeof buf, "%zd", refval(aref));
		break;
	case LISP_TAG_SYMBOL:{
			unsigned sym = urefval(aref);
			if(sym < LISP_INLINE_SYMBOL)
				snprintf(buf, sizeof buf, "%lc", sym);
			else
				snprintf(buf, sizeof buf, "%s", lispStringPointer(m, aref));
			break;
		}
	}
	lispWrite(m, port, (unsigned char *)buf, strlen(buf));
	return tag;
}

static LispRef
lispPush(LispMachine *m, LispRef val)
{
	m->stack = lispCons(m, val, m->stack);
}

static LispRef
lispPeek(LispMachine *m)
{
	LispRef val = lispCar(m, m->stack);
	return val;
}

static LispRef
lispPop(LispMachine *m)
{
	LispRef val = lispPeek(m);
	m->stack = lispCdr(m, m->stack);
	return val;
}

static void
lispGoto(LispMachine *m, int inst)
{
	m->inst = lispBuiltin(m, inst);
}

void
lispCall(LispMachine *m, int ret, int inst)
{
	lispPush(m, m->envr);
	lispPush(m, lispBuiltin(m, ret));
	lispGoto(m, inst);
}

static void
lispReturn(LispMachine *m)
{
	m->inst = lispPop(m);
	m->envr = lispPop(m);
	m->expr = LISP_NIL;
}

void
lispDefine(LispMachine *m, LispRef sym, LispRef val)
{
	LispRef *pair = lispRegister(m, lispCons(m, sym, val));
	LispRef *env = lispRegister(m, lispCdr(m, m->envr));
	*env = lispCons(m, *pair, *env);
	lispSetCdr(m, m->envr, *env);
	lispRelease(m, pair);
	lispRelease(m, env);
}

static int
lispApplyIf(LispMachine *m)
{
	switch(lispGetBuiltin(m, m->inst)){
	default:
		abort();
	case LISP_STATE_IF0:
		// (if cond then else)
		lispPush(m, m->expr);
		// evaluate condition recursively, return to IF1.
		m->expr = lispCar(m, lispCdr(m, m->expr));
		lispCall(m, LISP_STATE_IF1, LISP_STATE_EVAL);
		return 0;
	case LISP_STATE_IF1:
		// evaluate result as a tail-call, if condition
		// evaluated to #f, skip over 'then' to 'else'.
		m->expr = lispPop(m);
		if(lispIsExtRef(m, m->value)){
			// escape if condition is extref,
			// if may still be a symbol in expr, so resolve it.
			m->expr = lispCons(m, lispBuiltin(m, LISP_BUILTIN_IF), lispCdr(m, m->expr));
			lispGoto(m, LISP_STATE_CONTINUE);
			return 1;
		} else {
			LispRef tmp = lispCdr(m, m->expr);// 'if' -> 'cond'
			tmp = lispCdr(m, tmp); // 'cond' -> 'then'
			if(m->value == lispBuiltin(m, LISP_BUILTIN_FALSE))
				tmp = lispCdr(m, tmp); // 'then' -> 'else'
			m->expr = lispCar(m, tmp);
			lispGoto(m, LISP_STATE_EVAL);
			return 0;
		}
	}
}

static int
lispApplyLet(LispMachine *m)
{
	switch(lispGetBuiltin(m, m->inst)){
	default:
		abort();
	case LISP_STATE_LET0:{
			// (let sym val) -> args,
			// current environment gets sym associated with val.
			LispRef *sym = lispRegister(m, lispCdr(m, m->expr));
			LispRef *val = lispRegister(m, lispCdr(m, *sym));
			*sym = lispCar(m, *sym);
			if(lispIsPair(m, *sym)){
				// scheme shorthand: (let (name args...) body1 body2...).
				LispRef *tmp = lispRegister(m, lispCons(m, lispCdr(m, *sym), *val));
				*val = lispCons(m, lispBuiltin(m, LISP_BUILTIN_LAMBDA), *tmp);
				lispRelease(m, tmp);
				*sym = lispCar(m, *sym);
			} else {
				*val = lispCar(m, *val);
			}
			lispPush(m, *sym);
			m->expr = *val;
			lispRelease(m, sym);
			lispRelease(m, val);
			lispCall(m, LISP_STATE_LET1, LISP_STATE_EVAL);
		}
		return 0;
	case LISP_STATE_LET1:{
			// restore sym from stack, construct (sym . val)
			LispRef *sym = lispRegister(m, lispPop(m));
			*sym = lispCons(m, *sym, m->value);
			// push new (sym . val) just below current env head.
			LispRef *env = lispRegister(m, lispCdr(m, m->envr));
			*env = lispCons(m, *sym, *env);
			lispSetCdr(m, m->envr, *env);
			lispRelease(m, env);
			lispRelease(m, sym);
			lispReturn(m);
		}
		return 0;
	}

}

static int
lispApplySet(LispMachine *m)
{
	switch(lispGetBuiltin(m, m->inst)){
	default:
		abort();
	case LISP_STATE_SET0:{
			// (set! sym val) -> args,
			// current environment gets sym associated with val.
			LispRef *sym = lispRegister(m, lispCdr(m, m->expr));
			LispRef *val = lispRegister(m, lispCdr(m, *sym));
			*sym = lispCar(m, *sym);
			if(lispIsPair(m, *sym)){
				// setter for our object system: (set! ('field obj) value).
				// since set is a special form, we must explicitly evaluate
				// the list elements (but not call apply)
				lispPush(m, *val);
				// set expr to ('field obj) and eval the list (no apply)
				m->expr = *sym;
				lispRelease(m, sym);
				lispRelease(m, val);
				lispCall(m, LISP_STATE_SET1, LISP_STATE_EVAL_ARGS0);
				return 0;
			} else {
				*val = lispCar(m, *val);
				lispPush(m, *sym);
				m->expr = *val;
				lispRelease(m, sym);
				lispRelease(m, val);
				lispCall(m, LISP_STATE_SET2, LISP_STATE_EVAL);
				return 0;
			}
		}
		abort();
	case LISP_STATE_SET1:{
			LispRef *val = lispRegister(m, lispPop(m));
			LispRef *sym = lispRegister(m, m->value);
			*val = lispCar(m, *val);
			lispPush(m, *sym);
			m->expr = *val;
			lispRelease(m, sym);
			lispRelease(m, val);
			lispCall(m, LISP_STATE_SET2, LISP_STATE_EVAL);
			return 0;
		}
	case LISP_STATE_SET2:{
			// restore sym from stack, construct (sym . val)
			LispRef *symp = lispRegister(m, lispPop(m));
			LispRef envr;
			if(lispIsPair(m, *symp)){

				LispRef function = lispCar(m, lispCdr(m, *symp));
				if(lispIsExtRef(m, function)){
					// it's (10 buf) form, ie. buffer indexing
					// assemble a form (set! (10 buf) value) and call/ext
					m->expr = lispCons(m, m->value, LISP_NIL);
					m->expr = lispCons(m, *symp, m->expr);
					m->expr = lispCons(m, lispBuiltin(m, LISP_BUILTIN_SET), m->expr);
					lispRelease(m, symp);
					lispGoto(m, LISP_STATE_CONTINUE);
					return 1;
				}
				LispRef functionHead = lispCar(m, function);
				if(lispIsBuiltin(m, functionHead, LISP_BUILTIN_FUNCTION)){
					// it's (set! ('sym function) ...) form, ie. member access.
					// -> search sym in envr scoped in function
					envr = lispCdr(m, lispCdr(m, function));
					*symp = lispCar(m, *symp);
				} else {
					// else it's unsupported form
					fprintf(stderr, "set!: unsupported form in first argument\n");
					m->value = lispBuiltin(m, LISP_BUILTIN_ERROR);
					lispRelease(m, symp);
					lispReturn(m);
					return 0;
				}
			} else {
				// (set! sym ...) -> search sym in active envr.
				envr = m->envr;
			}
			m->expr = envr;
			lispPush(m, m->value);
			lispPush(m, *symp);
			lispRelease(m, symp);
			lispCall(m, LISP_STATE_SET3, LISP_STATE_SYM_LOOKUP0);
			return 0;
		}
	case LISP_STATE_SET3:{
			LispRef sym = lispPop(m);
			LispRef value = lispPop(m);
			LispRef pair = m->value;
			if(pair != LISP_NIL)
				lispSetCdr(m, pair, value);
			else
				fprintf(stderr, "set!: undefined symbol %s\n", (char *)lispStringPointer(m, sym));
			m->value = LISP_NIL;
			lispReturn(m);
			return 0;
		}
	}
	abort();
}

static int
lispApplyBuiltin(LispMachine *m)
{
	LispRef blt = lispGetBuiltin(m, lispCar(m, m->expr));
	if(blt - LISP_BUILTIN_ADD <= LISP_BUILTIN_DIV - LISP_BUILTIN_ADD){
		LispRef ref0, ref;
		int ires;
		int nterms;
		ref = lispCdr(m, m->expr);
		ref0 = lispCar(m, ref);
		if(lispIsNumber(m, ref0)){
			ires = lispGetInt(m, ref0);
		} else if(lispIsExtRef(m, ref0)){
			// arithmetic on external object, escape.
			lispGoto(m, LISP_STATE_CONTINUE);
			return 1;
		} else {
			m->value = lispBuiltin(m, LISP_BUILTIN_ERROR);
			lispReturn(m);
			return 0;
		}
		m->expr = lispCdr(m, ref);
		nterms = 0;
		while(m->expr != LISP_NIL){
			nterms++;
			ref = lispCar(m, m->expr);
			if(lispIsNumber(m, ref)){
				long long tmp = lispGetInt(m, ref);
				switch(blt){
				case LISP_BUILTIN_ADD:
					ires += tmp;
					break;
				case LISP_BUILTIN_SUB:
					ires -= tmp;
					break;
				case LISP_BUILTIN_MUL:
					ires *= tmp;
					break;
				case LISP_BUILTIN_DIV:
					ires /= tmp;
					break;
				}
			} else {
				m->value = lispBuiltin(m, LISP_BUILTIN_ERROR);
				lispReturn(m);
				return 0;
			}
			m->expr = lispCdr(m, m->expr);
		}
		// special case for unary sub: make it a negate.
		if(blt == LISP_BUILTIN_SUB && nterms == 0)
			ires = -ires;
		m->value = lispNumber(m, ires);
		lispReturn(m);
		return 0;
	} else if(blt == LISP_BUILTIN_ISPAIR){ // (pair? ...)
		m->expr = lispCdr(m, m->expr);
		m->expr = lispCar(m, m->expr);
		if(lispIsPair(m, m->expr))
			m->value =  lispBuiltin(m, LISP_BUILTIN_TRUE);
		else
			m->value =  lispBuiltin(m, LISP_BUILTIN_FALSE);
		lispReturn(m);
		return 0;
	} else if(blt == LISP_BUILTIN_ISEQUAL){ // (equal? ...)
		LispRef args = lispCdr(m, m->expr);
		LispRef arg0 = lispCar(m, args);
		args = lispCdr(m, args);
		LispRef arg = lispCar(m, args);
		if(lispIsExtRef(m, arg0) || lispIsExtRef(m, arg)){
			lispGoto(m, LISP_STATE_CONTINUE);
			return 1;
		} else if(reftag(arg0) != reftag(arg)){
			m->value = lispBuiltin(m, LISP_BUILTIN_FALSE);
			goto eqdone;
		} else if(lispIsNumber(m, arg0) && lispIsNumber(m, arg)){
			if(lispGetInt(m, arg0) != lispGetInt(m, arg)){
				m->value = lispBuiltin(m, LISP_BUILTIN_FALSE);
				goto eqdone;
			}
		} else if(lispIsList(m, arg0) && lispIsList(m, arg)){
			if(arg0 != arg){
				m->value = lispBuiltin(m, LISP_BUILTIN_FALSE);
				goto eqdone;
			}
		} else {
			fprintf(stderr, "equal?: unsupported types %d %d\n", reftag(arg0), reftag(arg));
			m->value = lispBuiltin(m, LISP_BUILTIN_ERROR);
			lispReturn(m);
			return 0;
		}
		m->value = lispBuiltin(m, LISP_BUILTIN_TRUE);
	eqdone:
		lispReturn(m);
		return 0;
	} else if(blt == LISP_BUILTIN_ISLESS){
		LispRef args = lispCdr(m, m->expr);
		LispRef arg0 = lispCar(m, args);
		args = lispCdr(m, args);
		LispRef arg1 = lispCar(m, args);
		m->value = lispBuiltin(m, LISP_BUILTIN_FALSE); // default to false.
		if(lispIsExtRef(m, arg0) || lispIsExtRef(m, arg1)){
			lispGoto(m, LISP_STATE_CONTINUE);
			return 1;
		} else if(lispIsNumber(m, arg0) && lispIsNumber(m, arg1)){
			if(lispGetInt(m, arg0) < lispGetInt(m, arg1))
				m->value = lispBuiltin(m, LISP_BUILTIN_TRUE);
		} else if(lispIsSymbol(m, arg0) && lispIsSymbol(m, arg1)){
			if(strcmp(lispStringPointer(m, arg0), lispStringPointer(m, arg1)) < 0)
				m->value = lispBuiltin(m, LISP_BUILTIN_TRUE);
		} else {
			fprintf(stderr, "less?: unsupported types\n");
			m->value = lispBuiltin(m, LISP_BUILTIN_ERROR);
			lispReturn(m);
			return 0;
		}
		lispReturn(m);
		return 0;
	} else if(blt == LISP_BUILTIN_ISERROR){ // (error? ...)
		m->expr = lispCdr(m, m->expr);
		m->expr = lispCar(m, m->expr);
		if(lispIsError(m, m->expr))
			m->value =  lispBuiltin(m, LISP_BUILTIN_TRUE);
		else
			m->value =  lispBuiltin(m, LISP_BUILTIN_FALSE);
		lispReturn(m);
		return 0;
	} else if(blt == LISP_BUILTIN_SETCAR || blt == LISP_BUILTIN_SETCDR){
		LispRef *cons = lispRegister(m, lispCdr(m, m->expr));
		LispRef *val = lispRegister(m, lispCdr(m, *cons));
		*cons = lispCar(m, *cons); // cons
		*val = lispCar(m, *val); // val
		if(blt == LISP_BUILTIN_SETCAR)
			lispSetCar(m, *cons, *val);
		else
			lispSetCdr(m, *cons, *val);
		lispRelease(m, cons);
		lispRelease(m, val);
		lispReturn(m);
		return 0;
	} else if(blt == LISP_BUILTIN_CAR){
		LispRef tmp = lispCdr(m, m->expr); // ('car thing.. -> (thing..
		tmp = lispCar(m, tmp); // (thing.. -> thing
		m->value = lispCar(m, tmp); // (car thing)
		lispReturn(m);
		return 0;
	} else if(blt == LISP_BUILTIN_CDR){
		LispRef tmp = lispCdr(m, m->expr);
		tmp = lispCar(m, tmp);
		m->value = lispCdr(m, tmp);
		lispReturn(m);
		return 0;
	} else if(blt == LISP_BUILTIN_CONS){
		LispRef tmp0 = lispCdr(m, m->expr);
		LispRef tmp1 = lispCdr(m, tmp0);
		tmp0 = lispCar(m, tmp0);
		tmp1 = lispCar(m, tmp1);
		m->value = lispCons(m, tmp0, tmp1);
		lispReturn(m);
		return 0;
	} else if(blt == LISP_BUILTIN_CALLCC){
		m->expr = lispCdr(m, m->expr);
		LispRef *tmp0 = lispRegister(m, lispCons(m, lispBuiltin(m, LISP_BUILTIN_CONTINUE), m->stack));
		LispRef *tmp1 = lispRegister(m, lispCons(m, *tmp0, LISP_NIL));
		m->expr = lispCons(m, lispCar(m, m->expr), *tmp1);
		lispRelease(m, tmp0);
		lispRelease(m, tmp1);
		lispGoto(m, LISP_STATE_EVAL);
		return 0;
	} else if(blt == LISP_BUILTIN_PRINT1){
		LispRef rest = lispCdr(m, m->expr);
		LispRef port = lispCar(m, rest);
		rest = lispCdr(m, rest);
		m->value = lispCar(m, rest);
		if(lispIsExtRef(m, m->value)){
			// escape extref printing
			lispGoto(m, LISP_STATE_CONTINUE);
			return 1;
		} else {
			lispPrint1(m, m->value, lispGetInt(m, port));
			lispReturn(m);
		}
		return 0;
	} else if(blt == LISP_BUILTIN_EVAL){
		m->expr = lispCar(m, lispCdr(m, m->expr)); // expr = cdar expr
		lispGoto(m, LISP_STATE_EVAL);
		return 0;
	}
}

static int
lispApplySymLookup(LispMachine *m)
{
	switch(lispGetBuiltin(m, m->inst)){
	default:
		abort();
	case LISP_STATE_SYM_LOOKUP0:
		if(m->expr == LISP_NIL){
			m->value = lispBuiltin(m, LISP_BUILTIN_ERROR); // not found
			lispReturn(m);
			return 0;
		} else {
			LispRef envr = m->expr;
			LispRef sym = lispCdr(m, m->stack);
			sym = lispCdr(m, sym);
			sym = lispCar(m, sym);
			LispRef pair = lispCar(m, envr);
			if(lispIsPair(m, pair)){
				LispRef key = lispCar(m, pair);
				if(key == sym){
					m->value = pair;
					lispReturn(m);
					return 0;
				}
			}
			envr = lispCdr(m, envr);
			m->expr = envr;
			return 0;
		}
	}
}

static int
lispEvalArgs(LispMachine *m)
{
	switch(lispGetBuiltin(m, m->inst)){
	default:
		abort();
	case LISP_STATE_EVAL_ARGS0:{
			// construct evaluation state: (expr . (expr-cell . value-cell))
			LispRef *state = lispRegister(m, lispCons(m, LISP_NIL, LISP_NIL));
			// push the first (expr-cell . value-cell) pair
			// cdr of this holds the return value once entire list is evaluated.
			lispPush(m, *state);
			*state = lispCons(m, m->expr, *state);
			lispPush(m, *state);
			lispRelease(m, state);
			lispGoto(m, LISP_STATE_EVAL_ARGS1);
			return 0;
		}
	case LISP_STATE_EVAL_ARGS1:{
			// load top of stack to state.
			LispRef *state = lispRegister(m, lispPeek(m));
			// store latest m->value to value-cell.
			lispSetCar(m, lispCdr(m, *state), m->value);
			// load expr-cell to register
			LispRef *exprCell = lispRegister(m, lispCar(m, *state)); // expr-cell = car(state)
			if(lispIsPair(m, *exprCell)){
				lispRelease(m, state);
				lispRelease(m, exprCell);
				lispGoto(m, LISP_STATE_EVAL_ARGS3);
				return 0;
			}
			// this following bit is non-standard, it allows one to call a function
			// with the notation (fn . args) or (fn arg1 arg2 . rest), effectively
			// splicing a list 'args' in to the argument list, analogous to how
			// varargs are declared in standard scheme functions. by allowing the
			// dotted notation for apply, there is no need an apply built-in.
			// unfortunately, of course the dotted tail must not be a literal list,
			// so defining apply as a function is still going to be necessary.
			if(*exprCell != LISP_NIL){
				m->expr = *exprCell;
				lispSetCar(m, *state, LISP_NIL);
				lispRelease(m, state);
				lispRelease(m, exprCell);
				lispCall(m, LISP_STATE_EVAL_ARGS2, LISP_STATE_EVAL);
				return 0;
			}

			// arg list evaluation is done
			assert(*exprCell == LISP_NIL);
			lispRelease(m, state);
			lispRelease(m, exprCell);
			lispPop(m);
			m->value = lispCdr(m, lispPop(m));
			lispReturn(m);
			return 0;
		}
	case LISP_STATE_EVAL_ARGS3:{
			// exprcell is a pair (a list)
			LispRef *state = lispRegister(m, lispPeek(m));
			LispRef *exprCell = lispRegister(m, lispCar(m, *state)); // expr-cell = car(state)
			// load new expr from expr-cell and set expr-cell to next expr.
			m->expr = lispCar(m, *exprCell); // expr = (car expr-cell)
			*exprCell = lispCdr(m, *exprCell); // expr-cell = (cdr expr-cell)
			lispSetCar(m, *state, *exprCell); //  car(state) = (cdr expr-cell)

			// allocate new value-cell, link cdr of current value-cell to it and set value-cell to the new one.
			LispRef *valueCell = lispRegister(m, lispCons(m, LISP_NIL, LISP_NIL));
			*exprCell = lispCdr(m, *state);
			lispSetCdr(m, *exprCell, *valueCell);
			lispSetCdr(m, *state, *valueCell);

			// release registers
			lispRelease(m, state);
			lispRelease(m, exprCell);
			lispRelease(m, valueCell);
			// call "apply" on the current expr and have it return to ARGS1
			// state.
			lispCall(m, LISP_STATE_EVAL_ARGS1, LISP_STATE_EVAL);
			return 0;
		}
	case LISP_STATE_EVAL_ARGS2:{
			// load top of stack to state.
			LispRef *state = lispRegister(m, lispPeek(m));
			// store new m->value to the rest of value chain.
			// vast majority of time m->value is nil, except for the
			// non-standard arg splicing situation from above.
			lispSetCdr(m, lispCdr(m, *state), m->value);
			lispRelease(m, state);
			// pop evaluation state
			lispPop(m);
			// pop the head and return its value-cell.
			m->value = lispCdr(m, lispPop(m));
			lispReturn(m);
		}
	}
}

int
lispStep(LispMachine *m)
{
again:
	if(!lispIsBuiltinTag(m, m->inst)){
		fprintf(stderr, "lispStep: inst is not built-in, stack corruption?\n");
		abort();
	}
	switch(lispGetBuiltin(m, m->inst)){
	default:
		fprintf(stderr, "lispStep: invalid instruction %zd, bailing out.\n", refval(m->inst));

	case LISP_STATE_IF0:
	case LISP_STATE_IF1:
		if(lispApplyIf(m) == 1)
			return 1;
		goto again;
	case LISP_STATE_LET0:
	case LISP_STATE_LET1:
		if(lispApplyLet(m) == 1)
			return 1;
		goto again;
	case LISP_STATE_SYM_LOOKUP0:
		if(lispApplySymLookup(m) == 1)
			return 1;
		goto again;
	case LISP_STATE_SYM_LOOKUP1:{
			LispRef sym = lispPop(m);
			if(m->value == lispBuiltin(m, LISP_BUILTIN_ERROR)){
				fprintf(stderr, "symbol %s not found\n", lispStringPointer(m, sym));
			} else {
				// extract value part of (sym . value)
				m->value = lispCdr(m, m->value);
			}
			lispReturn(m);
			goto again;
		}
	case LISP_STATE_SET0:
	case LISP_STATE_SET1:
	case LISP_STATE_SET2:
	case LISP_STATE_SET3:
		if(lispApplySet(m) == 1)
			return 1;
		goto again;
	case LISP_STATE_EVAL_ARGS0:
	case LISP_STATE_EVAL_ARGS1:
	case LISP_STATE_EVAL_ARGS2:
	case LISP_STATE_EVAL_ARGS3:
		if(lispEvalArgs(m) == 1)
			return 1;
		goto again;
	case LISP_STATE_BUILTIN0:
		if(lispApplyBuiltin(m) == 1)
			return 1;
		goto again;
	case LISP_STATE_CONTINUE:
		lispReturn(m);
		goto again;
	case LISP_STATE_RETURN:
		return 0;
	case LISP_STATE_EVAL:
		if(lispIsAtom(m, m->expr)){
			// atom evaluates to itself, these are currently
			// numbers, #true, #false, #greater, #equal, #less
			m->value = m->expr;
			lispReturn(m);
			goto again;
		} else if(lispIsSymbol(m, m->expr)){
			// symbol evaluates to the looked up value in
			// current environment
			lispPush(m, m->expr); // push the symbol to look for
			m->expr = m->envr; // environment to find it in.
			lispCall(m, LISP_STATE_SYM_LOOKUP1, LISP_STATE_SYM_LOOKUP0);
			goto again;
		} else if(lispIsPair(m, m->expr)){
			// form is (something), so this is 'application'
			// evaluate list head first, it is typically a lambda or a symbol.
			// then see if it's a special form (quote, lambda, function, if, define) 
			LispRef head;
			lispPush(m, m->expr);
			m->expr = lispCar(m, m->expr);
			lispCall(m, LISP_STATE_SPECIAL_FORMS, LISP_STATE_EVAL);
			goto again;
		}
	case LISP_STATE_SPECIAL_FORMS:{
			// list head is evaluated in m->value, rest of the list is
			// un-evaluated.
			LispRef head = m->value;
			m->value = LISP_NIL;
			m->expr = lispPop(m);

			// detect special forms.
			if(lispIsBuiltinTag(m, head)){
				LispRef blt = lispGetBuiltin(m, head);
				if(blt == LISP_BUILTIN_IF){
					// (if cond then else)
					lispGoto(m, LISP_STATE_IF0);
					goto again;
				}
				if(blt == LISP_BUILTIN_FUNCTION){
					// (function (lambda args body) envr): return self.
					m->value = m->expr;
					lispReturn(m);
					goto again;
				}
				if(blt == LISP_BUILTIN_CONTINUE){
					// continuation: return self.
					m->value = m->expr;
					lispReturn(m);
					goto again;
				}
				if(blt == LISP_BUILTIN_LET){
					// (let sym value)
					// (let (sym args) body)
					lispGoto(m, LISP_STATE_LET0);
					goto again;
				}
				if(blt == LISP_BUILTIN_SET){
					// (set! sym value)
					// (set! ('field obj) value)
					lispGoto(m, LISP_STATE_SET0);
					goto again;
				}
				if(blt == LISP_BUILTIN_SCOPE){
					// (scope vars body)
					// construct a new environment with only vars in it, then
					// continue to eval body.
					LispRef *newenvr = lispRegister(m, LISP_NIL);
					LispRef *vars = lispRegister(m, lispCdr(m, m->expr));
					LispRef *body = lispRegister(m, lispCdr(m, *vars));
					*vars = lispCar(m, *vars);
					*body = lispCar(m, *body);
					for(; *vars != LISP_NIL; *vars = lispCdr(m, *vars)){
						LispRef var = lispCar(m, *vars);
						LispRef env = m->envr;
						while(env != LISP_NIL){
							LispRef pair = lispCar(m, env);
							if(lispIsPair(m, pair)){
								LispRef key = lispCar(m, pair);
								if(key == var){
									*newenvr = lispCons(m, pair, *newenvr);
									break;
								}
							}
							env = lispCdr(m, env);
						}
					}
					m->envr = *newenvr;
					m->expr = *body;
					lispRelease(m, vars);
					lispRelease(m, newenvr);
					lispRelease(m, body);
					lispGoto(m, LISP_STATE_EVAL);
					goto again;
				}
				if(blt == LISP_BUILTIN_LAMBDA){
					// (lambda args body) -> (function (lambda args body) envr)
					LispRef *tmp = lispRegister(m, lispCons(m, m->expr, m->envr));
					m->value = lispCons(m, lispBuiltin(m, LISP_BUILTIN_FUNCTION), *tmp);
					lispRelease(m, tmp);
					lispReturn(m);
					goto again;
				}
				if(blt == LISP_BUILTIN_QUOTE){
					// (quote args) -> args
					m->value = lispCar(m, lispCdr(m, m->expr)); // expr = cdar expr
					lispReturn(m);
					goto again;
				}
			}

			// at this point we know it is a list, and that it is
			// not a special form. evaluate args, then apply.
			lispCall(m, LISP_STATE_APPLY, LISP_STATE_EVAL_ARGS0);
			goto again;
	case LISP_STATE_APPLY:
			m->expr = m->value;
			head = lispCar(m, m->expr);
			if(lispIsBuiltinTag(m, head)){
				lispGoto(m, LISP_STATE_BUILTIN0);
				goto again;
			} else if(lispIsExtRef(m, head)){
				// we are applying an external object to lisp arguments..
				lispGoto(m, LISP_STATE_CONTINUE);
				return 1;
			} else if(lispIsSymbol(m, head) || lispIsNumber(m, head)){
				// this is the accessor (getter/setter) for our "namespaces".
				// it returns an entry from a function's environment, so that
				// an object constructor can become as simple as
				//		(let (vec3 x y z) (lambda()))
				// which can then be used as
				//		(let r0 (vec3 1 2 3))
				//		(+ ('x r0) ('y r0) ('z r0))
				// note the prefix notation for field access. there is a
				// special form set! for this notation, so that
				//		(set! ('x r0) 7)
				// does what you might expect.
				LispRef function = lispCar(m, lispCdr(m, m->expr));
				if(function == LISP_NIL){
					m->value = lispBuiltin(m, LISP_BUILTIN_ERROR);
					lispReturn(m);
					goto again;
				}
				if(lispIsExtRef(m, function)){
					m->expr = lispCons(m, function, LISP_NIL);
					m->expr = lispCons(m, head, m->expr);
					lispGoto(m, LISP_STATE_CONTINUE);
					return 1;
				}
				// if it is a function (created by lambda), search the symbol
				// in its captured environment
				LispRef functionHead = lispCar(m, function);
				if(lispIsBuiltin(m, functionHead, LISP_BUILTIN_FUNCTION)){
					m->expr = lispCdr(m, lispCdr(m, function));
					lispPush(m, head);
					lispCall(m, LISP_STATE_SYM_LOOKUP1, LISP_STATE_SYM_LOOKUP0);
					goto again;
				} else {
					fprintf(stderr, "symbol finding in a non-object\n");
					m->value = lispBuiltin(m, LISP_BUILTIN_ERROR);
					lispReturn(m);
					goto again;
				}

			} else if(lispIsPair(m, head)){

				// form is ((function (lambda args body)) args), or a continuation.
				// ((function (lambda args . body) . envr) args) -> (body),
				// with a new environment

				LispRef function, lambda;
				function = lispCar(m, m->expr);
				if(function == LISP_NIL){
					m->value = lispBuiltin(m, LISP_BUILTIN_ERROR);
					lispReturn(m);
					goto again;
				}
				head = lispCar(m, function);
				if(!lispIsBuiltinTag(m, head)){
					m->value = lispBuiltin(m, LISP_BUILTIN_ERROR);
					lispReturn(m);
					goto again;
				}
				if(lispIsBuiltin(m, head, LISP_BUILTIN_CONTINUE)){
					// ((continue . stack) return-value)
					m->stack = lispCdr(m, function);
					m->value = lispCdr(m, m->expr);
					if(lispIsPair(m, m->value))
						m->value = lispCar(m, m->value);
					lispReturn(m);
					goto again; 
				} else if(lispIsBuiltin(m, head, LISP_BUILTIN_FUNCTION)){
					lispGoto(m, LISP_STATE_BETA0);
					goto again;
				} else {
					if(refval(head) != LISP_BUILTIN_FUNCTION){
						fprintf(stderr, "applying list with non-function head\n");
						m->value = lispBuiltin(m, LISP_BUILTIN_ERROR);
						lispReturn(m);
						goto again;
					}
				}
			} else {
				fprintf(stderr, "apply: head element has unrecognized form\n");
				lispReturn(m);
				goto again;
			}

	case LISP_STATE_BETA0:{
				LispRef function = lispCar(m, m->expr);
				LispRef lambda = lispCar(m, lispCdr(m, function));
				m->envr = lispCdr(m, lispCdr(m, function));

				LispRef *argnames = lispRegister(m, lispCdr(m, lambda));
				LispRef *args = lispRegister(m, lispCdr(m, m->expr)); // args = cdr expr
				if(lispIsPair(m, *argnames)){
					*argnames = lispCar(m, *argnames);
					lispPush(m, *argnames);
					lispPush(m, *args);
					lispRelease(m, argnames);
					lispRelease(m, args);
					lispGoto(m, LISP_STATE_BETA1);
					goto again;
				} else {
					lispPush(m, *argnames);
					lispPush(m, *args);
					lispRelease(m, argnames);
					lispRelease(m, args);
					lispGoto(m, LISP_STATE_BETA2);
					goto again;
				}
			}
	case LISP_STATE_BETA1:{
				// loop over argnames and args simultaneously, cons
				// them as pairs to the environment
				LispRef *args = lispRegister(m, lispPop(m)); // args = cdr expr
				LispRef *argnames = lispRegister(m, lispPop(m));
				if(lispIsPair(m, *argnames) && lispIsPair(m, *args)){
					LispRef *pair = lispRegister(m, lispCons(m,
						lispCar(m, *argnames),
						lispCar(m, *args)));
					m->envr = lispCons(m, *pair, m->envr);
					*argnames = lispCdr(m, *argnames);
					*args = lispCdr(m, *args);
					lispPush(m, *argnames);
					lispPush(m, *args);
					lispRelease(m, argnames);
					lispRelease(m, args);
					lispRelease(m, pair);
					lispGoto(m, LISP_STATE_BETA1);
					goto again;
				} else {
					lispPush(m, *argnames);
					lispPush(m, *args);
					lispRelease(m, argnames);
					lispRelease(m, args);
					lispGoto(m, LISP_STATE_BETA2);
					goto again;
				}
			}
	case LISP_STATE_BETA2:{
				// scheme-style variadic: if argnames list terminates in a
				// symbol instead of LISP_NIL, associate the rest of argslist
				// with it. notice: (lambda x (body)) also lands here.
				LispRef *args = lispRegister(m, lispPop(m)); // args = cdr expr
				LispRef *argnames = lispRegister(m, lispPop(m));
				if(*argnames != LISP_NIL && !lispIsPair(m, *argnames)){
					LispRef *pair = lispRegister(m, lispCons(m, *argnames, *args));
					m->envr = lispCons(m, *pair, m->envr);
					lispRelease(m, pair);
				} else if(*argnames != LISP_NIL || *args != LISP_NIL){
					fprintf(stderr, "mismatch in number of function args\n");
					m->value = lispBuiltin(m, LISP_BUILTIN_ERROR);
					lispReturn(m);
					goto again;
				}

				// push a new 'environment head' in.
				m->envr = lispCons(m, LISP_NIL, m->envr);
				// clear the registers.
				lispRelease(m, argnames);
				lispRelease(m, args);

				// parameters are bound, pull body from lambda to m->expr.
				LispRef function = lispCar(m, m->expr); // function = car expr
				LispRef lambda = lispCar(m, lispCdr(m, function));
				m->expr = lispCdr(m, lispCdr(m, lambda));
				lispPush(m, m->expr);
				lispGoto(m, LISP_STATE_BETA3);
				goto again;
			}
	case LISP_STATE_BETA3:{
				LispRef tmp = lispPeek(m);
				if(tmp != LISP_NIL){
					m->expr = lispCar(m, tmp);
					tmp = lispCdr(m, tmp);
					lispSetCar(m, m->stack, tmp);
					if(tmp != LISP_NIL){
						lispCall(m, LISP_STATE_BETA3, LISP_STATE_EVAL);
						goto again;
					} else { // tail call
						lispPop(m);
						lispGoto(m, LISP_STATE_EVAL);
						goto again;
					}
				}
				lispPop(m); // pop expr.
				lispReturn(m);
				goto again;
			}
		}
		fprintf(stderr, "lispStep eval: unrecognized form: ");
		//eval_print(m);
		fprintf(stderr, "\n");
		m->value = lispBuiltin(m, LISP_BUILTIN_ERROR);
		lispReturn(m);
		goto again;
	}
}

/*
 *	The following two routines implement a copying garbage collector.
 *
 *	Lispcopy takes two machines as parameters, and a reference in oldm.
 *	If the element is a pair, check whether it is a forwarding entry or not.
 *	If yes, return what the forwarding entry has.
 *	Else, append the (unmodified) pair to newm and change the pair in oldm into
 *	a forwarding entry to the pair's location in newm.
 *
 *	Strings, symbol names and their index are eternal constants which can be
 *	freed entirely if printing of constants or symbol names isn't required and
 *	memory is somehow really that scarce.
 */
static LispRef
lispCopy(LispMachine *newm, LispMachine *oldm, LispRef ref)
{
	if(lispIsList(oldm, ref)){
		if(ref == LISP_NIL)
			return LISP_NIL;
		if(lispIsBuiltin(oldm, lispCar(oldm, ref), LISP_BUILTIN_FORWARD))
			return lispCdr(oldm, ref);
		// load from old, cons in new. this is the copy phase: any references in
		// the new cell will point to something in oldm.
		LispRef newref = lispCons(newm, lispCar(oldm, ref), lispCdr(oldm, ref));
		// rewrite oldm to point to new.
		lispSetCar(oldm, ref, lispBuiltin(oldm, LISP_BUILTIN_FORWARD));
		lispSetCdr(oldm, ref, newref);
		return newref;
	}
	return ref;
}

/*
 *	This is the main garbage collector routine. The idea is to create the root
 *	set from vm registers using lispCopy to an otherwise empty machine.
 *	After the root set is formed, it proceeds in a breath-first manner, calling
 *	lispCopy on all of its memory locations until everything has been collapsed.
 */
void
lispCollect(LispMachine *m)
{
	if(m->mem.len == 0)
		return;

	size_t oldlen = m->mem.len;
	// There are two "memories" within m, namely m->mem and m->copy.
	// we create a copy of m called oldm here, and then exchange m->mem and
	// m->copy. It's a waste of memory, but it avoids one big malloc per gc.
	LispMachine oldm;
	memcpy(&oldm, m, sizeof oldm);
	memcpy(&m->mem, &oldm.copy, sizeof m->mem);
	if(m->mem.cap != oldm.mem.cap){
		fprintf(stderr, "resizing from %zu to %zu\n", m->mem.cap, oldm.mem.cap);
		m->mem.cap = oldm.mem.cap;
		void *p = realloc(m->mem.ref, m->mem.cap * sizeof m->mem.ref[0]);
		if(p == NULL){
			fprintf(stderr, "realloc failed during garbage collection. whoops\n");
			abort();
		}
		m->mem.ref = p;
		lispMemSet(m->mem.ref, LISP_NIL, m->mem.cap);
	}
	m->mem.len = 0;
	memcpy(&m->copy, &oldm.mem, sizeof m->copy);
	m->gclock++;

	// copy the "dynamic" registers.
	uint32_t reguse = oldm.reguse;
	for(size_t i = 0; i < nelem(m->regs); i++)
		if((reguse & (1<<i)) != 0)
			m->regs[i] = lispCopy(m, &oldm, oldm.regs[i]);
	m->reguse = reguse;

	m->value = lispCopy(m, &oldm, oldm.value);
	m->expr = lispCopy(m, &oldm, oldm.expr);
	m->envr = lispCopy(m, &oldm, oldm.envr);
	m->stack = lispCopy(m, &oldm, oldm.stack);

	for(size_t i = 2; i < m->mem.len; i++)
		m->mem.ref[i] = lispCopy(m, &oldm, m->mem.ref[i]);

//if(1)fprintf(stderr, "collected: from %zu to %zu\n", oldlen, m->mem.len);

	m->gclock--;
}

void
lispInit(LispMachine *m)
{
	// install initial environment (let built-ins)
	m->envr = lispCons(m, LISP_NIL, LISP_NIL);
	for(size_t i = 0; i < LISP_NUM_BUILTINS; i++){
		LispRef sym = lispSymbol(m, bltnames[i]);
		lispDefine(m, sym, lispBuiltin(m, i));
	}
}

int
lispSetPort(LispMachine *m, LispPort port, int (*writebyte)(int ch, void *ctx), int (*readbyte)(void *ctx), int (*unreadbyte)(int ch, void *ctx), void *ctx)
{
	if(port >= m->portscap){
		size_t portscap = m->portscap == 0 ? port+1 : m->portscap * 2;
		void *ports = realloc(m->ports, portscap * sizeof m->ports[0]);
		if(ports == NULL)
			return -1;
		m->ports = ports;
		m->portscap = portscap;
	}
	m->ports[port].writebyte = writebyte;
	m->ports[port].readbyte = readbyte;
	m->ports[port].unreadbyte = unreadbyte;
	m->ports[port].context = ctx;
	return 0;
}

LispRef
lispExtAlloc(LispMachine *m)
{
	if(m->extrefs.len == m->extrefs.cap){
		m->extrefs.cap = nextPow2(m->extrefs.cap);
		void *p = realloc(m->extrefs.p, m->extrefs.cap * sizeof m->extrefs.p[0]);
		if(p == NULL){
			fprintf(stderr, "lispExtAlloc: realloc failed\n");
			abort();
		}
		m->extrefs.p = p;
	}
	LispRef ref = mkref(m->extrefs.len, LISP_TAG_EXTREF);
	assert(urefval(ref) == m->extrefs.len);
	m->extrefs.len++;
	return ref;
}

int
lispExtSet(LispMachine *m, LispRef ref, void *obj, void *type)
{
	if(lispIsExtRef(m, ref)){
		size_t i = refval(ref);
		m->extrefs.p[i].obj = obj;
		m->extrefs.p[i].type = type;
		return 0;
	}
	abort();
	return -1;
}

int
lispExtGet(LispMachine *m, LispRef ref, void **obj, void **type)
{
	if(lispIsExtRef(m, ref)){
		size_t i = refval(ref);
		if(obj != NULL)
			*obj = m->extrefs.p[i].obj;
		if(type != NULL)
			*type = m->extrefs.p[i].type;
		return 0;
	}
	abort();
	return -1;
}
