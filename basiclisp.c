
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <math.h>

typedef unsigned int ref_t;
#define NIL (ref_t)0
#define nelem(x) (sizeof(x)/sizeof(x[0]))
#define MKREF(val, tag) (((val)<<3)|((tag)&7))

enum {
	TAG_CONS = 0,	// first so that NIL value of 0 is a cons.
	TAG_BIGINT,
	TAG_FLOAT,
	TAG_INTEGER,
	TAG_STRING,
	TAG_SYMBOL,
	TAG_ERROR,
	TAG_BUILTIN,

	BLT_IF = 0,
	BLT_BETA,
	BLT_CONTINUE,
	BLT_DEFINE,
	BLT_LAMBDA,
	BLT_QUOTE,
	BLT_CALLEXT,
	BLT_CALLCC,
	BLT_SET,
	BLT_SETCAR,
	BLT_SETCDR,
	BLT_CONS,
	BLT_CAR,
	BLT_CDR,
	BLT_LOCALENV,
	// arithmetic
	BLT_ADD,
	BLT_SUB,
	BLT_MUL,
	BLT_DIV,
	BLT_BITIOR,
	BLT_BITAND,
	BLT_BITXOR,
	BLT_BITNOT,
	BLT_REM,
	// predicates
	BLT_ISPAIR,
	BLT_ISEQ,
	BLT_ISLESS,
	BLT_TRUE,
	BLT_FALSE,
	// io
	BLT_PRINT1,
	NUM_BLT,

	// states for vmstep()
	INS_APPLY,
	INS_BETA1,
	INS_CONTINUE,
	INS_DEFINE1,
	INS_SET1,
	INS_RETURN,
	INS_EVAL,
	INS_IF1,
	INS_LISTEVAL,
	INS_LISTEVAL1,
	INS_LISTEVAL2,
	INS_HEAD1,

};

typedef struct Mach Mach;
struct Mach {
	ref_t inst; // state of the vmstep state machine

	ref_t reg0; // temp for mkcons.
	ref_t reg1; // temp for mkcons.
	ref_t reg2;
	ref_t reg3;
	ref_t reg4;

	ref_t valu; // return value
	ref_t expr; // expression being evaluated
	ref_t envr; // current environment, a stack of a-lists.
	ref_t stak; // call stack

	ref_t *idx;
	size_t idxlen;
	size_t idxcap;

	ref_t *mem;
	size_t memlen;
	size_t memcap;

	char *tok;
	size_t toklen;
	size_t tokcap;

	int lineno;

	int gclock;
};

static char *bltnames[] = {
[BLT_IF] = "if",
[BLT_BETA] = "beta",
[BLT_CONTINUE] = "continue",
[BLT_DEFINE] = "define",
[BLT_LAMBDA] = "lambda",
[BLT_QUOTE] = "quote",
[BLT_CALLEXT] = "call-external",
[BLT_CALLCC] = "call-with-current-continuation",
[BLT_SET] = "set!",
[BLT_SETCAR] = "set-car!",
[BLT_SETCDR] = "set-cdr!",
[BLT_CONS] = "cons",
[BLT_CAR] = "car",
[BLT_CDR] = "cdr",
[BLT_LOCALENV] = "local-env",
[BLT_ADD] = "+",
[BLT_SUB] = "-",
[BLT_MUL] = "*",
[BLT_DIV] = "/",
[BLT_BITIOR] = "bitwise-ior",
[BLT_BITAND] = "bitwise-and",
[BLT_BITXOR] = "bitwise-xor",
[BLT_BITNOT] = "bitwise-not",
[BLT_REM] = "remainder",
[BLT_ISPAIR] = "pair?",
[BLT_ISEQ] = "eq?",
[BLT_ISLESS] = "<",
[BLT_TRUE] = "#t",
[BLT_FALSE] = "#f",
[BLT_PRINT1] = "print1",
};

static ref_t
mkref(int val, int tag)
{
	return MKREF(val, tag);
}

static int
reftag(ref_t ref)
{
	return ref & 7;
}

static intptr_t
refval(ref_t ref)
{
	return (intptr_t)ref >> 3;
}

static size_t
urefval(ref_t ref)
{
	return ref >> 3;
}

static ref_t *
pointer(Mach *m, ref_t ref)
{
	size_t off = urefval(ref);
	if(off <= 0 || off >= m->memlen){
		fprintf(stderr, "dereferencing an out of bounds reference: %x\n", ref);
		abort();
	}
	return &m->mem[urefval(ref)];
}

static ref_t
vmload(Mach *m, ref_t base, size_t offset)
{
	ref_t *p = pointer(m, base);
	return p[offset];
}

static ref_t
vmstore(Mach *m, ref_t base, size_t offset, ref_t obj)
{
	ref_t *p = pointer(m, base);
	p[offset] = obj;
	return obj;
}

static int
isatom(Mach *m, ref_t a)
{
	int tag = reftag(a);
	return tag != TAG_SYMBOL && tag != TAG_CONS;
}

static int
issymbol(Mach *m, ref_t a)
{
	return reftag(a) == TAG_SYMBOL;
}

static int
ispair(Mach *m, ref_t a)
{
	return reftag(a) == TAG_CONS && a != NIL;
}

