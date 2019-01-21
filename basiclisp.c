
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <assert.h>
#include "basiclisp.h"


#define nelem(x) (sizeof(x)/sizeof(x[0]))

static char *bltnames[] = {
[LISP_BUILTIN_IF] = "if",
[LISP_BUILTIN_BETA] = "beta",
[LISP_BUILTIN_CONTINUE] = "continue",
[LISP_BUILTIN_DEFINE] = "define",
[LISP_BUILTIN_CAPTURE] = "capture",
[LISP_BUILTIN_LAMBDA] = "lambda",
[LISP_BUILTIN_QUOTE] = "quote",
[LISP_BUILTIN_CALLCC] = "call-with-current-continuation",
[LISP_BUILTIN_SET] = "set!",
[LISP_BUILTIN_SETCAR] = "set-car!",
[LISP_BUILTIN_SETCDR] = "set-cdr!",
[LISP_BUILTIN_CONS] = "cons",
[LISP_BUILTIN_CAR] = "car",
[LISP_BUILTIN_CDR] = "cdr",
[LISP_BUILTIN_EVAL] = "eval",
[LISP_BUILTIN_ADD] = "+",
[LISP_BUILTIN_SUB] = "-",
[LISP_BUILTIN_MUL] = "*",
[LISP_BUILTIN_DIV] = "/",
[LISP_BUILTIN_BITIOR] = "bitwise-ior",
[LISP_BUILTIN_BITAND] = "bitwise-and",
[LISP_BUILTIN_BITXOR] = "bitwise-xor",
[LISP_BUILTIN_BITNOT] = "bitwise-not",
[LISP_BUILTIN_REM] = "remainder",
[LISP_BUILTIN_ISPAIR] = "pair?",
[LISP_BUILTIN_ISEQ] = "equal?",
[LISP_BUILTIN_ISLESS] = "less?",
[LISP_BUILTIN_ISERROR] = "error?",
[LISP_BUILTIN_TRUE] = "#t",
[LISP_BUILTIN_FALSE] = "#f",
[LISP_BUILTIN_PRINT1] = "print1",
[LISP_BUILTIN_ERROR] = "error",
};

lispref_t *
lispregister(Mach *m, lispref_t val)
{
	uint32_t reguse = m->reguse;
	for(size_t i = 0; i < nelem(m->regs); i++){
		if((reguse & (1<<i)) == 0){
			lispref_t *regp = &m->regs[i];
			m->reguse = reguse | (1<<i);
			*regp = val;
			return regp;
		}
	}
	fprintf(stderr, "lispregister: ran out of registers\n");
	abort();
	return NULL;
}

void
lisprelease(Mach *m, lispref_t *regp)
{
	size_t i = regp - m->regs;
	if(i >= nelem(m->regs))
		abort();
	*regp = LISP_NIL;
	m->reguse &= ~(1<<i);
}

#if 0
static lispref_t
mkref(int val, int tag)
{
	return ((val<<LISP_TAG_BITS)|(tag&LISP_TAG_MASK));
}

static int
reftag(lispref_t ref)
{
	return ref & LISP_TAG_MASK;
}

static intptr_t
refval(lispref_t ref)
{
	return (intptr_t)ref >> LISP_TAG_BITS;
}

static size_t
urefval(lispref_t ref)
{
	return ref >> LISP_TAG_BITS;
}
#else
static lispref_t
mkref(int val, int tag)
{
	lispref_t ref = LISP_TAG_BIT >> tag;
	return ref | ((ref - 1) & val);
}

static int
reftag(lispref_t ref)
{
#if 1
	lispref_t nlz;
	__asm__("lzcnt %[ref], %[nlz]"
		: [nlz]"=r"(nlz)
		: [ref]"r"(ref)
		: "cc"
	);
	return (int)nlz;
#else
	for(int i = 0; i < 8; i++){
		lispref_t tmp = ref + (LISP_TAG_BIT >> i);
		if(tmp < ref)
			return i;
		ref = tmp;
	}
	return -1;
#endif
}

static intptr_t
refval(lispref_t ref)
{
	return (LISP_VAL_MASK >> reftag(ref)) & ref;
}

static size_t
urefval(lispref_t ref)
{
	return (LISP_VAL_MASK >> reftag(ref)) & ref;
}
#endif
lispref_t
lispmkbuiltin(Mach *m, int val)
{
	return mkref(val, LISP_TAG_BUILTIN);
}

static lispref_t *
pointer(Mach *m, lispref_t ref)
{
	size_t off = urefval(ref);
	if(off <= 0 || off >= m->mem.len){
		fprintf(stderr, "dereferencing an out of bounds reference: %x\n", ref);
		abort();
	}
	return m->mem.ref + off;
}

static char *
strpointer(Mach *m, lispref_t ref)
{
	size_t off = urefval(ref);
	if(off <= 0 || off >= m->strings.len){
		fprintf(stderr, "dereferencing an out of bounds string reference: %x off: %zu\n", ref, off);
		abort();
	}
	return m->strings.p + off;
}

lispref_t
lispcar(Mach *m, lispref_t base)
{
	lispref_t *p = pointer(m, base);
	return p[LISP_CAR_OFFSET];
}

lispref_t
lispcdr(Mach *m, lispref_t base)
{
	lispref_t *p = pointer(m, base);
	return p[LISP_CDR_OFFSET];
}

static lispref_t
lispsetcar(Mach *m, lispref_t base, lispref_t obj)
{
	lispref_t *p = pointer(m, base);
	p[LISP_CAR_OFFSET] = obj;
	return obj;
}

static lispref_t
lispsetcdr(Mach *m, lispref_t base, lispref_t obj)
{
	lispref_t *p = pointer(m, base);
	p[LISP_CDR_OFFSET] = obj;
	return obj;
}

static void
lispmemset(lispref_t *mem, lispref_t val, size_t len)
{
	for(size_t i = 0; i < len; i++)
		mem[i] = val;
}

static int
isatom(Mach *m, lispref_t a)
{
	int tag = reftag(a);
	return tag != LISP_TAG_SYMBOL && tag != LISP_TAG_PAIR;
}

int
lispsymbol(Mach *m, lispref_t a)
{
	return reftag(a) == LISP_TAG_SYMBOL;
}

int
lispnumber(Mach *m, lispref_t a)
{
	return reftag(a) == LISP_TAG_INTEGER;
}

