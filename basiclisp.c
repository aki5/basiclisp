
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <math.h>
#include "basiclisp.h"


#define nelem(x) (sizeof(x)/sizeof(x[0]))

static char *bltnames[] = {
[LISP_BUILTIN_IF] = "if",
[LISP_BUILTIN_BETA] = "beta",
[LISP_BUILTIN_CONTINUE] = "continue",
[LISP_BUILTIN_DEFINE] = "define",
[LISP_BUILTIN_LAMBDA] = "lambda",
[LISP_BUILTIN_QUOTE] = "quote",
[LISP_BUILTIN_CALLEXT] = "call-external",
[LISP_BUILTIN_CALLCC] = "call-with-current-continuation",
[LISP_BUILTIN_SET] = "set!",
[LISP_BUILTIN_SETCAR] = "set-car!",
[LISP_BUILTIN_SETCDR] = "set-cdr!",
[LISP_BUILTIN_CONS] = "cons",
[LISP_BUILTIN_CAR] = "car",
[LISP_BUILTIN_CDR] = "cdr",
[LISP_BUILTIN_CLEANENV] = "clean-environment",
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

static lispref_t
mkref(int val, int tag)
{
	return (((val)<<3)|((tag)&7));
}

static int
reftag(lispref_t ref)
{
	return ref & 7;
}

static intptr_t
refval(lispref_t ref)
{
	return (intptr_t)ref >> 3;
}

static size_t
urefval(lispref_t ref)
{
	return ref >> 3;
}

static lispref_t *
pointer(Mach *m, lispref_t ref)
{
	size_t off = urefval(ref);
	if(off <= 0 || off >= m->memlen){
		fprintf(stderr, "dereferencing an out of bounds reference: %x\n", ref);
		abort();
	}
	return &m->mem[urefval(ref)];
}

lispref_t
lispload(Mach *m, lispref_t base, size_t offset)
{
	lispref_t *p = pointer(m, base);
	return p[offset];
}

static lispref_t
lispstore(Mach *m, lispref_t base, size_t offset, lispref_t obj)
{
	lispref_t *p = pointer(m, base);
	p[offset] = obj;
	return obj;
}

static int
isatom(Mach *m, lispref_t a)
{
	int tag = reftag(a);
	return tag != LISP_TAG_SYMBOL && tag != LISP_TAG_PAIR;
}

static int
issymbol(Mach *m, lispref_t a)
{
	return reftag(a) == LISP_TAG_SYMBOL;
}

static int
ispair(Mach *m, lispref_t a)
{
	return reftag(a) == LISP_TAG_PAIR && a != LISP_NIL;
}

int
lisperror(Mach *m, lispref_t a)
{
	return reftag(a) == LISP_TAG_ERROR;
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
	case '.': case '-': case '+':
	case 'p': case 'e': case 'x':
		return 1;
	}
	return 0;
}

static void
tokappend(Mach *m, int ch)
{
	if(m->toklen == m->tokcap){
		m->tokcap = (m->tokcap == 0) ? 256 : 2*m->tokcap;
		m->tok = realloc(m->tok, m->tokcap);
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
	isinteger = 1;
	ishex = 0;
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
	case '(': case ')':
	case '\'': case ',':
	caseself:
		return ch;

	// it may be a dot, a number or a symbol.
	// look at the next character to decide.
	case '.':
		isinteger = 0;
	case '-': case '+':
		if((peekc = m->ports[0].readbyte(m->ports[0].context)) == -1)
			goto caseself;
		m->ports[0].unreadbyte(peekc, m->ports[0].context);
		// dot can appear solo, + and - are married
		// to a number or become symbols.
		if(ch == '.' && isbreak(peekc))
			goto caseself;
		if(peekc < '0' || peekc > '9')
			goto casesym;
	case '0': case '1': case '2': case '3': case '4':
	case '5': case '6': case '7': case '8': case '9':
		while(ch != -1){
			if(!isnumchar(ch) && !(ishex && ishexchar(ch))){
				if(isbreak(ch))
					break;
				goto casesym;
			}
			if(ch == 'x')
				ishex = 1;
			if(ch == '.' || ch == 'e' || ch == 'p')
				isinteger = 0;
			tokappend(m, ch);
			ch = m->ports[0].readbyte(m->ports[0].context);
		}
		if(ch != -1)
			m->ports[0].unreadbyte(ch, m->ports[0].context);
		return isinteger ? LISP_TAG_INTEGER : LISP_TAG_FLOAT;

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
		return LISP_TAG_STRING;

	// symbol is any string of nonbreak characters not starting with a number
	default:
	casesym:
		while(ch != -1 && !isbreak(ch)){
			tokappend(m, ch);
			ch = m->ports[0].readbyte(m->ports[0].context);
		}
		m->ports[0].unreadbyte(ch, m->ports[0].context);
		return LISP_TAG_SYMBOL;
	}
	return -1;
}