static int
iserror(Mach *m, ref_t a)
{
	return reftag(a) == TAG_ERROR;
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

typedef struct Buf Buf;
struct Buf {
	char *buf;
	size_t off;
	size_t len;
	size_t cap;
	int undo;
	int fd;
};

static int
lex(Mach *m, FILE *fp)
{
	int isinteger, ishex;
	int ch, peekc;

again:
	tokclear(m);
	if((ch = fgetc(fp)) == -1)
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
		while((ch = fgetc(fp)) != -1){
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
		if((peekc = fgetc(fp)) == -1)
			goto caseself;
		ungetc(peekc, fp);
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
			ch = fgetc(fp);
		}
		if(ch != -1)
			ungetc(ch, fp);
		return isinteger ? TAG_INTEGER : TAG_FLOAT;

	// string constant, detect and interpret standard escapes like in c.
	case '"':
		ch = fgetc(fp);
		if(ch == '\n')
			m->lineno++;
		while(ch != -1 && ch != '"'){
			if(ch == '\\'){
				int code;
				ch = fgetc(fp);
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
						ch = fgetc(fp);
					} while(ch >= '0' && ch <= '7');
					if(ch != -1) ungetc(ch, fp);
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
			ch = fgetc(fp);
			if(ch == '\n')
				m->lineno++;
		}
		return TAG_STRING;

	// symbol is any string of nonbreak characters not starting with a number
	default:
	casesym:
		while(ch != -1 && !isbreak(ch)){
			tokappend(m, ch);
			ch = fgetc(fp);
		}
		ungetc(ch, fp);
		return TAG_SYMBOL;
	}
	return -1;
}

void vmgc(Mach *m);