int
lispgetint(Mach *m, lispref_t ref)
{
	if(lispnumber(m, ref))
		return refval(ref);
	abort();
	return -1;
}

int
lispbuiltin1(Mach *m, lispref_t ref)
{
	return reftag(ref) == LISP_TAG_BUILTIN;
}

int
lispgetbuiltin(Mach *m, lispref_t ref)
{
	if(lispbuiltin1(m, ref))
		return urefval(ref);
	abort();
	return -1;
}

int
lispbuiltin(Mach *m, lispref_t ref, int builtin)
{
	return lispbuiltin1(m, ref) && lispgetbuiltin(m, ref) == builtin;
}

int
lispextref(Mach *mach, lispref_t a)
{
	return reftag(a) == LISP_TAG_EXTREF;
}

int
lispispair(Mach *m, lispref_t a)
{
	return reftag(a) == LISP_TAG_PAIR;
}

int
lisppair(Mach *m, lispref_t a)
{
	return reftag(a) == LISP_TAG_PAIR && a != LISP_NIL;
}

int
lisperror(Mach *m, lispref_t a)
{
	return lispbuiltin(m, a, LISP_BUILTIN_ERROR);
}

static int
iswhite(int c)
{
	switch(c){
	case '\t': case '\n': case '\v': case '\f': case '\r': case ' ':
		return 1;
	}
	return 0;
}

static int
isbreak(int c)
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
ishexchar(int c)
{
	switch(c){
	case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
		return 1;
	}
	return 0;
}

static int
isnumchar(int c)
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
tokappend(Mach *m, int ch)
{
	if(m->toklen == m->tokcap){
		m->tokcap = (m->tokcap == 0) ? 256 : 2*m->tokcap;
		void *p = realloc(m->tok, m->tokcap);
		if(p == NULL){
			fprintf(stderr, "tokappend: realloc failed\n");
			abort();
		}
		m->tok = p;
	}
	m->tok[m->toklen] = ch;
	m->toklen++;
}

static void
tokclear(Mach *m)
{
	m->toklen = 0;
}

static int
lex(Mach *m)
{
	int isinteger, ishex;
	int ch, peekc;

again:
	tokclear(m);
	if((ch = m->ports[0].readbyte(m->ports[0].context)) == -1)
		return -1;
	if(iswhite(ch)){
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
			if(!isnumchar(ch) && !(ishex && ishexchar(ch))){
				if(isbreak(ch))
					break;
				goto casesym;
			}
			if(ch == 'x')
				ishex = 1;
			tokappend(m, ch);
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
			tokappend(m, ch);
			ch = m->ports[0].readbyte(m->ports[0].context);
			if(ch == '\n')
				m->lineno++;
		}
		return LISP_TOK_STRING;

	// symbol is any string of nonbreak characters not starting with a number
	default:
	casesym:
		while(ch != -1 && !isbreak(ch)){
			tokappend(m, ch);
			ch = m->ports[0].readbyte(m->ports[0].context);
		}
		m->ports[0].unreadbyte(ch, m->ports[0].context);
		return LISP_TOK_SYMBOL;
	}
	return -1;
}

static size_t
nextpow2(size_t v)
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

static lispref_t
allocsymbol(Mach *m, char *str)
{
	// don't return nil by accident
	if(m->strings.len == 0)
		m->strings.len = 1;
	size_t slen = strlen(str)+1;
	while(m->strings.len+slen >= m->strings.cap){
		m->strings.cap = nextpow2(m->strings.len+slen);
		void *p = realloc(m->strings.p, m->strings.cap * sizeof m->strings.p[0]);
		if(p == NULL){
			fprintf(stderr, "allocsymbol: realloc failed\n");
			abort();
		}
		m->strings.p = p;
		memset(m->strings.p + m->strings.len, 0, m->strings.cap - m->strings.len);
	}
	lispref_t ref = mkref(m->strings.len, LISP_TAG_SYMBOL);
	memcpy(m->strings.p + m->strings.len, str, slen);
	m->strings.len += slen;
	return ref;
}