static lispref_t
allocate(Mach *m, size_t num, int tag)
{
	lispref_t ref;
	int didgc = 0;
	// first, try gc.
	if(!m->gclock && (m->memcap - m->memlen) < num){
		lispgc(m);
		didgc = 1;
	}
recheck:
	if((didgc && 4*m->memlen >= 3*m->memcap) || (m->memcap - m->memlen) < num){
		m->memcap = (m->memcap == 0) ? 256 : 2*m->memcap;
		m->mem = realloc(m->mem, m->memcap * sizeof m->mem[0]);
		memset(m->mem + m->memlen, 0, m->memcap - m->memlen);
		goto recheck;
	}
	// never return LISP_NIL by accident.
	if(m->memlen == 0){
		m->memlen += 2;
		goto recheck;
	}
	ref = mkref(m->memlen, tag);
	if(urefval(ref) != m->memlen || reftag(ref) != tag){
		fprintf(stderr, "out of address bits in ref: want %zx.%x got %zx.%x\n",
			m->memlen, tag, urefval(ref), reftag(ref));
		exit(1);
	}
	m->memlen += num;
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

	for(i = 0; i < m->idxcap; i++){
		lispref_t nref;
		off = (hash + i) & (m->idxcap - 1);
		nref = m->idx[off];
		if(nref == LISP_NIL){
			m->idx[off] = ref;
			m->idxlen++;
			return 0;
		}
	}
	return -1;
}