static ref_t
allocate(Mach *m, size_t num, int tag)
{
	ref_t ref;
	int didgc = 0;
	// first, try gc.
	if(!m->gclock && (m->memcap - m->memlen) < num){
		vmgc(m);
		didgc = 1;
	}
recheck:
	if((didgc && 4*m->memlen >= 3*m->memcap) || (m->memcap - m->memlen) < num){
		m->memcap = (m->memcap == 0) ? 256 : 2*m->memcap;
		m->mem = realloc(m->mem, m->memcap * sizeof m->mem[0]);
		memset(m->mem + m->memlen, 0, m->memcap - m->memlen);
		goto recheck;
	}
	// never return NIL by accident.
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
idxinsert1(Mach *m, uint32_t hash, ref_t ref)
{
	size_t i, off;

	for(i = 0; i < m->idxcap; i++){
		ref_t nref;
		off = (hash + i) & (m->idxcap - 1);
		nref = m->idx[off];
		if(nref == NIL){
			m->idx[off] = ref;
			m->idxlen++;
			return 0;
		}
	}
	return -1;
}

static int
idxinsert(Mach *m, uint32_t hash, ref_t ref)
{
	size_t i;

	if(3*(m->idxlen/2) >= m->idxcap){
		ref_t *old;
		size_t oldcap;

		old = m->idx;
		oldcap = m->idxcap;

		m->idxcap = m->idxcap < 16 ? 16 : 2*m->idxcap;
		m->idx = malloc(m->idxcap * sizeof m->idx[0]);
		memset(m->idx, 0, m->idxcap * sizeof m->idx[0]);
		m->idxlen = 0;
		if(old != NULL){
			for(i = 0; i < oldcap; i++){
				ref_t oldref = old[i];
				if(oldref != NIL){
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

static ref_t
idxlookup(Mach *m, uint32_t hash, char *str)
{
	ref_t ref;
	size_t i, off;

	for(i = 0; i < m->idxcap; i++){
		off = (hash + i) & (m->idxcap - 1);
		ref = m->idx[off];
		if(ref == NIL)
			break;
		if(!strcmp(str, (char *)pointer(m, ref))){
			return ref;
		}
	}
	return NIL;
}

static ref_t
mkany(Mach *m, void *data, size_t len, int type)
{
	size_t num;
	ref_t ref;

	num = (len + sizeof(ref) - 1) / sizeof(ref);
	ref = allocate(m, num, type);
	memcpy(pointer(m, ref), data, len);
	return ref;
}

static ref_t
mkint(Mach *m, long long v)
{
	ref_t ref;

	ref = mkref(v, TAG_INTEGER);
	if((long long)refval(ref) == v)
		return ref;

	return mkany(m, &v, sizeof v, TAG_BIGINT);
}

static ref_t
mkfloat(Mach *m, double v)
{
	return mkany(m, &v, sizeof v, TAG_FLOAT);
}

static ref_t
mkstring(Mach *m, char *str, int type)
{
	uint32_t hash, hash2;
	ref_t ref;
	hash = fnv32a(str, 0);
	if((ref = idxlookup(m, hash, str)) == NIL){
		ref = mkany(m, str, strlen(str)+1, type);
		idxinsert(m, hash, ref);
	}
	return ref;
}

static ref_t
mkcons(Mach *m, ref_t a, ref_t d)
{
	ref_t ref;
	m->reg0 = a;
	m->reg1 = d;
	ref = allocate(m, 2, TAG_CONS);
	vmstore(m, ref, 0, m->reg0);
	vmstore(m, ref, 1, m->reg1);
	m->reg0 = NIL;
	m->reg1 = NIL;
	return ref;
}

static ref_t
listparse(Mach *m, FILE *fp, int justone)
{
	ref_t list = NIL, prev = NIL, cons, nval;
	int ltok;
	int dot = 0;

m->gclock++;
	while((ltok = lex(m, fp)) != -1){
		tokappend(m, '\0');
		switch(ltok){
		default:
			fprintf(stderr, "unknown token %d: '%c' '%s'\n", ltok, ltok, m->tok);
			break;
		case ')':
			goto done;
		case '(':
			nval = listparse(m, fp, 0);
			goto append;
		case '\'':
			nval = listparse(m, fp, 1);
			nval = mkcons(m, mkref(BLT_QUOTE, TAG_BUILTIN), mkcons(m, nval, NIL));
			goto append;
		case '.':
			dot++;
			break;
		case ',':
			fprintf(stderr, "TODO: backquote not implemented yet\n");
			break;
		case TAG_INTEGER:
			nval = mkint(m, strtoll(m->tok, NULL, 0));
			goto append;
		case TAG_FLOAT:
			nval = mkfloat(m, strtod(m->tok, NULL));
			goto append;
		case TAG_STRING:
			nval = mkstring(m, m->tok, TAG_STRING);
			goto append;
		case TAG_SYMBOL:
			nval = mkstring(m, m->tok, TAG_SYMBOL);
		append:
			if(justone){
				list = nval;
				goto done;
			}
			if(dot == 0){
				cons = mkcons(m, nval, NIL);
				if(prev != NIL)
					vmstore(m, prev, 1, cons);
				else
					list = cons;
				prev = cons;
			} else if(dot == 1){
				vmstore(m, prev, 1, nval);
				dot++;
			} else {
				fprintf(stderr, "malformed s-expression, bad use of '.'\n");
				goto done;
			}
			break;
		}
	}
	if(justone && ltok == -1)
		list = TAG_ERROR;
done:
m->gclock--;
	return list;
}

static long long
loadint(Mach *m, ref_t ref)
{
	int tag = reftag(ref);
	if(tag == TAG_INTEGER)
		return (long long)refval(ref);
	if(tag == TAG_BIGINT)
		return *(long long *)pointer(m, ref);
	fprintf(stderr, "loadint: non-integer reference\n");
	return 0;
}

static double
loadfloat(Mach *m, ref_t ref)
{
	if(reftag(ref) == TAG_FLOAT)
		return *(double *)pointer(m, ref);
	fprintf(stderr, "loadfloat: non-float reference\n");
	return 0;
}

static int
atomprint(Mach *m, ref_t aref, FILE *fp)
{
	int tag = reftag(aref);
	switch(tag){
	default:
		fprintf(stderr, "listprint: unknown atom: val:%zx tag:%x\n", refval(aref), reftag(aref));
		break;
	case TAG_BUILTIN:
		if(refval(aref) >= 0 && refval(aref) < NUM_BLT)
			fprintf(fp, "%s", bltnames[refval(aref)]);
		else
			fprintf(fp, "blt-0x%zx", refval(aref));
		break;
	case TAG_ERROR:
		fprintf(fp, "error %zx\n", refval(aref));
		break;
	case TAG_CONS:
		if(aref == NIL)
			fprintf(fp, "()");
		else
			fprintf(fp, "cons(#x%x)", aref);
		break;
	case TAG_INTEGER:
		fprintf(fp, "%zd", refval(aref));
		break;
	case TAG_BIGINT:
		fprintf(fp, "%lld", *(long long *)pointer(m, aref));
		break;
	case TAG_FLOAT:
		fprintf(fp, "%f", *(double *)pointer(m, aref));
		break;
	case TAG_STRING:
		fprintf(fp, "%s", (char *)pointer(m, aref));
		break;
	case TAG_SYMBOL:
		fprintf(fp, "%s", (char *)pointer(m, aref));
		break;
	}
	return tag;
}

static void
vmgoto(Mach *m, ref_t inst)
{
	m->inst = mkref(inst, TAG_BUILTIN);
}

static void
vmcall(Mach *m, ref_t ret, ref_t inst)
{
	m->stak = mkcons(m, m->envr, m->stak);
	m->stak = mkcons(m, mkref(ret, TAG_BUILTIN), m->stak);
	vmgoto(m, inst);
}

static void
vmreturn(Mach *m)
{
	m->inst = vmload(m, m->stak, 0);
	m->stak = vmload(m, m->stak, 1);
	m->envr = vmload(m, m->stak, 0);
	m->stak = vmload(m, m->stak, 1);
	m->expr = NIL;
}

static void
vmdefine(Mach *m, ref_t sym, ref_t val)
{
	ref_t *pair = &m->reg2;
	ref_t *env = &m->reg3;
	*pair = mkcons(m, sym, val);
	*env = vmload(m, m->envr, 1);
	*env = mkcons(m, *pair, *env);
	vmstore(m, m->envr, 1, *env);
}

int
vmstep(Mach *m)
{
	size_t i;
again:
	if(reftag(m->inst) != TAG_BUILTIN){
		fprintf(stderr, "vmstep: inst is not built-in, stack corruption?\n");
		abort();
	}
	switch(refval(m->inst)){
	default:
		fprintf(stderr, "vmstep: invalid instruction %zd, bailing out.\n", refval(m->inst));
	case INS_CONTINUE:
		vmreturn(m);
		goto again;
	case INS_RETURN:
		return 0;
	case INS_EVAL:
		if(isatom(m, m->expr)){
			m->valu = m->expr;
			vmreturn(m);
			goto again;
		} else if(issymbol(m, m->expr)){
			ref_t lst = m->envr;
			while(lst != NIL){
				ref_t pair = vmload(m, lst, 0);
				if(ispair(m, pair)){
					ref_t key = vmload(m, pair, 0);
					if(key == m->expr){
						m->valu = vmload(m, pair, 1);
						break;
					}
				}
				lst = vmload(m, lst, 1);
			}
			if(lst == NIL){
				fprintf(stderr, "undefined symbol: %s\n", (char *)pointer(m, m->expr));
				m->valu = m->expr; // undefined
			}
			vmreturn(m);
			goto again;
		} else if(ispair(m, m->expr)){
			// form is (something), so at least we are applying something.
			// handle special forms (quote, lambda, beta, if, define) before
			// evaluating args.

			// TODO: evaluating the head element here is a bit of a hack.

			ref_t head;
			m->stak = mkcons(m, m->expr, m->stak);
			m->expr = vmload(m, m->expr, 0);
			vmcall(m, INS_HEAD1, INS_EVAL);
			goto again;
	case INS_HEAD1:
			head = m->valu;
			m->valu = NIL;
			m->expr = vmload(m, m->stak, 0);
			m->stak = vmload(m, m->stak, 1);

			if(reftag(head) == TAG_BUILTIN){
				ref_t blt = refval(head);
				if(blt == BLT_IF){
					// (if cond then else)
					m->expr = vmload(m, m->expr, 1);
					m->stak = mkcons(m, m->expr, m->stak);
					// evaluate condition recursively
					m->expr = vmload(m, m->expr, 0);
					vmcall(m, INS_IF1, INS_EVAL);
					goto again;
	case INS_IF1:
					// evaluate result as a tail-call, if condition
					// evaluated to #f, skip over 'then' to 'else'.
					m->expr = vmload(m, m->stak, 0);
					m->stak = vmload(m, m->stak, 1);
					m->expr = vmload(m, m->expr, 1);
					if(m->valu == MKREF(BLT_FALSE, TAG_BUILTIN))
						m->expr = vmload(m, m->expr, 1);
					m->expr = vmload(m, m->expr, 0);
					vmgoto(m, INS_EVAL);
					goto again;
				}
				if(blt == BLT_BETA || blt == BLT_CONTINUE){
					// beta literal: return self.
					m->valu = m->expr;
					vmreturn(m);
					goto again;
				}
				if(blt == BLT_DEFINE || blt == BLT_SET){
					// (define sym val) -> args,
					// current environment gets sym associated with val.
					ref_t *sym = &m->reg2;
					ref_t *val = &m->reg3;
					ref_t *tmp = &m->reg4;
					*sym = vmload(m, m->expr, 1);
					*val = vmload(m, *sym, 1);
					*sym = vmload(m, *sym, 0);
					if(ispair(m, *sym)){
						// scheme shorthand: (define (name args...) body1 body2...).
						*tmp = mkcons(m, vmload(m, *sym, 1), *val);
						*val = mkcons(m, mkref(BLT_LAMBDA, TAG_BUILTIN), *tmp);
						*sym = vmload(m, *sym, 0);
					} else {
						*val = vmload(m, *val, 0);
					}
					m->stak = mkcons(m, *sym, m->stak);
					m->expr = *val;
					*sym = NIL;
					*val = NIL;

					vmcall(m, blt == BLT_SET ? INS_SET1 : INS_DEFINE1, INS_EVAL);
					goto again;
	case INS_DEFINE1:
					sym = &m->reg2;
					ref_t *env = &m->reg3;
					// restore sym from stak, construct (sym . val)
					*sym = vmload(m, m->stak, 0);
					m->stak = vmload(m, m->stak, 1);
					*sym = mkcons(m, *sym, m->valu);
					// push new (sym . val) just below current env head.
					*env = vmload(m, m->envr, 1);
					*env = mkcons(m, *sym, *env);
					vmstore(m, m->envr, 1, *env);
					*env = NIL;
					*sym = NIL;
					vmreturn(m);
					goto again;
	case INS_SET1:
					sym = &m->reg2;
					env = &m->reg3;
					// restore sym from stak, construct (sym . val)
					*sym = vmload(m, m->stak, 0);
					m->stak = vmload(m, m->stak, 1);
					ref_t lst = m->envr;
					ref_t pair;
					while(lst != NIL){
						pair = vmload(m, lst, 0);
						if(ispair(m, pair)){
							ref_t key = vmload(m, pair, 0);
							if(key == *sym)
								break;
						}
						lst = vmload(m, lst, 1);
					}
					if(lst != NIL)
						vmstore(m, pair, 1, m->valu);
					else
						fprintf(stderr, "set!: undefined symbol %s\n", (char *)pointer(m, *sym)); 
					*env = NIL;
					*sym = NIL;
					m->valu = NIL;
					vmreturn(m);
					goto again;
				}
				if(blt == BLT_LAMBDA){
					// (lambda args body) -> (beta (lambda args body) envr)
					ref_t *tmp = &m->reg2;
					*tmp = mkcons(m, m->expr, m->envr);
					m->valu = mkcons(m, mkref(BLT_BETA, TAG_BUILTIN), *tmp);
					vmreturn(m);
					goto again;
				}
				if(blt == BLT_QUOTE){
					// (quote args) -> args
					m->valu = vmload(m, vmload(m, m->expr, 1), 0); // expr = cdar expr
					vmreturn(m);
					goto again;
				}
			}

			// at this point we know it is a list, and that it is
			// not a special form. evaluate args, then apply.
			vmcall(m, INS_APPLY, INS_LISTEVAL);
			goto again;
	case INS_APPLY:
			m->expr = m->valu;
			head = vmload(m, m->expr, 0);
			if(reftag(head) == TAG_BUILTIN){
				ref_t blt = refval(head);
				if(blt >= BLT_ADD && blt <= BLT_REM){
					ref_t ref0, ref, tag0, tag;
					long long ires;
					double fres;
					int nterms;
					m->expr = vmload(m, m->expr, 1);
					ref0 = vmload(m, m->expr, 0);
					tag0 = reftag(ref0);
					if(tag0 == TAG_INTEGER || tag0 == TAG_BIGINT){
						ires = loadint(m, ref0);
						tag0 = TAG_INTEGER;
					} else if(tag0 == TAG_FLOAT){
						fres = loadfloat(m, ref0);
					} else {
						m->valu = TAG_ERROR;
						vmreturn(m);
						goto again;
					}
					m->expr = vmload(m, m->expr, 1);
					nterms = 0;
					while(m->expr != NIL){
						nterms++;
						ref = vmload(m, m->expr, 0);
						tag = reftag(ref);
						if(tag == TAG_BIGINT)
							tag = TAG_INTEGER;
						if(tag0 != tag){
							m->valu = TAG_ERROR;
							vmreturn(m);
							goto again;
						}
						if(reftag(ref0) == TAG_FLOAT){
							double tmp = loadfloat(m, ref);
							if(blt == BLT_ADD)
								fres += tmp;
							else if(blt == BLT_SUB)
								fres -= tmp;
							else if(blt == BLT_MUL)
								fres *= tmp;
							else if(blt == BLT_DIV)
								fres /= tmp;
							else if(blt == BLT_REM)
								fres = fmod(fres, tmp);
							else
								fprintf(stderr, "invalid op %d for float\n", blt);
						} else {
							long long tmp = loadint(m, ref);
							if(blt == BLT_ADD)
								ires += tmp;
							else if(blt == BLT_SUB)
								ires -= tmp;
							else if(blt == BLT_MUL)
								ires *= tmp;
							else if(blt == BLT_DIV)
								ires /= tmp;
							else if(blt == BLT_BITIOR)
								ires |= tmp;
							else if(blt == BLT_BITAND)
								ires &= tmp;
							else if(blt == BLT_BITXOR)
								ires ^= tmp;
							else if(blt == BLT_BITNOT)
								ires = ~tmp;
							else if(blt == BLT_REM)
								ires %= tmp;
						}
						m->expr = vmload(m, m->expr, 1);
					}
					if(reftag(ref0) == TAG_FLOAT){
						if(blt == BLT_SUB && nterms == 0)
							fres = -fres;
						m->valu = mkfloat(m, fres);
					} else {
						if(blt == BLT_SUB && nterms == 0)
							ires = -ires;
						m->valu = mkint(m, ires);
					}
					vmreturn(m);
					goto again;
				} else if(blt == BLT_ISPAIR){ // (pair? ...)
					m->expr = vmload(m, m->expr, 1);
					m->expr = vmload(m, m->expr, 0);
					if(ispair(m, m->expr))
						m->valu =  mkref(BLT_TRUE, TAG_BUILTIN);
					else
						m->valu =  mkref(BLT_FALSE, TAG_BUILTIN);
					vmreturn(m);
					goto again;
				} else if(blt == BLT_ISEQ){ // (eq? ...)
					ref_t ref0;
					m->expr = vmload(m, m->expr, 1);
					ref0 = vmload(m, m->expr, 0);
					m->expr = vmload(m, m->expr, 1);
					while(m->expr != NIL){
						ref_t ref = vmload(m, m->expr, 0);
						if(reftag(ref0) != reftag(ref)){
							m->valu = mkref(BLT_FALSE, TAG_BUILTIN);
							goto eqdone;
						}
						if(reftag(ref0) == TAG_FLOAT){
							double v0, v;
							v0 = loadfloat(m, ref0);
							v = loadfloat(m, ref);
							if(v0 != v){
								m->valu = mkref(BLT_FALSE, TAG_BUILTIN);
								goto eqdone;
							}
						} else if(reftag(ref0) == TAG_INTEGER || reftag(ref0) == TAG_BIGINT){
							long long v0, v;
							v0 = loadint(m, ref0);
							v = loadint(m, ref);
							if(v0 != v){
								m->valu = mkref(BLT_FALSE, TAG_BUILTIN);
								goto eqdone;
							}
						}
						if(refval(ref0) != refval(ref)){
							m->valu = mkref(BLT_FALSE, TAG_BUILTIN);
							goto eqdone;
						}
						m->expr = vmload(m, m->expr, 1);
					}
					m->valu = mkref(BLT_TRUE, TAG_BUILTIN);
				eqdone:
					vmreturn(m);
					goto again;
				} else if(blt == BLT_ISLESS){
					ref_t ref0, ref1;
					m->expr = vmload(m, m->expr, 1);
					ref0 = vmload(m, m->expr, 0);
					m->expr = vmload(m, m->expr, 1);
					ref1 = vmload(m, m->expr, 0);
					m->valu = mkref(BLT_FALSE, TAG_BUILTIN); // default to false.
					if((reftag(ref0) == TAG_INTEGER || reftag(ref0) == TAG_BIGINT)
					&& (reftag(ref1) == TAG_INTEGER || reftag(ref1) == TAG_BIGINT)){
						long long i0, i1;
						i0 = loadint(m, ref0);
						i1 = loadint(m, ref1);
						if(i0 < i1)
							m->valu = mkref(BLT_TRUE, TAG_BUILTIN);
					} else if(reftag(ref0) == TAG_FLOAT && reftag(ref1) == TAG_FLOAT){
						double f0, f1;
						f0 = loadfloat(m, ref0);
						f1 = loadfloat(m, ref1);
						if(f0 < f1)
							m->valu = mkref(BLT_TRUE, TAG_BUILTIN);
					} else if((reftag(ref0) == TAG_SYMBOL && reftag(ref1) == TAG_SYMBOL)
						|| (reftag(ref0) == TAG_STRING && reftag(ref1) == TAG_STRING)){
						if(strcmp((char*)pointer(m, ref0), (char*)pointer(m, ref1)) < 0)
							m->valu = mkref(BLT_TRUE, TAG_BUILTIN);
					} else if(reftag(ref0) < reftag(ref1)){
						m->valu = mkref(BLT_TRUE, TAG_BUILTIN);
					}
					vmreturn(m);
					goto again;
				} else if(blt == BLT_SETCAR || blt == BLT_SETCDR){
					m->reg2 = vmload(m, m->expr, 1); // cons ref
					m->reg3 = vmload(m, m->reg2, 1); // val ref
					m->reg2 = vmload(m, m->reg2, 0); // cons
					m->reg3 = vmload(m, m->reg3, 0); // val
					vmstore(m, m->reg2, blt == BLT_SETCAR ? 0 : 1, m->reg3);
					m->reg2 = NIL;
					m->reg3 = NIL;
					vmreturn(m);
					goto again;
				} else if(blt == BLT_CAR){
					m->reg2 = vmload(m, m->expr, 1);
					m->reg2 = vmload(m, m->reg2, 0);
					m->valu = vmload(m, m->reg2, 0);
					m->reg2 = NIL;
					vmreturn(m);
					goto again;
				} else if(blt == BLT_CDR){
					m->reg2 = vmload(m, m->expr, 1);
					m->reg2 = vmload(m, m->reg2, 0);
					m->valu = vmload(m, m->reg2, 1);
					m->reg2 = NIL;
					vmreturn(m);
					goto again;
				} else if(blt == BLT_CONS){
					m->reg2 = vmload(m, m->expr, 1);
					m->reg3 = vmload(m, m->reg2, 1);
					m->reg2 = vmload(m, m->reg2, 0);
					m->reg3 = vmload(m, m->reg3, 0);
					m->valu = mkcons(m, m->reg2, m->reg3);
					m->reg2 = NIL;
					m->reg3 = NIL;
					vmreturn(m);
					goto again;
				} else if(blt == BLT_CALLCC){
					m->expr = vmload(m, m->expr, 1);
					m->reg2 = mkcons(m, mkref(BLT_CONTINUE, TAG_BUILTIN), m->stak);
					m->reg3 = mkcons(m, m->reg2, NIL);
					m->expr = mkcons(m, vmload(m, m->expr, 0), m->reg3);
					m->reg2 = NIL;
					m->reg3 = NIL;
					vmgoto(m, INS_EVAL);
					goto again;
				} else if(blt == BLT_CALLEXT){
					vmgoto(m, INS_CONTINUE);
					return 1;
				} else if(blt == BLT_PRINT1){
					m->expr = vmload(m, m->expr, 1);
					m->expr = vmload(m, m->expr, 0);
					atomprint(m, m->expr, stdout);
					m->valu = m->expr;
					vmreturn(m);
					goto again;
				} else if(blt == BLT_LOCALENV){
					m->valu = m->envr;
					vmreturn(m);
					goto again;
				}

			} else if(ispair(m, head)){

				// form is ((beta (lambda...)) args)
				// ((beta (lambda args . body) . envr) args) -> (body),
				// with a new environment

				ref_t beta, lambda;
				ref_t *argnames = &m->reg2;
				ref_t *args = &m->reg3;

				beta = vmload(m, m->expr, 0);
				if(beta == NIL){
					m->valu = TAG_ERROR;
					vmreturn(m);
					goto again;
				}
				head = vmload(m, beta, 0);
				if(reftag(head) != TAG_BUILTIN){
					m->valu = TAG_ERROR;
					vmreturn(m);
					goto again;
				}
				if(refval(head) == BLT_CONTINUE){
					// ((continue . stack) return-value)
					m->stak = vmload(m, beta, 1);
					m->valu = vmload(m, m->expr, 1);
					if(ispair(m, m->valu))
						m->valu = vmload(m, m->valu, 0);
					vmreturn(m);
					goto again;
				}
				if(refval(head) != BLT_BETA){
					m->valu = TAG_ERROR;
					vmreturn(m);
					goto again;
				}

				lambda = vmload(m, vmload(m, beta, 1), 0);
				m->envr = vmload(m, vmload(m, beta, 1), 1);

				*argnames = vmload(m, lambda, 1);
				*args = vmload(m, m->expr, 1); // args = cdr expr
				if(ispair(m, *argnames)){
					// loop over argnames and args simultaneously, cons
					// them as pairs to the environment
					*argnames = vmload(m, *argnames, 0);
					while(ispair(m, *argnames) && ispair(m, *args)){
						ref_t *pair = &m->reg4;
						*pair = mkcons(m,
							vmload(m, *argnames, 0),
							vmload(m, *args, 0));
						m->envr = mkcons(m, *pair, m->envr);
						*argnames = vmload(m, *argnames, 1);
						*args = vmload(m, *args, 1);
						*pair = NIL;
					}
				}

				// scheme-style variadic: argnames list terminates in a
				// symbol instead of nil, associate the rest of argslist
				// with it. notice: (lambda x (body)) also lands here.
				if(*argnames != NIL){
					ref_t *pair = &m->reg4;
					*pair = mkcons(m, *argnames, *args);
					m->envr = mkcons(m, *pair, m->envr);
					*pair = NIL;
				}

				// push a new 'environment head' in.
				m->envr = mkcons(m, NIL, m->envr);
				// clear the registers.
				*argnames = NIL;
				*args = NIL;

				// parameters are bound, pull body from lambda to m->expr.
				beta = vmload(m, m->expr, 0); // beta = car expr
				lambda = vmload(m, vmload(m, beta, 1), 0);
				m->expr = vmload(m, vmload(m, lambda, 1), 1);
				m->stak = mkcons(m, m->expr, m->stak);
	case INS_BETA1:
				m->reg2 = vmload(m, m->stak, 0);
				if(m->reg2 != NIL){
					m->expr = vmload(m, m->reg2, 0);
					m->reg2 = vmload(m, m->reg2, 1);
					vmstore(m, m->stak, 0, m->reg2);
					if(m->reg2 != NIL){
						vmcall(m, INS_BETA1, INS_EVAL);
						goto again;
					} else { // tail call
						m->stak = vmload(m, m->stak, 1); // pop expr
						vmgoto(m, INS_EVAL);
						goto again;
					}
				}
				m->stak = vmload(m, m->stak, 1); // pop expr (body-list).
				vmreturn(m);
				goto again;
			} else {
				fprintf(stderr, "apply: head is weird: ");
				//eval_print(m);
				fprintf(stderr, "\n");
				vmreturn(m);
				goto again;
			}
		}
		fprintf(stderr, "vmstep eval: unrecognized form: ");
		//eval_print(m);
		fprintf(stderr, "\n");
		abort();
		m->valu = TAG_ERROR;
		vmreturn(m);
		goto again;
	case INS_LISTEVAL:
		m->reg3 = mkcons(m, NIL, NIL); // initial value-prev
		m->reg2 = mkcons(m, m->expr, m->reg3);
		m->stak = mkcons(m, m->reg3, m->stak);
		m->stak = mkcons(m, m->reg2, m->stak);
		m->reg3 = m->expr;
		goto listeval_first;
	case INS_LISTEVAL1:
		// top of stack contains (expr-list . value-prev)
		// where both are lists.
		m->reg2 = vmload(m, m->stak, 0);
		// store new m->valu to value-prev.
		m->reg3 = vmload(m, m->reg2, 1);
		vmstore(m, m->reg3, 0, m->valu);
		// load remaining expression to reg3.
		m->reg3 = vmload(m, m->reg2, 0);
listeval_first:
		if(ispair(m, m->reg3)){
			m->expr = vmload(m, m->reg3, 0);
			m->reg3 = vmload(m, m->reg3, 1);
			vmstore(m, m->reg2, 0, m->reg3);

			m->reg4 = mkcons(m, NIL, NIL);
			m->reg3 = vmload(m, m->reg2, 1);
			vmstore(m, m->reg3, 1, m->reg4);
			vmstore(m, m->reg2, 1, m->reg4);
			m->reg2 = NIL;
			m->reg3 = NIL;
			m->reg4 = NIL;
			vmcall(m, INS_LISTEVAL1, INS_EVAL);
			goto again;
		}
		// this following bit is non-standard, it allows one to call a function
		// with the notation (fn . args) or (fn arg1 arg2 . rest), effectively
		// splicing a list 'args' in to the argument list, analogous to how
		// varargs are declared in standard scheme functions. by allowing the
		// dotted notation for apply, there is no need an apply built-in.
		// unfortunately, of course the dotted tail must not be a literal list,
		// so defining apply as a function is still going to be necessary.
		if(m->reg3 != NIL){
			m->expr = m->reg3;
			vmstore(m, m->reg2, 0, NIL);
			m->reg2 = NIL;
			m->reg3 = NIL;
			m->reg4 = NIL;
			vmcall(m, INS_LISTEVAL2, INS_EVAL);
			goto again;
	case INS_LISTEVAL2:
			m->reg2 = vmload(m, m->stak, 0);
			// store new m->valu to the rest of value chain.
			m->reg3 = vmload(m, m->reg2, 1);
			vmstore(m, m->reg3, 1, m->valu);
		}

		m->reg2 = NIL;
		m->reg3 = NIL;
		m->reg4 = NIL;
		m->stak = vmload(m, m->stak, 1);
		m->valu = vmload(m, m->stak, 0);
		m->valu = vmload(m, m->valu, 1); // skip over 'artificial' head value.
		m->stak = vmload(m, m->stak, 1);
		vmreturn(m);
		goto again;
	}
}

uint32_t *
allocbit(size_t i)
{
	uint32_t *map;
	size_t nmap = (i+31)/32;

	map = malloc(nmap * sizeof map[0]);
	memset(map, 0, nmap * sizeof map[0]);
	return map;
}

uint32_t
getbit(uint32_t *map, size_t i)
{
	return map[i/32] & (1 << (i&31));
}

void
setbit(uint32_t *map, size_t i)
{
	map[i/32] |= 1 << (i&31);
}

ref_t
vmcopy(Mach *m, uint32_t *isatom, uint32_t *isforw, Mach *oldm, ref_t ref)
{
	ref_t nref;
	size_t off;

	switch(reftag(ref)){
	case TAG_INTEGER:
	case TAG_ERROR:
	case TAG_BUILTIN:
		nref = ref;
		goto done;
	default:
		if(getbit(isforw, urefval(ref)) != 0){
			nref = vmload(oldm, ref, 0);
			goto done;
		}
		break;
	}

	off = m->memlen;
	switch(reftag(ref)){
	default:
		fprintf(stderr, "vmcopy: unknown tag %d\n", reftag(ref));
		return NIL;
	case TAG_CONS:
		if(ref == NIL)
			return NIL;
		nref = mkcons(m, vmload(oldm, ref, 0), vmload(oldm, ref, 1));
		goto forw;
	case TAG_BIGINT:
		nref = mkint(m, loadint(oldm, ref));
		goto forw;
	case TAG_FLOAT:
		nref = mkfloat(m, loadfloat(oldm, ref));
		goto forw;
	case TAG_STRING:
	case TAG_SYMBOL:
		//fprintf(stderr, "gc: %s: %s\n", reftag(ref) == TAG_SYMBOL ? "symbol" : "string", (char *)pointer(oldm, ref));
		nref = mkstring(m, (char *)pointer(oldm, ref), reftag(ref));
	forw:
		vmstore(oldm, ref, 0, nref);
		setbit(isforw, urefval(ref));
		break;
	}
	if(reftag(ref) != TAG_CONS)
		for(;off < m->memlen; off++)
			setbit(isatom, off);
done:
	if(nref == NIL)
		fprintf(stderr, "vmcopy: returning NIL!\n");
	return nref;
}

void
vmgc(Mach *m)
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
	ref_t reg0tmp = vmcopy(m, isatom, isforw, &oldm, oldm.reg0);
	ref_t reg1tmp = vmcopy(m, isatom, isforw, &oldm, oldm.reg1);
	m->reg2 = vmcopy(m, isatom, isforw, &oldm, oldm.reg2);
	m->reg3 = vmcopy(m, isatom, isforw, &oldm, oldm.reg3);
	m->reg4 = vmcopy(m, isatom, isforw, &oldm, oldm.reg4);

	m->valu = vmcopy(m, isatom, isforw, &oldm, oldm.valu);
	m->expr = vmcopy(m, isatom, isforw, &oldm, oldm.expr);
	m->envr = vmcopy(m, isatom, isforw, &oldm, oldm.envr);
	m->stak = vmcopy(m, isatom, isforw, &oldm, oldm.stak);

	// cheney style breadth first scan, m->memlen effectively
	// acts as the "tail" while i is the "head", and pointers
	// between i and tail are yet to be converted (forwarded)
	for(i = 2; i < m->memlen; i++){
		if(getbit(isatom, i) != 0)
			continue;
		m->mem[i] = vmcopy(m, isatom, isforw, &oldm, m->mem[i]);
	}

	// re-copy the registers used by cons, vmcopy calls cons itself so
	// they get overwritten over and over again during gc.
	m->reg0 = reg0tmp;
	m->reg1 = reg1tmp;

	free(oldm.mem);
	free(isatom);
	free(isforw);

	m->gclock--;
}

int
main(int argc, char *argv[])
{
	Mach m;
	ref_t list;
	size_t i;

	memset(&m, 0, sizeof m);

	// install initial environment (define built-ins)
	m.envr = mkcons(&m, NIL, NIL);
	for(i = 0; i < NUM_BLT; i++){
		ref_t sym;
		sym = mkstring(&m, bltnames[i], TAG_SYMBOL);
		vmdefine(&m, sym, mkref(i, TAG_BUILTIN));
	}

	for(i = 1; i < (size_t)argc; i++){
		FILE *fp;
		fp = fopen(argv[i], "rb");
		if(fp == NULL){
			fprintf(stderr, "cannot open %s\n", argv[i]);
			return 1;
		}
		for(;;){
			m.expr = listparse(&m, fp, 1);
			if(iserror(&m, m.expr))
				break;
			vmcall(&m, INS_RETURN, INS_EVAL);
			while(vmstep(&m) == 1)
				;
			m.expr = NIL;
			m.valu = NIL;
		}

		fclose(fp);
		fprintf(stderr, "read %s\n", argv[i]);
	}

	for(;;){
		printf("> "); fflush(stdout);
		m.expr = listparse(&m, stdin, 1);
		if(iserror(&m, m.expr))
			break;
		vmcall(&m, INS_RETURN, INS_EVAL);
		while(vmstep(&m) == 1){
			fprintf(stderr, "call-external: ");
			m.valu = vmload(&m, m.expr, 1);
			fprintf(stderr, "\n");
		}
		if(iserror(&m, m.valu))
			break;
		m.valu = NIL;
		m.expr = NIL;
	}
	exit(1);
	return 0;
}