static lispref_t
allocpair(Mach *m)
{
	lispref_t ref;
	size_t num = 2;
	int didgc = 0;
	// first, try gc.
	if(!m->gclock && (m->mem.cap - m->mem.len) < num){
		lispcollect(m);
		didgc = 1;
	}
recheck:
	if((didgc && 4*m->mem.len >= 3*m->mem.cap) || (m->mem.cap - m->mem.len) < num){
		m->mem.cap = (m->mem.cap == 0) ? 256 : 2*m->mem.cap;
		void *p = realloc(m->mem.ref, m->mem.cap * sizeof m->mem.ref[0]);
		if(p == NULL){
			fprintf(stderr, "allocpair: realloc failed (after gc)\n");
			abort();
		}
		m->mem.ref = p;
		lispmemset(m->mem.ref + m->mem.len, LISP_NIL, m->mem.cap - m->mem.len);
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
	lispsetcar(m, ref, LISP_NIL);
	lispsetcdr(m, ref, LISP_NIL);
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
idxinsert1(Mach *m, uint32_t hash, lispref_t ref)
{
	size_t i, off;

	for(i = 0; i < m->idx.cap; i++){
		lispref_t nref;
		off = (hash + i) & (m->idx.cap - 1);
		nref = m->idx.ref[off];
		if(nref == LISP_NIL){
			m->idx.ref[off] = ref;
			m->idx.len++;
			return 0;
		}
	}
	return -1;
}

static int
idxinsert(Mach *m, uint32_t hash, lispref_t ref)
{
	size_t i;

	if(3*(m->idx.len/2) >= m->idx.cap){
		lispref_t *old;
		size_t oldcap;

		old = m->idx.ref;
		oldcap = m->idx.cap;

		m->idx.cap = m->idx.cap < 16 ? 16 : 2*m->idx.cap;
		m->idx.ref = malloc(m->idx.cap * sizeof m->idx.ref[0]);
		lispmemset(m->idx.ref, LISP_NIL, m->idx.cap);
		m->idx.len = 0;
		if(old != NULL){
			for(i = 0; i < oldcap; i++){
				lispref_t oldref = old[i];
				if(oldref != LISP_NIL){
					uint32_t oldhash;
					oldhash = fnv32a((char *)strpointer(m, oldref), 0);
					if(idxinsert1(m, oldhash, oldref) == -1)
						abort();
				}
			}
			free(old);
		}
	}
	if(idxinsert1(m, hash, ref) == -1)
		abort();
	return 0;
}

static lispref_t
idxlookup(Mach *m, uint32_t hash, char *str)
{
	lispref_t ref;
	size_t i, off;

	for(i = 0; i < m->idx.cap; i++){
		off = (hash + i) & (m->idx.cap - 1);
		ref = m->idx.ref[off];
		if(ref == LISP_NIL)
			break;
		if(!strcmp(str, (char *)strpointer(m, ref))){
			return ref;
		}
	}
	return LISP_NIL;
}

lispref_t
lispmknumber(Mach *m, int v)
{
	lispref_t ref = mkref(v, LISP_TAG_INTEGER);
	if((int)refval(ref) == v)
		return ref;
	fprintf(stderr, "lispmknumber: integer overflow %d\n", v);
	abort();
}

static lispref_t
lispcons(Mach *m, lispref_t a, lispref_t d)
{
	lispref_t ref;
	lispref_t *areg = lispregister(m, a);
	lispref_t *dreg = lispregister(m, d);
	ref = allocpair(m);
	lispsetcar(m, ref, *areg);
	lispsetcdr(m, ref, *dreg);
	lisprelease(m, areg);
	lisprelease(m, dreg);
	return ref;
}

lispref_t
lispmksymbol(Mach *m, char *str)
{
	lispref_t ref;
	uint32_t hash = fnv32a(str, 0);
	if((ref = idxlookup(m, hash, str)) == LISP_NIL){
		ref = allocsymbol(m, str);
		idxinsert(m, hash, ref);
	}
	return ref;
}

lispref_t
lispparse(Mach *m, int justone)
{
	lispref_t list = LISP_NIL, prev = LISP_NIL, cons, nval;
	int ltok;
	int dot = 0;

m->gclock++;
	while((ltok = lex(m)) != -1){
		tokappend(m, '\0');
		switch(ltok){
		default:
			//fprintf(stderr, "unknown token %d: '%c' '%s'\n", ltok, ltok, m->tok);
			list = lispmkbuiltin(m, LISP_BUILTIN_ERROR);
			goto done;
		case ')':
			goto done;
		case '(':
			nval = lispparse(m, 0);
			goto append;
		case '\'':
			nval = lispparse(m, 1);
			nval = lispcons(m, lispmkbuiltin(m, LISP_BUILTIN_QUOTE), lispcons(m, nval, LISP_NIL));
			goto append;
		case '.':
			dot++;
			break;
		case ',':
			//fprintf(stderr, "TODO: backquote not implemented yet\n");
			list = lispmkbuiltin(m, LISP_BUILTIN_ERROR);
			goto done;
		case LISP_TOK_INTEGER:
			nval = lispmknumber(m, strtol(m->tok, NULL, 0));
			goto append;
		case LISP_TOK_STRING:
			nval = lispcons(m, lispmkbuiltin(m, LISP_BUILTIN_QUOTE),
				lispcons(m, lispmksymbol(m, m->tok),
				LISP_NIL));
			goto append;
		case LISP_TOK_SYMBOL:
			nval = lispmksymbol(m, m->tok);
		append:
			if(justone){
				list = nval;
				goto done;
			}
			if(dot == 0){
				cons = lispcons(m, nval, LISP_NIL);
				if(prev != LISP_NIL)
					lispsetcdr(m, prev, cons);
				else
					list = cons;
				prev = cons;
			} else if(dot == 1){
				if(prev != LISP_NIL){
					lispsetcdr(m, prev, nval);
					dot++;
				} else {
					//fprintf(stderr, "malformed s-expression, bad use of '.'\n");
					list = lispmkbuiltin(m, LISP_BUILTIN_ERROR);
					goto done;
				}
			} else {
				//fprintf(stderr, "malformed s-expression, bad use of '.'\n");
				list = lispmkbuiltin(m, LISP_BUILTIN_ERROR);
				goto done;
			}
			break;
		}
	}
	if(justone && ltok == -1)
		list = lispmkbuiltin(m, LISP_BUILTIN_ERROR);
done:
m->gclock--;
	return list;
}

static int
lispwrite(Mach *m, lispport_t port, unsigned char *buf, size_t len)
{
	for(size_t i = 0; i < len; i++)
		if(m->ports[port].writebyte(buf[i], m->ports[port].context) == -1)
			return i == 0 ? -1 : (int)i;
	return len;
}

static int
lispread(Mach *m, lispport_t port)
{
	return m->ports[port].readbyte(m->ports[port].context);
}

int
lispprint1(Mach *m, lispref_t aref, lispport_t port)
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
	case LISP_TAG_SYMBOL:
		snprintf(buf, sizeof buf, "%s", strpointer(m, aref));
		break;
	}
	lispwrite(m, port, (unsigned char *)buf, strlen(buf));
	return tag;
}

static lispref_t
lisppush(Mach *m, lispref_t val)
{
	m->stack = lispcons(m, val, m->stack);
}

static lispref_t
lisppop(Mach *m)
{
	lispref_t val = lispcar(m, m->stack);
	m->stack = lispcdr(m, m->stack);
	return val;
}

static void
lispgoto(Mach *m, int inst)
{
	m->inst = lispmkbuiltin(m, inst);
}

void
lispcall(Mach *m, int ret, int inst)
{
	lisppush(m, m->envr);
	lisppush(m, lispmkbuiltin(m, ret));
	lispgoto(m, inst);
}

static void
lispreturn(Mach *m)
{
	m->inst = lisppop(m);
	m->envr = lisppop(m);
	m->expr = LISP_NIL;
}

void
lispdefine(Mach *m, lispref_t sym, lispref_t val)
{
	lispref_t *pair = lispregister(m, lispcons(m, sym, val));
	lispref_t *env = lispregister(m, lispcdr(m, m->envr));
	*env = lispcons(m, *pair, *env);
	lispsetcdr(m, m->envr, *env);
	lisprelease(m, pair);
	lisprelease(m, env);
}