static int
idxinsert(Mach *m, uint32_t hash, lispref_t ref)
{
	size_t i;

	if(3*(m->idxlen/2) >= m->idxcap){
		lispref_t *old;
		size_t oldcap;

		old = m->idx;
		oldcap = m->idxcap;

		m->idxcap = m->idxcap < 16 ? 16 : 2*m->idxcap;
		m->idx = malloc(m->idxcap * sizeof m->idx[0]);
		memset(m->idx, 0, m->idxcap * sizeof m->idx[0]);
		m->idxlen = 0;
		if(old != NULL){
			for(i = 0; i < oldcap; i++){
				lispref_t oldref = old[i];
				if(oldref != LISP_NIL){
					uint32_t oldhash;
					oldhash = fnv32a((char *)pointer(m, oldref), 0);
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

	for(i = 0; i < m->idxcap; i++){
		off = (hash + i) & (m->idxcap - 1);
		ref = m->idx[off];
		if(ref == LISP_NIL)
			break;
		if(!strcmp(str, (char *)pointer(m, ref))){
			return ref;
		}
	}
	return LISP_NIL;
}

static lispref_t
mkany(Mach *m, void *data, size_t len, int type)
{
	size_t num;
	lispref_t ref;

	num = (len + sizeof(ref) - 1) / sizeof(ref);
	ref = allocate(m, num, type);
	memcpy(pointer(m, ref), data, len);
	return ref;
}

static lispref_t
mkint(Mach *m, long long v)
{
	lispref_t ref;

	ref = mkref(v, LISP_TAG_INTEGER);
	if((long long)refval(ref) == v)
		return ref;

	return mkany(m, &v, sizeof v, LISP_TAG_BIGINT);
}

static lispref_t
mkfloat(Mach *m, double v)
{
	return mkany(m, &v, sizeof v, LISP_TAG_FLOAT);
}

static lispref_t
mkstring(Mach *m, char *str, int type)
{
	uint32_t hash;
	lispref_t ref;
	hash = fnv32a(str, 0);
	if((ref = idxlookup(m, hash, str)) == LISP_NIL){
		ref = mkany(m, str, strlen(str)+1, type);
		idxinsert(m, hash, ref);
	}
	return ref;
}

static lispref_t
lispcons(Mach *m, lispref_t a, lispref_t d)
{
	lispref_t ref;
	lispref_t *areg = lispregister(m, a);
	lispref_t *dreg = lispregister(m, d);
	ref = allocate(m, 2, LISP_TAG_PAIR);
	lispstore(m, ref, 0, *areg);
	lispstore(m, ref, 1, *dreg);
	lisprelease(m, areg);
	lisprelease(m, dreg);
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
			list = LISP_TAG_ERROR;
			goto done;
		case ')':
			goto done;
		case '(':
			nval = lispparse(m, 0);
			goto append;
		case '\'':
			nval = lispparse(m, 1);
			nval = lispcons(m, mkref(LISP_BUILTIN_QUOTE, LISP_TAG_BUILTIN), lispcons(m, nval, LISP_NIL));
			goto append;
		case '.':
			dot++;
			break;
		case ',':
			//fprintf(stderr, "TODO: backquote not implemented yet\n");
			list = LISP_TAG_ERROR;
			goto done;
		case LISP_TAG_INTEGER:
			nval = mkint(m, strtoll(m->tok, NULL, 0));
			goto append;
		case LISP_TAG_FLOAT:
			nval = mkfloat(m, strtod(m->tok, NULL));
			goto append;
		case LISP_TAG_STRING:
			nval = mkstring(m, m->tok, LISP_TAG_STRING);
			goto append;
		case LISP_TAG_SYMBOL:
			nval = mkstring(m, m->tok, LISP_TAG_SYMBOL);
		append:
			if(justone){
				list = nval;
				goto done;
			}
			if(dot == 0){
				cons = lispcons(m, nval, LISP_NIL);
				if(prev != LISP_NIL)
					lispstore(m, prev, 1, cons);
				else
					list = cons;
				prev = cons;
			} else if(dot == 1){
				if(prev != LISP_NIL){
					lispstore(m, prev, 1, nval);
					dot++;
				} else {
					//fprintf(stderr, "malformed s-expression, bad use of '.'\n");
					list = LISP_TAG_ERROR;
					goto done;
				}
			} else {
				//fprintf(stderr, "malformed s-expression, bad use of '.'\n");
				list = LISP_TAG_ERROR;
				goto done;
			}
			break;
		}
	}
	if(justone && ltok == -1)
		list = LISP_TAG_ERROR;
done:
m->gclock--;
	return list;
}

static long long
loadint(Mach *m, lispref_t ref)
{
	int tag = reftag(ref);
	if(tag == LISP_TAG_INTEGER)
		return (long long)refval(ref);
	if(tag == LISP_TAG_BIGINT)
		return *(long long *)pointer(m, ref);
	fprintf(stderr, "loadint: non-integer reference\n");
	return 0;
}

static double
loadfloat(Mach *m, lispref_t ref)
{
	if(reftag(ref) == LISP_TAG_FLOAT)
		return *(double *)pointer(m, ref);
	fprintf(stderr, "loadfloat: non-float reference\n");
	return 0;
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

static int
atomprint(Mach *m, lispref_t aref, lispport_t port)
{
	char buf[128];
	char *ptr = NULL;
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
	case LISP_TAG_ERROR:
		snprintf(buf, sizeof buf, "error %zx\n", refval(aref));
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
	case LISP_TAG_BIGINT:
		snprintf(buf, sizeof buf, "%lld", *(long long *)pointer(m, aref));
		break;
	case LISP_TAG_FLOAT:
		snprintf(buf, sizeof buf, "%g", *(double *)pointer(m, aref));
		break;
	case LISP_TAG_STRING:
		snprintf(buf, sizeof buf, "%s", (char *)pointer(m, aref));
		break;
	case LISP_TAG_SYMBOL:
		snprintf(buf, sizeof buf, "%s", (char *)pointer(m, aref));
		break;
	}
	lispwrite(m, port, buf, strlen(buf));
	return tag;
}

static void
lispgoto(Mach *m, lispref_t inst)
{
	m->inst = mkref(inst, LISP_TAG_BUILTIN);
}

void
lispcall(Mach *m, lispref_t ret, lispref_t inst)
{
	m->stack = lispcons(m, m->envr, m->stack);
	m->stack = lispcons(m, mkref(ret, LISP_TAG_BUILTIN), m->stack);
	lispgoto(m, inst);
}

static void
lispreturn(Mach *m)
{
	m->inst = lispload(m, m->stack, 0);
	m->stack = lispload(m, m->stack, 1);
	m->envr = lispload(m, m->stack, 0);
	m->stack = lispload(m, m->stack, 1);
	m->expr = LISP_NIL;
}

static void
lispdefine(Mach *m, lispref_t sym, lispref_t val)
{
	lispref_t *pair = lispregister(m, lispcons(m, sym, val));
	lispref_t *env = lispregister(m, lispload(m, m->envr, 1));
	*env = lispcons(m, *pair, *env);
	lispstore(m, m->envr, 1, *env);
	lisprelease(m, pair);
	lisprelease(m, env);
}

int
lispstep(Mach *m)
{
again:
	if(reftag(m->inst) != LISP_TAG_BUILTIN){
		fprintf(stderr, "lispstep: inst is not built-in, stack corruption?\n");
		abort();
	}
	switch(refval(m->inst)){
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
		} else if(issymbol(m, m->expr)){
			lispref_t lst = m->envr;
			while(lst != LISP_NIL){
				lispref_t pair = lispload(m, lst, 0);
				if(ispair(m, pair)){
					lispref_t key = lispload(m, pair, 0);
					if(key == m->expr){
						m->value = lispload(m, pair, 1);
						break;
					}
				}
				lst = lispload(m, lst, 1);
			}
			if(lst == LISP_NIL){
				fprintf(stderr, "undefined symbol: %s\n", (char *)pointer(m, m->expr));
				m->value = m->expr; // undefined
			}
			lispreturn(m);
			goto again;
		} else if(ispair(m, m->expr)){
			// form is (something), so at least we are applying something.
			// handle special forms (quote, lambda, beta, if, define) before
			// evaluating args.

			// TODO: evaluating the head element here is a bit of a hack.

			lispref_t head;
			m->stack = lispcons(m, m->expr, m->stack);
			m->expr = lispload(m, m->expr, 0);
			lispcall(m, LISP_STATE_HEAD1, LISP_STATE_EVAL);
			goto again;
	case LISP_STATE_HEAD1:
			head = m->value;
			m->value = LISP_NIL;
			m->expr = lispload(m, m->stack, 0);
			m->stack = lispload(m, m->stack, 1);

			if(reftag(head) == LISP_TAG_BUILTIN){
				lispref_t blt = refval(head);
				if(blt == LISP_BUILTIN_IF){
					// (if cond then else)
					m->expr = lispload(m, m->expr, 1);
					m->stack = lispcons(m, m->expr, m->stack);
					// evaluate condition recursively
					m->expr = lispload(m, m->expr, 0);
					lispcall(m, LISP_STATE_IF1, LISP_STATE_EVAL);
					goto again;
	case LISP_STATE_IF1:
					// evaluate result as a tail-call, if condition
					// evaluated to #f, skip over 'then' to 'else'.
					m->expr = lispload(m, m->stack, 0);
					m->stack = lispload(m, m->stack, 1);
					m->expr = lispload(m, m->expr, 1);
					if(m->value == mkref(LISP_BUILTIN_FALSE, LISP_TAG_BUILTIN))
						m->expr = lispload(m, m->expr, 1);
					m->expr = lispload(m, m->expr, 0);
					lispgoto(m, LISP_STATE_EVAL);
					goto again;
				}
				if(blt == LISP_BUILTIN_BETA || blt == LISP_BUILTIN_CONTINUE){
					// beta literal: return self.
					m->value = m->expr;
					lispreturn(m);
					goto again;
				}
				if(blt == LISP_BUILTIN_DEFINE || blt == LISP_BUILTIN_SET){
					// (define sym val) -> args,
					// current environment gets sym associated with val.
					lispref_t *sym = lispregister(m, lispload(m, m->expr, 1));
					lispref_t *val = lispregister(m, lispload(m, *sym, 1));
					*sym = lispload(m, *sym, 0);
					if(ispair(m, *sym)){
						// scheme shorthand: (define (name args...) body1 body2...).
						lispref_t *tmp = lispregister(m, lispcons(m, lispload(m, *sym, 1), *val));
						*val = lispcons(m, mkref(LISP_BUILTIN_LAMBDA, LISP_TAG_BUILTIN), *tmp);
						lisprelease(m, tmp);
						*sym = lispload(m, *sym, 0);
					} else {
						*val = lispload(m, *val, 0);
					}
					m->stack = lispcons(m, *sym, m->stack);
					m->expr = *val;
					lisprelease(m, sym);
					lisprelease(m, val);
					lispcall(m, blt == LISP_BUILTIN_SET ? LISP_STATE_SET1 : LISP_STATE_DEFINE1, LISP_STATE_EVAL);
					goto again;
	case LISP_STATE_DEFINE1:
					// restore sym from stak, construct (sym . val)
					sym = lispregister(m, lispload(m, m->stack, 0));
					m->stack = lispload(m, m->stack, 1);
					*sym = lispcons(m, *sym, m->value);
					// push new (sym . val) just below current env head.
					lispref_t *env = lispregister(m, lispload(m, m->envr, 1));
					*env = lispcons(m, *sym, *env);
					lispstore(m, m->envr, 1, *env);
					lisprelease(m, env);
					lisprelease(m, sym);
					lispreturn(m);
					goto again;
	case LISP_STATE_SET1:
					// restore sym from stak, construct (sym . val)
					sym = lispregister(m, lispload(m, m->stack, 0));
					m->stack = lispload(m, m->stack, 1);
					lispref_t lst = m->envr;
					lispref_t pair;
					while(lst != LISP_NIL){
						pair = lispload(m, lst, 0);
						if(ispair(m, pair)){
							lispref_t key = lispload(m, pair, 0);
							if(key == *sym)
								break;
						}
						lst = lispload(m, lst, 1);
					}
					if(lst != LISP_NIL)
						lispstore(m, pair, 1, m->value);
					else
						fprintf(stderr, "set!: undefined symbol %s\n", (char *)pointer(m, *sym));
					lisprelease(m, sym);
					m->value = LISP_NIL;
					lispreturn(m);
					goto again;
				}
				if(blt == LISP_BUILTIN_LAMBDA){
					// (lambda args body) -> (beta (lambda args body) envr)
					lispref_t *tmp = lispregister(m, lispcons(m, m->expr, m->envr));
					m->value = lispcons(m, mkref(LISP_BUILTIN_BETA, LISP_TAG_BUILTIN), *tmp);
					lisprelease(m, tmp);
					lispreturn(m);
					goto again;
				}
				if(blt == LISP_BUILTIN_QUOTE){
					// (quote args) -> args
					m->value = lispload(m, lispload(m, m->expr, 1), 0); // expr = cdar expr
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
			head = lispload(m, m->expr, 0);
			if(reftag(head) == LISP_TAG_BUILTIN){
				lispref_t blt = refval(head);
				if(blt >= LISP_BUILTIN_ADD && blt <= LISP_BUILTIN_REM){
					lispref_t ref0, ref, tag0, tag;
					long long ires;
					double fres;
					int nterms;
					m->expr = lispload(m, m->expr, 1);
					ref0 = lispload(m, m->expr, 0);
					tag0 = reftag(ref0);
					if(tag0 == LISP_TAG_INTEGER || tag0 == LISP_TAG_BIGINT){
						ires = loadint(m, ref0);
						tag0 = LISP_TAG_INTEGER;
					} else if(tag0 == LISP_TAG_FLOAT){
						fres = loadfloat(m, ref0);
					} else {
						m->value = LISP_TAG_ERROR;
						lispreturn(m);
						goto again;
					}
					m->expr = lispload(m, m->expr, 1);
					nterms = 0;
					while(m->expr != LISP_NIL){
						nterms++;
						ref = lispload(m, m->expr, 0);
						tag = reftag(ref);
						if(tag == LISP_TAG_BIGINT)
							tag = LISP_TAG_INTEGER;
						if(tag0 != tag){
							m->value = LISP_TAG_ERROR;
							lispreturn(m);
							goto again;
						}
						if(reftag(ref0) == LISP_TAG_FLOAT){
							double tmp = loadfloat(m, ref);
							if(blt == LISP_BUILTIN_ADD)
								fres += tmp;
							else if(blt == LISP_BUILTIN_SUB)
								fres -= tmp;
							else if(blt == LISP_BUILTIN_MUL)
								fres *= tmp;
							else if(blt == LISP_BUILTIN_DIV)
								fres /= tmp;
							else if(blt == LISP_BUILTIN_REM)
								fres = fmod(fres, tmp);
							else
								fprintf(stderr, "invalid op %d for float\n", blt);
						} else {
							long long tmp = loadint(m, ref);
							if(blt == LISP_BUILTIN_ADD)
								ires += tmp;
							else if(blt == LISP_BUILTIN_SUB)
								ires -= tmp;
							else if(blt == LISP_BUILTIN_MUL)
								ires *= tmp;
							else if(blt == LISP_BUILTIN_DIV)
								ires /= tmp;
							else if(blt == LISP_BUILTIN_BITIOR)
								ires |= tmp;
							else if(blt == LISP_BUILTIN_BITAND)
								ires &= tmp;
							else if(blt == LISP_BUILTIN_BITXOR)
								ires ^= tmp;
							else if(blt == LISP_BUILTIN_BITNOT)
								ires = ~tmp;
							else if(blt == LISP_BUILTIN_REM)
								ires %= tmp;
						}
						m->expr = lispload(m, m->expr, 1);
					}
					if(reftag(ref0) == LISP_TAG_FLOAT){
						if(blt == LISP_BUILTIN_SUB && nterms == 0)
							fres = -fres;
						m->value = mkfloat(m, fres);
					} else {
						if(blt == LISP_BUILTIN_SUB && nterms == 0)
							ires = -ires;
						m->value = mkint(m, ires);
					}
					lispreturn(m);
					goto again;
				} else if(blt == LISP_BUILTIN_ISPAIR){ // (pair? ...)
					m->expr = lispload(m, m->expr, 1);
					m->expr = lispload(m, m->expr, 0);
					if(ispair(m, m->expr))
						m->value =  mkref(LISP_BUILTIN_TRUE, LISP_TAG_BUILTIN);
					else
						m->value =  mkref(LISP_BUILTIN_FALSE, LISP_TAG_BUILTIN);
					lispreturn(m);
					goto again;
				} else if(blt == LISP_BUILTIN_ISEQ){ // (eq? ...)
					lispref_t ref0;
					m->expr = lispload(m, m->expr, 1);
					ref0 = lispload(m, m->expr, 0);
					m->expr = lispload(m, m->expr, 1);
					while(m->expr != LISP_NIL){
						lispref_t ref = lispload(m, m->expr, 0);
						if(reftag(ref0) != reftag(ref)){
							m->value = mkref(LISP_BUILTIN_FALSE, LISP_TAG_BUILTIN);
							goto eqdone;
						}
						if(reftag(ref0) == LISP_TAG_FLOAT){
							double v0, v;
							v0 = loadfloat(m, ref0);
							v = loadfloat(m, ref);
							if(v0 != v){
								m->value = mkref(LISP_BUILTIN_FALSE, LISP_TAG_BUILTIN);
								goto eqdone;
							}
						} else if(reftag(ref0) == LISP_TAG_INTEGER || reftag(ref0) == LISP_TAG_BIGINT){
							long long v0, v;
							v0 = loadint(m, ref0);
							v = loadint(m, ref);
							if(v0 != v){
								m->value = mkref(LISP_BUILTIN_FALSE, LISP_TAG_BUILTIN);
								goto eqdone;
							}
						} else if(refval(ref0) != refval(ref)){
							m->value = mkref(LISP_BUILTIN_FALSE, LISP_TAG_BUILTIN);
							goto eqdone;
						}
						m->expr = lispload(m, m->expr, 1);
					}
					m->value = mkref(LISP_BUILTIN_TRUE, LISP_TAG_BUILTIN);
				eqdone:
					lispreturn(m);
					goto again;
				} else if(blt == LISP_BUILTIN_ISLESS){
					lispref_t ref0, ref1;
					m->expr = lispload(m, m->expr, 1);
					ref0 = lispload(m, m->expr, 0);
					m->expr = lispload(m, m->expr, 1);
					ref1 = lispload(m, m->expr, 0);
					m->value = mkref(LISP_BUILTIN_FALSE, LISP_TAG_BUILTIN); // default to false.
					if((reftag(ref0) == LISP_TAG_INTEGER || reftag(ref0) == LISP_TAG_BIGINT)
					&& (reftag(ref1) == LISP_TAG_INTEGER || reftag(ref1) == LISP_TAG_BIGINT)){
						long long i0, i1;
						i0 = loadint(m, ref0);
						i1 = loadint(m, ref1);
						if(i0 < i1)
							m->value = mkref(LISP_BUILTIN_TRUE, LISP_TAG_BUILTIN);
					} else if(reftag(ref0) == LISP_TAG_FLOAT && reftag(ref1) == LISP_TAG_FLOAT){
						double f0, f1;
						f0 = loadfloat(m, ref0);
						f1 = loadfloat(m, ref1);
						if(f0 < f1)
							m->value = mkref(LISP_BUILTIN_TRUE, LISP_TAG_BUILTIN);
					} else if((reftag(ref0) == LISP_TAG_SYMBOL && reftag(ref1) == LISP_TAG_SYMBOL)
						|| (reftag(ref0) == LISP_TAG_STRING && reftag(ref1) == LISP_TAG_STRING)){
						if(strcmp((char*)pointer(m, ref0), (char*)pointer(m, ref1)) < 0)
							m->value = mkref(LISP_BUILTIN_TRUE, LISP_TAG_BUILTIN);
					} else if(reftag(ref0) < reftag(ref1)){
						m->value = mkref(LISP_BUILTIN_TRUE, LISP_TAG_BUILTIN);
					}
					lispreturn(m);
					goto again;
				} else if(blt == LISP_BUILTIN_ISERROR){ // (error? ...)
					m->expr = lispload(m, m->expr, 1);
					m->expr = lispload(m, m->expr, 0);
					if(lisperror(m, m->expr))
						m->value =  mkref(LISP_BUILTIN_TRUE, LISP_TAG_BUILTIN);
					else
						m->value =  mkref(LISP_BUILTIN_FALSE, LISP_TAG_BUILTIN);
					lispreturn(m);
					goto again;
				} else if(blt == LISP_BUILTIN_SETCAR || blt == LISP_BUILTIN_SETCDR){
					m->reg2 = lispload(m, m->expr, 1); // cons ref
					m->reg3 = lispload(m, m->reg2, 1); // val ref
					m->reg2 = lispload(m, m->reg2, 0); // cons
					m->reg3 = lispload(m, m->reg3, 0); // val
					lispstore(m, m->reg2, blt == LISP_BUILTIN_SETCAR ? 0 : 1, m->reg3);
					m->reg2 = LISP_NIL;
					m->reg3 = LISP_NIL;
					lispreturn(m);
					goto again;
				} else if(blt == LISP_BUILTIN_CAR){
					m->reg2 = lispload(m, m->expr, 1);
					m->reg2 = lispload(m, m->reg2, 0);
					m->value = lispload(m, m->reg2, 0);
					m->reg2 = LISP_NIL;
					lispreturn(m);
					goto again;
				} else if(blt == LISP_BUILTIN_CDR){
					m->reg2 = lispload(m, m->expr, 1);
					m->reg2 = lispload(m, m->reg2, 0);
					m->value = lispload(m, m->reg2, 1);
					m->reg2 = LISP_NIL;
					lispreturn(m);
					goto again;
				} else if(blt == LISP_BUILTIN_CONS){
					m->reg2 = lispload(m, m->expr, 1);
					m->reg3 = lispload(m, m->reg2, 1);
					m->reg2 = lispload(m, m->reg2, 0);
					m->reg3 = lispload(m, m->reg3, 0);
					m->value = lispcons(m, m->reg2, m->reg3);
					m->reg2 = LISP_NIL;
					m->reg3 = LISP_NIL;
					lispreturn(m);
					goto again;
				} else if(blt == LISP_BUILTIN_CALLCC){
					m->expr = lispload(m, m->expr, 1);
					m->reg2 = lispcons(m, mkref(LISP_BUILTIN_CONTINUE, LISP_TAG_BUILTIN), m->stack);
					m->reg3 = lispcons(m, m->reg2, LISP_NIL);
					m->expr = lispcons(m, lispload(m, m->expr, 0), m->reg3);
					m->reg2 = LISP_NIL;
					m->reg3 = LISP_NIL;
					lispgoto(m, LISP_STATE_EVAL);
					goto again;
				} else if(blt == LISP_BUILTIN_CALLEXT){
					lispgoto(m, LISP_STATE_CONTINUE);
					return 1;
				} else if(blt == LISP_BUILTIN_PRINT1){
					m->expr = lispload(m, m->expr, 1);
					m->reg2 = lispload(m, m->expr, 0);
					m->expr = lispload(m, m->expr, 1);
					m->expr = lispload(m, m->expr, 0);
					atomprint(m, m->expr, loadint(m, m->reg2));
					m->reg2 = LISP_NIL;
					m->value = m->expr;
					lispreturn(m);
					goto again;
				} else if(blt == LISP_BUILTIN_CLEANENV){
					m->value = m->cleanenvr;
					lispreturn(m);
					goto again;
				}else if(blt == LISP_BUILTIN_EVAL){
					m->expr = lispload(m, lispload(m, m->expr, 1), 0); // expr = cdar expr
					lispgoto(m, LISP_STATE_EVAL);
					goto again;
				}

			} else if(ispair(m, head)){

				// form is ((beta (lambda...)) args), or a continuation.
				// ((beta (lambda args . body) . envr) args) -> (body),
				// with a new environment

				lispref_t beta, lambda;
				beta = lispload(m, m->expr, 0);
				if(beta == LISP_NIL){
					m->value = LISP_TAG_ERROR;
					lispreturn(m);
					goto again;
				}
				head = lispload(m, beta, 0);
				if(reftag(head) != LISP_TAG_BUILTIN){
					m->value = LISP_TAG_ERROR;
					lispreturn(m);
					goto again;
				}
				if(refval(head) == LISP_BUILTIN_CONTINUE){
					// ((continue . stack) return-value)
					m->stack = lispload(m, beta, 1);
					m->value = lispload(m, m->expr, 1);
					if(ispair(m, m->value))
						m->value = lispload(m, m->value, 0);
					lispreturn(m);
					goto again;
				}
				if(refval(head) != LISP_BUILTIN_BETA){
					fprintf(stderr, "applying list with non-beta head\n");
					m->value = LISP_TAG_ERROR;
					lispreturn(m);
					goto again;
				}

				lambda = lispload(m, lispload(m, beta, 1), 0);
				m->envr = lispload(m, lispload(m, beta, 1), 1);

				lispref_t *argnames = lispregister(m, lispload(m, lambda, 1));
				lispref_t *args = lispregister(m, lispload(m, m->expr, 1)); // args = cdr expr
				if(ispair(m, *argnames)){
					// loop over argnames and args simultaneously, cons
					// them as pairs to the environment
					*argnames = lispload(m, *argnames, 0);
					while(ispair(m, *argnames) && ispair(m, *args)){
						lispref_t *pair = lispregister(m, lispcons(m,
							lispload(m, *argnames, 0),
							lispload(m, *args, 0)));
						m->envr = lispcons(m, *pair, m->envr);
						*argnames = lispload(m, *argnames, 1);
						*args = lispload(m, *args, 1);
						lisprelease(m, pair);
					}
				}

				// scheme-style variadic: argnames list terminates in a
				// symbol instead of LISP_NIL, associate the rest of argslist
				// with it. notice: (lambda x (body)) also lands here.
				if(*argnames != LISP_NIL && !ispair(m, *argnames)){
					lispref_t *pair = lispregister(m, lispcons(m, *argnames, *args));
					m->envr = lispcons(m, *pair, m->envr);
					lisprelease(m, pair);
				} else if(*argnames != LISP_NIL || *args != LISP_NIL){
					fprintf(stderr, "mismatch in number of function args\n");
					m->value = LISP_TAG_ERROR;
					lispreturn(m);
					goto again;
				}
				

				// push a new 'environment head' in.
				m->envr = lispcons(m, LISP_NIL, m->envr);
				// clear the registers.
				lisprelease(m, argnames);
				lisprelease(m, args);

				// parameters are bound, pull body from lambda to m->expr.
				beta = lispload(m, m->expr, 0); // beta = car expr
				lambda = lispload(m, lispload(m, beta, 1), 0);
				m->expr = lispload(m, lispload(m, lambda, 1), 1);
				m->stack = lispcons(m, m->expr, m->stack);
	case LISP_STATE_BETA1:
				m->reg2 = lispload(m, m->stack, 0);
				if(m->reg2 != LISP_NIL){
					m->expr = lispload(m, m->reg2, 0);
					m->reg2 = lispload(m, m->reg2, 1);
					lispstore(m, m->stack, 0, m->reg2);
					if(m->reg2 != LISP_NIL){
						lispcall(m, LISP_STATE_BETA1, LISP_STATE_EVAL);
						goto again;
					} else { // tail call
						m->stack = lispload(m, m->stack, 1); // pop expr
						lispgoto(m, LISP_STATE_EVAL);
						goto again;
					}
				}
				m->stack = lispload(m, m->stack, 1); // pop expr (body-list).
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
		m->value = LISP_TAG_ERROR;
		lispreturn(m);
		goto again;
	case LISP_STATE_LISTEVAL:
		{
			lispref_t *state;
			// construct initial evaluation state: (expr-cell . value-cell)
			state = &m->reg2;
			m->reg3 = lispcons(m, LISP_NIL, LISP_NIL); // first "value-cell"
			m->stack = lispcons(m, m->reg3, m->stack); // push it on the stack
			*state = lispcons(m, m->expr, m->reg3); // construct evaluation state
			m->stack = lispcons(m, *state, m->stack); // push evaluation state
			m->reg3 = m->expr;
			goto listeval_first;
	case LISP_STATE_LISTEVAL1:
			state = &m->reg2;
			*state = lispload(m, m->stack, 0);
			// store new m->value to value-cell.
			lispstore(m, lispload(m, *state, 1), 0, m->value);
			// load expr-cell to reg3.
			m->reg3 = lispload(m, *state, 0); // expr-cell = car(state)
listeval_first:
			if(ispair(m, m->reg3)){
				// load new expr from expr-cell and set expr-cell to next expr.
				m->expr = lispload(m, m->reg3, 0); // expr = (car expr-cell)
				m->reg3 = lispload(m, m->reg3, 1); // reg3 = (cdr expr-cell)
				lispstore(m, *state, 0, m->reg3); //  car(state) = (cdr expr-cell)

				// allocate new value-cell, link cdr of current value-cell to it and set value-cell to the new one.
				m->reg4 = lispcons(m, LISP_NIL, LISP_NIL);
				m->reg3 = lispload(m, *state, 1); // reg3 = value-cell ; cdr(state)
				lispstore(m, m->reg3, 1, m->reg4); // cdr(cdr state) = reg4, cdr(value-cell) = reg4
				lispstore(m, *state, 1, m->reg4); // value-cell = reg4

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
				lispstore(m, *state, 0, LISP_NIL);
				*state = LISP_NIL;
				m->reg3 = LISP_NIL;
				m->reg4 = LISP_NIL;
				lispcall(m, LISP_STATE_LISTEVAL2, LISP_STATE_EVAL);
				goto again;
	case LISP_STATE_LISTEVAL2:
				state = &m->reg2;
				*state = lispload(m, m->stack, 0);
				// store new m->value to the rest of value chain.
				m->reg3 = lispload(m, *state, 1);
				lispstore(m, m->reg3, 1, m->value);
			}

			*state = LISP_NIL;
			m->reg3 = LISP_NIL;
			m->reg4 = LISP_NIL;
			m->stack = lispload(m, m->stack, 1);
			m->value = lispload(m, m->stack, 0);
			m->value = lispload(m, m->value, 1); // skip over 'artificial' head value.
			m->stack = lispload(m, m->stack, 1);
			lispreturn(m);
			goto again;
		}
	}
}

static uint32_t *
allocbit(size_t i)
{
	uint32_t *map;
	size_t nmap = (i+31)/32;

	map = malloc(nmap * sizeof map[0]);
	memset(map, 0, nmap * sizeof map[0]);
	return map;
}

static uint32_t
getbit(uint32_t *map, size_t i)
{
	return map[i/32] & (1 << (i&31));
}

static void
setbit(uint32_t *map, size_t i)
{
	map[i/32] |= 1 << (i&31);
}

static lispref_t
lispcopy(Mach *m, uint32_t *isatom, uint32_t *isforw, Mach *oldm, lispref_t ref)
{
	lispref_t nref;
	size_t off;

	switch(reftag(ref)){
	case LISP_TAG_INTEGER:
	case LISP_TAG_ERROR:
	case LISP_TAG_BUILTIN:
		nref = ref;
		goto done;
	default:
		if(getbit(isforw, urefval(ref)) != 0){
			nref = lispload(oldm, ref, 0);
			goto done;
		}
		break;
	}

	off = m->memlen;
	switch(reftag(ref)){
	default:
		fprintf(stderr, "lispcopy: unknown tag %d\n", reftag(ref));
		return LISP_NIL;
	case LISP_TAG_PAIR:
		if(ref == LISP_NIL)
			return LISP_NIL;
		nref = lispcons(m, lispload(oldm, ref, 0), lispload(oldm, ref, 1));
		goto forw;
	case LISP_TAG_BIGINT:
		nref = mkint(m, loadint(oldm, ref));
		goto forw;
	case LISP_TAG_FLOAT:
		nref = mkfloat(m, loadfloat(oldm, ref));
		goto forw;
	case LISP_TAG_STRING:
	case LISP_TAG_SYMBOL:
		//fprintf(stderr, "gc: %s: %s\n", reftag(ref) == LISP_TAG_SYMBOL ? "symbol" : "string", (char *)pointer(oldm, ref));
		nref = mkstring(m, (char *)pointer(oldm, ref), reftag(ref));
	forw:
		lispstore(oldm, ref, 0, nref);
		setbit(isforw, urefval(ref));
		break;
	}
	if(reftag(ref) != LISP_TAG_PAIR)
		for(;off < m->memlen; off++)
			setbit(isatom, off);
done:
	if(nref == LISP_NIL)
		fprintf(stderr, "lispcopy: returning LISP_NIL!\n");
	return nref;
}

void
lispgc(Mach *m)
{
	size_t i;
	uint32_t *isatom;
	uint32_t *isforw;
	Mach oldm;

	if(m->memlen == 0)
		return;

	memcpy(&oldm, m, sizeof oldm);

	isatom = allocbit(m->memlen);
	isforw = allocbit(m->memlen);
	m->mem = malloc(m->memcap * sizeof m->mem[0]);
	memset(m->mem, 0, m->memcap * sizeof m->mem[0]);
	m->memlen = 0;
	m->idxcap = 0;
	m->idxlen = 0;
	m->gclock++;

	// copy registers as roots.
	lispref_t reg0tmp = lispcopy(m, isatom, isforw, &oldm, oldm.reg0);
	lispref_t reg1tmp = lispcopy(m, isatom, isforw, &oldm, oldm.reg1);
	m->reg2 = lispcopy(m, isatom, isforw, &oldm, oldm.reg2);
	m->reg3 = lispcopy(m, isatom, isforw, &oldm, oldm.reg3);
	m->reg4 = lispcopy(m, isatom, isforw, &oldm, oldm.reg4);

	// copy the "dynamic" registers.
	uint32_t reguse = oldm.reguse;
	for(size_t i = 0; i < nelem(m->regs); i++)
		if((reguse & (1<<i)) != 0)
			m->regs[i] = lispcopy(m, isatom, isforw, &oldm, oldm.regs[i]);
	m->reguse = reguse;

	m->value = lispcopy(m, isatom, isforw, &oldm, oldm.value);
	m->expr = lispcopy(m, isatom, isforw, &oldm, oldm.expr);
	m->envr = lispcopy(m, isatom, isforw, &oldm, oldm.envr);
	m->stack = lispcopy(m, isatom, isforw, &oldm, oldm.stack);

	m->cleanenvr = lispcopy(m, isatom, isforw, &oldm, oldm.cleanenvr);

	// cheney style breadth first scan, m->memlen effectively
	// acts as the "tail" while i is the "head", and pointers
	// between i and tail are yet to be converted (forwarded)
	// we should convert this into an incremental gc scheme, where
	// both spaces are exposed to lispload and lispstore, and perform
	// the operation on the approppriate space based on the bitmap.
	for(i = 2; i < m->memlen; i++){
		if(getbit(isatom, i) != 0)
			continue;
		m->mem[i] = lispcopy(m, isatom, isforw, &oldm, m->mem[i]);
	}

	// re-copy the registers used by cons, lispcopy calls cons itself so
	// they get overwritten over and over again during gc.
	m->reg0 = reg0tmp;
	m->reg1 = reg1tmp;

	free(oldm.mem);
	free(isatom);
	free(isforw);

//fprintf(stderr, "."); fflush(stderr);
	m->gclock--;
}

void
lispinit(Mach *m)
{
	// install initial environment (define built-ins)
	int i;
	m->envr = lispcons(m, LISP_NIL, LISP_NIL);
	for(i = 0; i < LISP_NUM_BUILTINS; i++){
		lispref_t sym;
		sym = mkstring(m, bltnames[i], LISP_TAG_SYMBOL);
		lispdefine(m, sym, mkref(i, LISP_TAG_BUILTIN));
		m->cleanenvr = lispload(m, m->envr, 1);
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