int
lispstep(Mach *m)
{
again:
	if(!lispbuiltin1(m, m->inst)){
		fprintf(stderr, "lispstep: inst is not built-in, stack corruption?\n");
		abort();
	}
	switch(lispgetbuiltin(m, m->inst)){
	default:
		fprintf(stderr, "lispstep: invalid instruction %zd, bailing out.\n", refval(m->inst));
	case LISP_STATE_CONTINUE:
		lispreturn(m);
		goto again;
	case LISP_STATE_RETURN:
		return 0;
	case LISP_STATE_EVAL:
		if(isatom(m, m->expr)){
			m->value = m->expr;
			lispreturn(m);
			goto again;
		} else if(lispsymbol(m, m->expr)){
			lispref_t lst = m->envr;
			while(lst != LISP_NIL){
				lispref_t pair = lispcar(m, lst);
				if(lisppair(m, pair)){
					lispref_t key = lispcar(m, pair);
					if(key == m->expr){
						m->value = lispcdr(m, pair);
						break;
					}
				}
				lst = lispcdr(m, lst);
			}
			if(lst == LISP_NIL){
				fprintf(stderr, "undefined symbol: %s\n", (char *)strpointer(m, m->expr));
				m->value = m->expr; // undefined
			}
			lispreturn(m);
			goto again;
		} else if(lisppair(m, m->expr)){
			// form is (something), so at least we are applying something.
			// handle special forms (quote, lambda, beta, if, define) before
			// evaluating args.

			// TODO: evaluating the head element here is a bit of a hack.

			lispref_t head;
			lisppush(m, m->expr);
			m->expr = lispcar(m, m->expr);
			lispcall(m, LISP_STATE_HEAD1, LISP_STATE_EVAL);
			goto again;
	case LISP_STATE_HEAD1:
			head = m->value;
			m->value = LISP_NIL;
			m->expr = lisppop(m);

			if(lispbuiltin1(m, head)){
				lispref_t blt = lispgetbuiltin(m, head);
				if(blt == LISP_BUILTIN_IF){
					// (if cond then else)
					m->expr = lispcdr(m, m->expr);
					lisppush(m, m->expr);
					// evaluate condition recursively
					m->expr = lispcar(m, m->expr);
					lispcall(m, LISP_STATE_IF1, LISP_STATE_EVAL);
					goto again;
	case LISP_STATE_IF1:
					// evaluate result as a tail-call, if condition
					// evaluated to #f, skip over 'then' to 'else'.
					m->expr = lisppop(m);
					m->expr = lispcdr(m, m->expr);
					if(m->value == lispmkbuiltin(m, LISP_BUILTIN_FALSE))
						m->expr = lispcdr(m, m->expr);
					m->expr = lispcar(m, m->expr);
					lispgoto(m, LISP_STATE_EVAL);
					goto again;
				}
				if(blt == LISP_BUILTIN_BETA || blt == LISP_BUILTIN_CONTINUE){
					// beta literal: return self.
					m->value = m->expr;
					lispreturn(m);
					goto again;
				}
				if(blt == LISP_BUILTIN_DEFINE){
					// (define sym val) -> args,
					// current environment gets sym associated with val.
					lispref_t *sym = lispregister(m, lispcdr(m, m->expr));
					lispref_t *val = lispregister(m, lispcdr(m, *sym));
					*sym = lispcar(m, *sym);
					if(lisppair(m, *sym)){
						// scheme shorthand: (define (name args...) body1 body2...).
						lispref_t *tmp = lispregister(m, lispcons(m, lispcdr(m, *sym), *val));
						*val = lispcons(m, lispmkbuiltin(m, LISP_BUILTIN_LAMBDA), *tmp);
						lisprelease(m, tmp);
						*sym = lispcar(m, *sym);
					} else {
						*val = lispcar(m, *val);
					}
					lisppush(m, *sym);
					m->expr = *val;
					lisprelease(m, sym);
					lisprelease(m, val);
					lispcall(m, LISP_STATE_DEFINE1, LISP_STATE_EVAL);
					goto again;
	case LISP_STATE_DEFINE1:
					// restore sym from stack, construct (sym . val)
					sym = lispregister(m, lisppop(m));
					*sym = lispcons(m, *sym, m->value);
					// push new (sym . val) just below current env head.
					lispref_t *env = lispregister(m, lispcdr(m, m->envr));
					*env = lispcons(m, *sym, *env);
					lispsetcdr(m, m->envr, *env);
					lisprelease(m, env);
					lisprelease(m, sym);
					lispreturn(m);
					goto again;
				}
				if(blt == LISP_BUILTIN_SET){
					// (set! sym val) -> args,
					// current environment gets sym associated with val.
					lispref_t *sym = lispregister(m, lispcdr(m, m->expr));
					lispref_t *val = lispregister(m, lispcdr(m, *sym));
					*sym = lispcar(m, *sym);
					if(lisppair(m, *sym)){
						// setter for our object system: (set! ('field obj) value).
						// since set is a special form, we must explicitly evaluate
						// the list elements (but not call apply)
						lisppush(m, *val);
						// set expr to ('field obj) and eval the list (no apply)
						m->expr = *sym;
						lisprelease(m, sym);
						lisprelease(m, val);
						lispcall(m, LISP_STATE_SET1, LISP_STATE_LISTEVAL);
						goto again;
	case LISP_STATE_SET1:
						val = lispregister(m, lisppop(m));
						sym = lispregister(m, m->value);
					}
					*val = lispcar(m, *val);
					lisppush(m, *sym);
					m->expr = *val;
					lisprelease(m, sym);
					lisprelease(m, val);
					lispcall(m, LISP_STATE_SET2, LISP_STATE_EVAL);
					goto again;
	case LISP_STATE_SET2:
					// restore sym from stack, construct (sym . val)
					sym = lispregister(m, lisppop(m));
					lispref_t lst;
					if(lisppair(m, *sym)){
						lispref_t beta = lispcar(m, lispcdr(m, *sym));
						if(lispextref(m, beta)){
							// it's (10 buf) form, ie. buffer indexing
							// assemble a form (set! (10 buf) value) and call/ext
							m->expr = lispcons(m, m->value, LISP_NIL);
							m->expr = lispcons(m, *sym, m->expr);
							m->expr = lispcons(m, lispmkbuiltin(m, LISP_BUILTIN_SET), m->expr);
							lisprelease(m, sym);
							lispgoto(m, LISP_STATE_CONTINUE);
							return 1;
						}
						lispref_t betahead = lispcar(m, beta);
						if(lispbuiltin(m, betahead, LISP_BUILTIN_BETA)){
							// it's ('field obj) form, ie. object access.
							// set environment scan to start from beta's captured environment.
							lst = lispcdr(m, lispcdr(m, beta));
							*sym = lispcar(m, *sym);
						} else {
							// else it's unsupported form
							fprintf(stderr, "set!: unsupported form in first argument\n");
							m->value = lispmkbuiltin(m, LISP_BUILTIN_ERROR);
							lisprelease(m, sym);
							lispreturn(m);
							goto again;
						}
					} else {
						lst = m->envr;
					}
					lispref_t pair;
					while(lst != LISP_NIL){
						pair = lispcar(m, lst);
						if(lisppair(m, pair)){
							lispref_t key = lispcar(m, pair);
							if(key == *sym)
								break;
						}
						lst = lispcdr(m, lst);
					}
					if(lst != LISP_NIL)
						lispsetcdr(m, pair, m->value);
					else
						fprintf(stderr, "set!: undefined symbol %s\n", (char *)strpointer(m, *sym));
					lisprelease(m, sym);
					m->value = LISP_NIL;
					lispreturn(m);
					goto again;
				}
				if(blt == LISP_BUILTIN_CAPTURE){
					// (capture vars body)
					// construct a new environment with only vars in it, then
					// continue to eval body.
					lispref_t *newenvr = lispregister(m, LISP_NIL);
					lispref_t *vars = lispregister(m, lispcdr(m, m->expr));
					lispref_t *body = lispregister(m, lispcdr(m, *vars));
					*vars = lispcar(m, *vars);
					*body = lispcar(m, *body);
					for(; *vars != LISP_NIL; *vars = lispcdr(m, *vars)){
						lispref_t var = lispcar(m, *vars);
						lispref_t env = m->envr;
						while(env != LISP_NIL){
							lispref_t pair = lispcar(m, env);
							if(lisppair(m, pair)){
								lispref_t key = lispcar(m, pair);
								if(key == var){
									*newenvr = lispcons(m, pair, *newenvr);
									break;
								}
							}
							env = lispcdr(m, env);
						}
					}
					m->envr = *newenvr;
					m->expr = *body;
					lisprelease(m, vars);
					lisprelease(m, newenvr);
					lisprelease(m, body);
					lispgoto(m, LISP_STATE_EVAL);
					goto again;
				}
				if(blt == LISP_BUILTIN_LAMBDA){
					// (lambda args body) -> (beta (lambda args body) envr)
					lispref_t *tmp = lispregister(m, lispcons(m, m->expr, m->envr));
					m->value = lispcons(m, lispmkbuiltin(m, LISP_BUILTIN_BETA), *tmp);
					lisprelease(m, tmp);
					lispreturn(m);
					goto again;
				}
				if(blt == LISP_BUILTIN_QUOTE){
					// (quote args) -> args
					m->value = lispcar(m, lispcdr(m, m->expr)); // expr = cdar expr
					lispreturn(m);
					goto again;
				}
			}

			// at this point we know it is a list, and that it is
			// not a special form. evaluate args, then apply.
			lispcall(m, LISP_STATE_APPLY, LISP_STATE_LISTEVAL);
			goto again;
	case LISP_STATE_APPLY:
			m->expr = m->value;
			head = lispcar(m, m->expr);
			if(lispbuiltin1(m, head)){
				lispref_t blt = lispgetbuiltin(m, head);
				if(blt >= LISP_BUILTIN_ADD && blt <= LISP_BUILTIN_REM){
					lispref_t ref0, ref;
					int ires;
					int nterms;
					m->expr = lispcdr(m, m->expr);
					ref0 = lispcar(m, m->expr);
					if(lispnumber(m, ref0)){
						ires = lispgetint(m, ref0);
					} else {
						m->value = lispmkbuiltin(m, LISP_BUILTIN_ERROR);
						lispreturn(m);
						goto again;
					}
					m->expr = lispcdr(m, m->expr);
					nterms = 0;
					while(m->expr != LISP_NIL){
						nterms++;
						ref = lispcar(m, m->expr);
						if(lispnumber(m, ref)){
							long long tmp = lispgetint(m, ref);
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
							case LISP_BUILTIN_BITIOR:
								ires |= tmp;
								break;
							case LISP_BUILTIN_BITAND:
								ires &= tmp;
								break;
							case LISP_BUILTIN_BITXOR:
								ires ^= tmp;
								break;
							case LISP_BUILTIN_BITNOT:
								ires = ~tmp;
								break;
							case LISP_BUILTIN_REM:
								ires %= tmp;
								break;
							}
						} else {
							m->value = lispmkbuiltin(m, LISP_BUILTIN_ERROR);
							lispreturn(m);
							goto again;
						}
						m->expr = lispcdr(m, m->expr);
					}
					// special case for unary sub: make it a negate.
					if(blt == LISP_BUILTIN_SUB && nterms == 0)
						ires = -ires;
					m->value = lispmknumber(m, ires);
					lispreturn(m);
					goto again;
				} else if(blt == LISP_BUILTIN_ISPAIR){ // (pair? ...)
					m->expr = lispcdr(m, m->expr);
					m->expr = lispcar(m, m->expr);
					if(lisppair(m, m->expr))
						m->value =  lispmkbuiltin(m, LISP_BUILTIN_TRUE);
					else
						m->value =  lispmkbuiltin(m, LISP_BUILTIN_FALSE);
					lispreturn(m);
					goto again;
				} else if(blt == LISP_BUILTIN_ISEQ){ // (eq? ...)
					m->expr = lispcdr(m, m->expr);
					lispref_t ref0 = lispcar(m, m->expr);
					m->expr = lispcdr(m, m->expr);
					while(m->expr != LISP_NIL){
						lispref_t ref = lispcar(m, m->expr);
						if(reftag(ref0) != reftag(ref)){
							m->value = lispmkbuiltin(m, LISP_BUILTIN_FALSE);
							goto eqdone;
						} else if(lispnumber(m, ref0) && lispnumber(m, ref)){
							if(lispgetint(m, ref0) != lispgetint(m, ref)){
								m->value = lispmkbuiltin(m, LISP_BUILTIN_FALSE);
								goto eqdone;
							}
						} else if(lispispair(m, ref0) && lispispair(m, ref)){
							if(ref0 != ref){
								m->value = lispmkbuiltin(m, LISP_BUILTIN_FALSE);
								goto eqdone;
							}
						} else {
							fprintf(stderr, "eq?: unsupported types %d %d\n", reftag(ref0), reftag(ref));
							m->value = lispmkbuiltin(m, LISP_BUILTIN_ERROR);
							lispreturn(m);
							goto again;
						}
						m->expr = lispcdr(m, m->expr);
					}
					m->value = lispmkbuiltin(m, LISP_BUILTIN_TRUE);
				eqdone:
					lispreturn(m);
					goto again;
				} else if(blt == LISP_BUILTIN_ISLESS){
					m->expr = lispcdr(m, m->expr);
					lispref_t ref0 = lispcar(m, m->expr);
					m->expr = lispcdr(m, m->expr);
					lispref_t ref1 = lispcar(m, m->expr);
					m->value = lispmkbuiltin(m, LISP_BUILTIN_FALSE); // default to false.
					if(lispnumber(m, ref0) && lispnumber(m, ref1)){
						if(lispgetint(m, ref0) < lispgetint(m, ref1))
							m->value = lispmkbuiltin(m, LISP_BUILTIN_TRUE);
					} else if(lispsymbol(m, ref0) && lispsymbol(m, ref1)){
						if(strcmp(strpointer(m, ref0), strpointer(m, ref1)) < 0)
							m->value = lispmkbuiltin(m, LISP_BUILTIN_TRUE);
					} else {
						fprintf(stderr, "less?: unsupported types\n");
						m->value = lispmkbuiltin(m, LISP_BUILTIN_ERROR);
						lispreturn(m);
						goto again;
					}
					lispreturn(m);
					goto again;
				} else if(blt == LISP_BUILTIN_ISERROR){ // (error? ...)
					m->expr = lispcdr(m, m->expr);
					m->expr = lispcar(m, m->expr);
					if(lisperror(m, m->expr))
						m->value =  lispmkbuiltin(m, LISP_BUILTIN_TRUE);
					else
						m->value =  lispmkbuiltin(m, LISP_BUILTIN_FALSE);
					lispreturn(m);
					goto again;
				} else if(blt == LISP_BUILTIN_SETCAR || blt == LISP_BUILTIN_SETCDR){
					lispref_t *cons = lispregister(m, lispcdr(m, m->expr));
					lispref_t *val = lispregister(m, lispcdr(m, *cons));
					*cons = lispcar(m, *cons); // cons
					*val = lispcar(m, *val); // val
					if(blt == LISP_BUILTIN_SETCAR)
						lispsetcar(m, *cons, *val);
					else
						lispsetcdr(m, *cons, *val);
					lisprelease(m, cons);
					lisprelease(m, val);
					lispreturn(m);
					goto again;
				} else if(blt == LISP_BUILTIN_CAR){
					m->reg2 = lispcdr(m, m->expr);
					m->reg2 = lispcar(m, m->reg2);
					m->value = lispcar(m, m->reg2);
					m->reg2 = LISP_NIL;
					lispreturn(m);
					goto again;
				} else if(blt == LISP_BUILTIN_CDR){
					m->reg2 = lispcdr(m, m->expr);
					m->reg2 = lispcar(m, m->reg2);
					m->value = lispcdr(m, m->reg2);
					m->reg2 = LISP_NIL;
					lispreturn(m);
					goto again;
				} else if(blt == LISP_BUILTIN_CONS){
					m->reg2 = lispcdr(m, m->expr);
					m->reg3 = lispcdr(m, m->reg2);
					m->reg2 = lispcar(m, m->reg2);
					m->reg3 = lispcar(m, m->reg3);
					m->value = lispcons(m, m->reg2, m->reg3);
					m->reg2 = LISP_NIL;
					m->reg3 = LISP_NIL;
					lispreturn(m);
					goto again;
				} else if(blt == LISP_BUILTIN_CALLCC){
					m->expr = lispcdr(m, m->expr);
					m->reg2 = lispcons(m, lispmkbuiltin(m, LISP_BUILTIN_CONTINUE), m->stack);
					m->reg3 = lispcons(m, m->reg2, LISP_NIL);
					m->expr = lispcons(m, lispcar(m, m->expr), m->reg3);
					m->reg2 = LISP_NIL;
					m->reg3 = LISP_NIL;
					lispgoto(m, LISP_STATE_EVAL);
					goto again;
				} else if(blt == LISP_BUILTIN_PRINT1){
					m->expr = lispcdr(m, m->expr);
					m->reg2 = lispcar(m, m->expr);
					m->expr = lispcdr(m, m->expr);
					m->expr = lispcar(m, m->expr);
					lispprint1(m, m->expr, lispgetint(m, m->reg2));
					m->reg2 = LISP_NIL;
					m->value = m->expr;
					lispreturn(m);
					goto again;
				} else if(blt == LISP_BUILTIN_EVAL){
					m->expr = lispcar(m, lispcdr(m, m->expr)); // expr = cdar expr
					lispgoto(m, LISP_STATE_EVAL);
					goto again;
				}
			} else if(lispextref(m, head)){
				// we are applying an external object to lisp arguments..
				lispgoto(m, LISP_STATE_CONTINUE);
				return 1;
			} else if(lispsymbol(m, head) || lispnumber(m, head)){
				// this is the accessor (getter/setter) for our "namespaces".
				// it returns an entry from a function's environment, so that
				// an object constructor can become as simple as
				//		(define (vec3 x y z) (lambda()))
				// which can then be used as
				//		(define r0 (vec3 1 2 3))
				//		(+ ('x r0) ('y r0) ('z r0))
				// note the prefix notation for field access. there is a
				// special form in set! for this notation, so that
				//		(set! ('x r0) 7)
				// does what you might expect.
				lispref_t beta = lispcar(m, lispcdr(m, m->expr));
				if(beta == LISP_NIL){
					m->value = lispmkbuiltin(m, LISP_BUILTIN_ERROR);
					lispreturn(m);
					goto again;
				}
				if(lispextref(m, beta)){
					m->expr = lispcons(m, beta, LISP_NIL);
					m->expr = lispcons(m, head, m->expr);
					lispgoto(m, LISP_STATE_CONTINUE);
					return 1;
				}
				lispref_t betahead = lispcar(m, beta);
				if(lispbuiltin(m, betahead, LISP_BUILTIN_BETA)){
					lispref_t lst = lispcdr(m, lispcdr(m, beta));
					while(lst != LISP_NIL){
						lispref_t pair = lispcar(m, lst);
						if(lisppair(m, pair)){
							lispref_t key = lispcar(m, pair);
							if(key == head){
								m->value = lispcdr(m, pair);
								lispreturn(m);
								goto again;
							}
						}
						lst = lispcdr(m, lst);
					}
					fprintf(stderr, "symbol %s not found in object\n", (char *)strpointer(m, head));
					m->value = lispmkbuiltin(m, LISP_BUILTIN_ERROR);
					lispreturn(m);
					goto again;
				} else {
					fprintf(stderr, "symbol finding in a non-object\n");
					m->value = lispmkbuiltin(m, LISP_BUILTIN_ERROR);
					lispreturn(m);
					goto again;
				}

			} else if(lisppair(m, head)){

				// form is ((beta (lambda...)) args), or a continuation.
				// ((beta (lambda args . body) . envr) args) -> (body),
				// with a new environment

				lispref_t beta, lambda;
				beta = lispcar(m, m->expr);
				if(beta == LISP_NIL){
					m->value = lispmkbuiltin(m, LISP_BUILTIN_ERROR);
					lispreturn(m);
					goto again;
				}
				head = lispcar(m, beta);
				if(!lispbuiltin1(m, head)){
					m->value = lispmkbuiltin(m, LISP_BUILTIN_ERROR);
					lispreturn(m);
					goto again;
				}
				if(lispbuiltin(m, head, LISP_BUILTIN_CONTINUE)){
					// ((continue . stack) return-value)
					m->stack = lispcdr(m, beta);
					m->value = lispcdr(m, m->expr);
					if(lisppair(m, m->value))
						m->value = lispcar(m, m->value);
					lispreturn(m);
					goto again;
				} else if(lispbuiltin(m, head, LISP_BUILTIN_BETA)){

					lambda = lispcar(m, lispcdr(m, beta));
					m->envr = lispcdr(m, lispcdr(m, beta));

					lispref_t *argnames = lispregister(m, lispcdr(m, lambda));
					lispref_t *args = lispregister(m, lispcdr(m, m->expr)); // args = cdr expr
					if(lisppair(m, *argnames)){
						// loop over argnames and args simultaneously, cons
						// them as pairs to the environment
						*argnames = lispcar(m, *argnames);
						while(lisppair(m, *argnames) && lisppair(m, *args)){
							lispref_t *pair = lispregister(m, lispcons(m,
								lispcar(m, *argnames),
								lispcar(m, *args)));
							m->envr = lispcons(m, *pair, m->envr);
							*argnames = lispcdr(m, *argnames);
							*args = lispcdr(m, *args);
							lisprelease(m, pair);
						}
					}

					// scheme-style variadic: argnames list terminates in a
					// symbol instead of LISP_NIL, associate the rest of argslist
					// with it. notice: (lambda x (body)) also lands here.
					if(*argnames != LISP_NIL && !lisppair(m, *argnames)){
						lispref_t *pair = lispregister(m, lispcons(m, *argnames, *args));
						m->envr = lispcons(m, *pair, m->envr);
						lisprelease(m, pair);
					} else if(*argnames != LISP_NIL || *args != LISP_NIL){
						fprintf(stderr, "mismatch in number of function args\n");
						m->value = lispmkbuiltin(m, LISP_BUILTIN_ERROR);
						lispreturn(m);
						goto again;
					}

					// push a new 'environment head' in.
					m->envr = lispcons(m, LISP_NIL, m->envr);
					// clear the registers.
					lisprelease(m, argnames);
					lisprelease(m, args);

					// parameters are bound, pull body from lambda to m->expr.
					beta = lispcar(m, m->expr); // beta = car expr
					lambda = lispcar(m, lispcdr(m, beta));
					m->expr = lispcdr(m, lispcdr(m, lambda));
					lisppush(m, m->expr);
				} else {
					if(refval(head) != LISP_BUILTIN_BETA){
						fprintf(stderr, "applying list with non-beta head\n");
						m->value = lispmkbuiltin(m, LISP_BUILTIN_ERROR);
						lispreturn(m);
						goto again;
					}
				}

	case LISP_STATE_BETA1:
				m->reg2 = lispcar(m, m->stack);
				if(m->reg2 != LISP_NIL){
					m->expr = lispcar(m, m->reg2);
					m->reg2 = lispcdr(m, m->reg2);
					lispsetcar(m, m->stack, m->reg2);
					if(m->reg2 != LISP_NIL){
						lispcall(m, LISP_STATE_BETA1, LISP_STATE_EVAL);
						goto again;
					} else { // tail call
						lisppop(m);
						lispgoto(m, LISP_STATE_EVAL);
						goto again;
					}
				}
				lisppop(m); // pop expr.
				lispreturn(m);
				goto again;
			} else {
				fprintf(stderr, "apply: head is weird: ");
				//eval_print(m);
				fprintf(stderr, "\n");
				lispreturn(m);
				goto again;
			}
		}
		fprintf(stderr, "lispstep eval: unrecognized form: ");
		//eval_print(m);
		fprintf(stderr, "\n");
		m->value = lispmkbuiltin(m, LISP_BUILTIN_ERROR);
		lispreturn(m);
		goto again;
	case LISP_STATE_LISTEVAL:
		{
			lispref_t *state;
			// construct initial evaluation state: (expr-cell . value-cell)
			state = &m->reg2;
			m->reg3 = lispcons(m, LISP_NIL, LISP_NIL); // first "value-cell"
			lisppush(m, m->reg3);
			*state = lispcons(m, m->expr, m->reg3); // construct evaluation state
			lisppush(m, *state);
			m->reg3 = m->expr;
			goto listeval_first;
	case LISP_STATE_LISTEVAL1:
			state = &m->reg2;
			*state = lispcar(m, m->stack);
			// store new m->value to value-cell.
			lispsetcar(m, lispcdr(m, *state), m->value);
			// load expr-cell to reg3.
			m->reg3 = lispcar(m, *state); // expr-cell = car(state)
listeval_first:
			if(lisppair(m, m->reg3)){
				// load new expr from expr-cell and set expr-cell to next expr.
				m->expr = lispcar(m, m->reg3); // expr = (car expr-cell)
				m->reg3 = lispcdr(m, m->reg3); // reg3 = (cdr expr-cell)
				lispsetcar(m, *state, m->reg3); //  car(state) = (cdr expr-cell)

				// allocate new value-cell, link cdr of current value-cell to it and set value-cell to the new one.
				m->reg4 = lispcons(m, LISP_NIL, LISP_NIL);
				m->reg3 = lispcdr(m, *state); // reg3 = value-cell ; cdr(state)
				lispsetcdr(m, m->reg3, m->reg4); // cdr(cdr state) = reg4, cdr(value-cell) = reg4
				lispsetcdr(m, *state, m->reg4); // value-cell = reg4

				// clean up registers, mainly for gc.
				*state = LISP_NIL;
				m->reg3 = LISP_NIL;
				m->reg4 = LISP_NIL;
				// call "apply" on the current expr and have it return to this case.
				lispcall(m, LISP_STATE_LISTEVAL1, LISP_STATE_EVAL);
				goto again;
			}
			// this following bit is non-standard, it allows one to call a function
			// with the notation (fn . args) or (fn arg1 arg2 . rest), effectively
			// splicing a list 'args' in to the argument list, analogous to how
			// varargs are declared in standard scheme functions. by allowing the
			// dotted notation for apply, there is no need an apply built-in.
			// unfortunately, of course the dotted tail must not be a literal list,
			// so defining apply as a function is still going to be necessary.
			if(m->reg3 != LISP_NIL){
				m->expr = m->reg3;
				lispsetcar(m, *state, LISP_NIL);
				*state = LISP_NIL;
				m->reg3 = LISP_NIL;
				m->reg4 = LISP_NIL;
				lispcall(m, LISP_STATE_LISTEVAL2, LISP_STATE_EVAL);
				goto again;
	case LISP_STATE_LISTEVAL2:
				state = &m->reg2;
				*state = lispcar(m, m->stack);
				// store new m->value to the rest of value chain.
				m->reg3 = lispcdr(m, *state);
				lispsetcdr(m, m->reg3, m->value);
			}

			*state = LISP_NIL;
			m->reg3 = LISP_NIL;
			m->reg4 = LISP_NIL;
			lisppop(m);
			m->value = lispcdr(m, lisppop(m));
			lispreturn(m);
			goto again;
		}
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
static lispref_t
lispcopy(Mach *newm, Mach *oldm, lispref_t ref)
{
	if(lispispair(oldm, ref)){
		if(ref == LISP_NIL)
			return LISP_NIL;
		if(lispbuiltin(oldm, lispcar(oldm, ref), LISP_BUILTIN_FORWARD))
			return lispcdr(oldm, ref);
		// load from old, cons in new. this is the copy phase: any references in
		// the new cell will point to something in oldm.
		lispref_t newref = lispcons(newm, lispcar(oldm, ref), lispcdr(oldm, ref));
		// rewrite oldm to point to new.
		lispsetcar(oldm, ref, lispmkbuiltin(oldm, LISP_BUILTIN_FORWARD));
		lispsetcdr(oldm, ref, newref);
		return newref;
	}
	return ref;
}

/*
 *	This is the main garbage collector routine. The idea is to create the root
 *	set from vm registers using lispcopy to an otherwise empty machine.
 *	After the root set is formed, it proceeds in a breath-first manner, calling
 *	lispcopy on all of its memory locations until everything has been collapsed.
 */
void
lispcollect(Mach *m)
{
	if(m->mem.len == 0)
		return;

	size_t oldlen = m->mem.len;
	// There are two "memories" within m, namely m->mem and m->copy.
	// we create a copy of m called oldm here, and then exchange m->mem and
	// m->copy. It's a waste of memory, but it avoids one big malloc per gc.
	Mach oldm;
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
		lispmemset(m->mem.ref, LISP_NIL, m->mem.cap);
	}
	m->mem.len = 0;
	memcpy(&m->copy, &oldm.mem, sizeof m->copy);
	m->gclock++;

	// copy registers as roots.
	lispref_t reg0tmp = lispcopy(m, &oldm, oldm.reg0);
	lispref_t reg1tmp = lispcopy(m, &oldm, oldm.reg1);
	m->reg2 = lispcopy(m, &oldm, oldm.reg2);
	m->reg3 = lispcopy(m, &oldm, oldm.reg3);
	m->reg4 = lispcopy(m, &oldm, oldm.reg4);

	// copy the "dynamic" registers.
	uint32_t reguse = oldm.reguse;
	for(size_t i = 0; i < nelem(m->regs); i++)
		if((reguse & (1<<i)) != 0)
			m->regs[i] = lispcopy(m, &oldm, oldm.regs[i]);
	m->reguse = reguse;

	m->value = lispcopy(m, &oldm, oldm.value);
	m->expr = lispcopy(m, &oldm, oldm.expr);
	m->envr = lispcopy(m, &oldm, oldm.envr);
	m->stack = lispcopy(m, &oldm, oldm.stack);

	for(size_t i = 2; i < m->mem.len; i++)
		m->mem.ref[i] = lispcopy(m, &oldm, m->mem.ref[i]);

if(0)fprintf(stderr, "collected: from %zu to %zu\n", oldlen, m->mem.len);
	// re-copy the registers used by cons, lispcopy calls cons itself so
	// they get overwritten over and over again during gc.
	m->reg0 = reg0tmp;
	m->reg1 = reg1tmp;

	m->gclock--;
}

void
lispinit(Mach *m)
{
	// install initial environment (define built-ins)
	m->envr = lispcons(m, LISP_NIL, LISP_NIL);
	for(size_t i = 0; i < LISP_NUM_BUILTINS; i++){
		lispref_t sym = lispmksymbol(m, bltnames[i]);
		lispdefine(m, sym, lispmkbuiltin(m, i));
	}
}

int
lispsetport(Mach *m, lispport_t port, int (*writebyte)(int ch, void *ctx), int (*readbyte)(void *ctx), int (*unreadbyte)(int ch, void *ctx), void *ctx)
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

lispref_t
lispextalloc(Mach *m)
{
	if(m->extrefs.len == m->extrefs.cap){
		m->extrefs.cap = nextpow2(m->extrefs.cap);
		void *p = realloc(m->extrefs.p, m->extrefs.cap * sizeof m->extrefs.p[0]);
		if(p == NULL){
			fprintf(stderr, "lispextalloc: realloc failed\n");
			abort();
		}
		m->extrefs.p = p;
	}
	lispref_t ref = mkref(m->extrefs.len, LISP_TAG_EXTREF);
	assert(urefval(ref) == m->extrefs.len);
	m->extrefs.len++;
	return ref;
}

int
lispextset(Mach *m, lispref_t ref, void *obj, void *type)
{
	if(lispextref(m, ref)){
		size_t i = refval(ref);
		m->extrefs.p[i].obj = obj;
		m->extrefs.p[i].type = type;
		return 0;
	}
	abort();
	return -1;
}

int
lispextget(Mach *m, lispref_t ref, void **obj, void **type)
{
	if(lispextref(m, ref)){
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
