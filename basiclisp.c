
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>

#define NIL (ref_t)0
#define nelem(x) (sizeof(x)/sizeof(x[0]))

enum {
	CONS = 0,	// first so that NIL value of 0 is a cons.
	BIGINT,
	FLOAT,
	INTEGER,
	STRING,
	SYMBOL,
	ERROR,

	// states for vmstep()
	APPLY = 100,
	BETA,
	BETA1,
	CONTINUE,
	DEFINE1,
	RETURN,
	EVAL,
	IF1,
	LISTEVAL,
	LISTEVAL1,
};

typedef unsigned short ref_t;

typedef struct Mach Mach;
struct Mach {
	ref_t reg0; // temp for mkcons.
	ref_t reg1; // temp for mkcons.
	ref_t reg2;
	ref_t reg3;
	ref_t reg4;

	ref_t valu; // return value
	ref_t expr; // expression being evaluated
	ref_t envr; // current environment, a stack of a-lists.
	ref_t inst; // 'instruction', a state pointer.
	ref_t stak; // call stack

	// hardwired symbols for special forms.
	ref_t _if, beta, define, lambda, quote;

	// list manipulation
	ref_t cons, car, cdr;

	// arithmetic
	ref_t add, sub, mul, div, rem;

	// logic
	ref_t eq, null, less;

	// constants
	ref_t truth, untruth; // #t, #f

	// extension mechanism. this bails out from the interpreter,
	// allowing the caller to produce a new m->valu based on what
	// happens to be in m->expr, and then just call vmstep again.
	ref_t extcall;

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
};

static ref_t
mkref(int val, int tag)
{
	return (val << 3) | (tag & 7);
}

static int
reftag(ref_t ref)
{
	return ref & 7;
}

static ssize_t
refval(ref_t ref)
{
	return (ssize_t)ref >> 3;
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
	return tag != SYMBOL && tag != CONS;
}

static int
issymbol(Mach *m, ref_t a)
{
	return reftag(a) == SYMBOL;
}

static int
iscons(Mach *m, ref_t a)
{
	return reftag(a) == CONS && a != NIL;
}

static int
iserror(Mach *m, ref_t a)
{
	return reftag(a) == ERROR;
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
isnumchar(int c)
{
	switch(c){
	case '0': case '1': case '2': case '3': case '4':
	case '5': case '6': case '7': case '8': case '9':
	case '.': case '-': case '+':
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
lex(Mach *m, FILE *fp)
{
	int isinteger;
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
	case '.': case '-': case '+':
		isinteger = 0;
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
			if(!isnumchar(ch)){
				if(isbreak(ch))
					break;
				goto casesym;
			}
			if(ch == '.')
				isinteger = 0;
			tokappend(m, ch);
			ch = fgetc(fp);
		}
		if(ch != -1)
			ungetc(ch, fp);
		return isinteger ? INTEGER : FLOAT;

	// string constant, detect and skip escapes but don't interpret them
	case '"':
		ch = fgetc(fp);
		if(ch == '\n')
			m->lineno++;
		while(ch != -1 && ch != '"'){
			if(ch == '\\'){
				tokappend(m, ch);
				ch = fgetc(fp);
				if(ch == '\n')
					m->lineno++;
				if(ch == -1)
					continue;
			}
			tokappend(m, ch);
			ch = fgetc(fp);
			if(ch == '\n')
				m->lineno++;
		}
		return STRING;

	// symbol is any string of nonbreak characters not starting with a number
	default:
	casesym:
		while(ch != -1 && !isbreak(ch)){
			tokappend(m, ch);
			ch = fgetc(fp);
		}
		ungetc(ch, fp);
		return SYMBOL;
	}
	return -1;
}

static ref_t
allocate(Mach *m, size_t num, int tag)
{
	ref_t ref;
recheck:
	if((m->memcap - m->memlen) < num){
		m->memcap = (m->memcap == 0) ? 256 : 2*m->memcap;
		m->mem = realloc(m->mem, m->memcap * sizeof m->mem[0]);
		goto recheck;
	}
	if(m->memlen == 0){
		m->memlen += 2;
		goto recheck;
	}
	ref = mkref(m->memlen, tag);
	if(urefval(ref) != m->memlen || reftag(ref) != tag){
		fprintf(stderr, "out of bits in ref: want %zx.%x got %zx.%x\n",
			m->memlen, tag, urefval(ref), reftag(ref));
		abort();
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

	ref = mkref(v, INTEGER);
	if((long long)refval(ref) == v)
		return ref;

	return mkany(m, &v, sizeof v, BIGINT);
}

static ref_t
mkfloat(Mach *m, double v)
{
	return mkany(m, &v, sizeof v, FLOAT);
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
	ref = allocate(m, 2, CONS);
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

	while((ltok = lex(m, fp)) != -1){
		tokappend(m, '\0');
		switch(ltok){
		default:
			fprintf(stderr, "unknown token %d: '%c' '%s'\n", ltok, ltok, m->tok);
			break;
		case ')':
			return list;
		case '(':
			nval = listparse(m, fp, 0);
			goto append;
		case '\'':
			nval = listparse(m, fp, 1);
			nval = mkcons(m, m->quote, mkcons(m, nval, NIL));
			goto append;
		case '.':
			dot++;
			break;
		case ',':
			fprintf(stderr, "TODO: backquote not implemented yet\n");
			break;
		case INTEGER:
			nval = mkint(m, strtoll(m->tok, NULL, 0));
			goto append;
		case FLOAT:
			nval = mkfloat(m, strtod(m->tok, NULL));
			goto append;
		case STRING:
			nval = mkstring(m, m->tok, STRING);
			goto append;
		case SYMBOL:
			nval = mkstring(m, m->tok, SYMBOL);
		append:
			if(justone)
				return nval;
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
				return list;
			}
			break;
		}
	}
	if(ltok == -1)
		return ERROR;
	// should return error I think, but it's perhaps convenient
	// to treat EOF as end-of-list.
	return list;
}

static long long
loadint(Mach *m, ref_t ref)
{
	int tag = reftag(ref);
	if(tag == INTEGER)
		return (long long)refval(ref);
	if(tag == BIGINT)
		return *(long long *)pointer(m, ref);
	fprintf(stderr, "loadint: non-integer reference\n");
	return 0;
}

static double
loadfloat(Mach *m, ref_t ref)
{
	if(reftag(ref) == FLOAT)
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
	case ERROR:
		fprintf(fp, "error %zx\n", refval(aref));
		break;
	case CONS:
		if(aref == NIL)
			fprintf(fp, "nil");
		else
			fprintf(fp, "cons(#x%x)", aref);
		break;
	case INTEGER:
		fprintf(fp, "%zd", refval(aref));
		break;
	case BIGINT:
		fprintf(fp, "%lld", *(long long *)pointer(m, aref));
		break;
	case FLOAT:
		fprintf(fp, "%f", *(double *)pointer(m, aref));
		break;
	case STRING:
		fprintf(fp, "\"%s\"", (char *)pointer(m, aref));
		break;
	case SYMBOL:
		fprintf(fp, "%s", (char *)pointer(m, aref));
		break;
	}
	return tag;
}

// (print ...)
static void
eval_print(Mach *m)
{
	FILE *fp = stderr;
	int first = 1;
	ref_t *stak = &m->reg2;
	ref_t *list = &m->reg3;
	ref_t *valu = &m->reg4;

	// skip over the 'print symbol in head position.
	// *valu = vmload(m, m->expr, 1);
	*valu = m->expr;
	*stak = NIL;
	*list = NIL;
	for(;;){
		if(iscons(m, *valu)){
			fprintf(fp, "(");
			*stak = mkcons(m, *list, *stak);
			*list = *valu;
			*valu = vmload(m, *list, 0);
			first = 1;
		} else {
			if(!first)
				fprintf(fp, " ");
			atomprint(m, *valu, fp);
			first = 0;
unwind:
			while(*list == NIL){
				if(*stak == NIL)
					goto done;
				fprintf(fp, ")");
				*list = vmload(m, *stak, 0);
				*stak = vmload(m, *stak, 1);
			}
			*list = vmload(m, *list, 1);
			if(*list == NIL)
				goto unwind;
			if(!iscons(m, *list)){
				fprintf(fp, " .");
				*valu = *list;
				*list = NIL;
			} else {
				*valu = vmload(m, *list, 0);
			}
		}
	}
done:
	*stak = NIL;
	*list = NIL;
	*valu = NIL;
}

static void
vmgoto(Mach *m, ref_t inst)
{
	m->inst = inst;
}

static void
vmcall(Mach *m, ref_t ret, ref_t inst)
{
	m->stak = mkcons(m, ret, m->stak);
	vmgoto(m, inst);
}

static void
vmreturn(Mach *m)
{
	ref_t inst;
	inst = vmload(m, m->stak, 0);
	m->stak = vmload(m, m->stak, 1);
	vmgoto(m, inst);
}

int
vmstep(Mach *m)
{
	size_t i;
again:
	switch(m->inst){
	default:
		fprintf(stderr, "vmstep: invalid instruction %d, bailing out.\n", m->inst);
	case CONTINUE:
		vmreturn(m);
		goto again;
	case RETURN:
		return 0;
	case EVAL:
		if(isatom(m, m->expr)){
			m->valu = m->expr;
			vmreturn(m);
			goto again;
		} else if(issymbol(m, m->expr)){
			ref_t lst = m->envr;
			while(lst != NIL){
				ref_t pair = vmload(m, lst, 0);
				if(iscons(m, pair)){
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
		} else if(iscons(m, m->expr)){
			// form is (something), so at least we are applying something.
			// handle special forms (quote, lambda, beta, if, define) before
			// evaluating args.
			ref_t head = vmload(m, m->expr, 0);

			if(head == m->_if){
				// (if cond then else)
				m->expr = vmload(m, m->expr, 1);
				m->stak = mkcons(m, m->expr, m->stak);
				// evaluate condition recursively
				m->expr = vmload(m, m->expr, 0);
				vmcall(m, IF1, EVAL);
				goto again;
	case IF1:
				// evaluate result as a tail-call, if condition
				// evaluated to #f, skip over 'then' to 'else'.
				m->expr = vmload(m, m->stak, 0);
				m->stak = vmload(m, m->stak, 1);
				m->expr = vmload(m, m->expr, 1);
				if(m->valu == m->untruth)
					m->expr = vmload(m, m->expr, 1);
				m->expr = vmload(m, m->expr, 0);
				vmgoto(m, EVAL);
				goto again;
			}
			if(head == m->beta){
				// beta literal: return self.
				m->valu = m->expr;
				vmreturn(m);
				goto again;
			}
			if(head == m->define){
				// (define sym val) -> args,
				// current environment gets sym associated with val.
				ref_t *sym = &m->reg2;
				ref_t *val = &m->reg3;
				ref_t *env = &m->reg3; // used after vmcall..
				*sym = vmload(m, m->expr, 1);
				*val = vmload(m, *sym, 1);
				*sym = vmload(m, *sym, 0);
				if(iscons(m, *sym)){
					// scheme shorthand: (define (name args...) body1 body2...).
					*val = mkcons(m, m->lambda, mkcons(m, vmload(m, *sym, 1), *val));
					*sym = vmload(m, *sym, 0);
				} else {
					*val = vmload(m, *val, 0);
				}
				m->stak = mkcons(m, *sym, m->stak);
				m->expr = *val;

				vmcall(m, DEFINE1, EVAL);
				goto again;
	case DEFINE1:
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

				m->valu = NIL;
				vmreturn(m);
				goto again;
			}
			if(head == m->lambda){
				// (lambda args body) -> (beta (lambda args body) envr)
				m->valu = mkcons(m, m->beta, mkcons(m, m->expr, m->envr));
				vmreturn(m);
				goto again;
			}
			if(head == m->quote){
				// (quote args) -> args
				m->valu = vmload(m, vmload(m, m->expr, 1), 0); // expr = cdar expr
				vmreturn(m);
				goto again;
			}

			// at this point we know it is a list, and that it is
			// not a special form. evaluate args, then apply.
			vmcall(m, APPLY, LISTEVAL);
			goto again;
	case APPLY:
			m->expr = m->valu;
			head = vmload(m, m->expr, 0);
			if(head == m->add || head == m->sub || head == m->mul || head == m->div || head == m->rem){
				ref_t ref0, ref;
				long long ires;
				double fres;
				m->expr = vmload(m, m->expr, 1);
				ref0 = vmload(m, m->expr, 0);
				if(reftag(ref0) == INTEGER || reftag(ref0) == BIGINT){
					ires = loadint(m, ref0);
				} else if(reftag(ref0) == FLOAT){
					fres = loadfloat(m, ref0);
				} else {
					m->valu = ERROR;
					vmreturn(m);
					goto again;
				}
				m->expr = vmload(m, m->expr, 1);
				while(m->expr != NIL){
					ref = vmload(m, m->expr, 0);
					if(reftag(ref0) != reftag(ref)){
						m->valu = ERROR;
						vmreturn(m);
						goto again;
					}
					if(reftag(ref0) == FLOAT){
						double tmp = loadfloat(m, ref);
						if(head == m->add)
							fres += tmp;
						else if(head == m->sub)
							fres -= tmp;
						else if(head == m->mul)
							fres *= tmp;
						else if(head == m->div)
							fres /= tmp;
						else if(head == m->rem)
							fres = fmod(fres, tmp);
					} else {
						long long tmp = loadint(m, ref);
						if(head == m->add)
							ires += tmp;
						else if(head == m->sub)
							ires -= tmp;
						else if(head == m->mul)
							ires *= tmp;
						else if(head == m->div)
							ires /= tmp;
						else if(head == m->rem)
							ires %= tmp;
					}
					m->expr = vmload(m, m->expr, 1);
				}
				if(reftag(ref0) == FLOAT){
					m->valu = mkfloat(m, fres);
				} else {
					m->valu = mkint(m, ires);
				}
				vmreturn(m);
				goto again;
			} else if(head == m->null){ // (null? ...)
				ref_t ref0;
				m->expr = vmload(m, m->expr, 1);
				ref0 = vmload(m, m->expr, 0);
				if(reftag(ref0) == CONS){
					if(ref0 == NIL)
						m->valu = m->truth;
					else
						m->valu = m->untruth;
				} else {
					m->valu = m->untruth;
				}
				vmreturn(m);
				goto again;
			} else if(head == m->eq){ // (eq? ...)
				ref_t ref0;
				m->expr = vmload(m, m->expr, 1);
				ref0 = vmload(m, m->expr, 0);
				m->expr = vmload(m, m->expr, 1);
				while(m->expr != NIL){
					ref_t ref = vmload(m, m->expr, 0);
					if(reftag(ref0) != reftag(ref)){
						m->valu = m->untruth;
						goto eqdone;
					}
					if(reftag(ref0) == FLOAT){
						double v0, v;
						v0 = loadfloat(m, ref0);
						v = loadfloat(m, ref);
						if(v0 != v){
							m->valu = m->untruth;
							goto eqdone;
						}
					} else if(reftag(ref0) == INTEGER || reftag(ref0) == BIGINT){
						long long v0, v;
						v0 = loadint(m, ref0);
						v = loadint(m, ref);
						if(v0 != v){
							m->valu = m->untruth;
							goto eqdone;
						}
					}
					if(refval(ref0) != refval(ref)){
						m->valu = m->untruth;
						goto eqdone;
					}
					m->expr = vmload(m, m->expr, 1);
				}
				m->valu = m->truth;
			eqdone:
				vmreturn(m);
				goto again;
			} else if(head == m->less){
				ref_t ref0, ref1;
				m->expr = vmload(m, m->expr, 1);
				ref0 = vmload(m, m->expr, 0);
				m->expr = vmload(m, m->expr, 1);
				ref1 = vmload(m, m->expr, 0);
				m->valu = m->untruth; // default to false.
				if((reftag(ref0) == INTEGER || reftag(ref0) == BIGINT)
				&& (reftag(ref1) == INTEGER || reftag(ref1) == BIGINT)){
					long long i0, i1;
					i0 = loadint(m, ref0);
					i1 = loadint(m, ref1);
					if(i0 < i1)
						m->valu = m->truth;
				} else if(reftag(ref0) == FLOAT && reftag(ref1) == FLOAT){
					double f0, f1;
					f0 = loadfloat(m, ref0);
					f1 = loadfloat(m, ref1);
					if(f0 < f1)
						m->valu = m->truth;
				} else if((reftag(ref0) == SYMBOL && reftag(ref1) == SYMBOL)
					|| (reftag(ref0) == STRING && reftag(ref1) == STRING)){
					if(strcmp((char*)pointer(m, ref0), (char*)pointer(m, ref1)) < 0)
						m->valu = m->truth;
				} else if(reftag(ref0) < reftag(ref1)){
					m->valu = m->truth;
				}
				vmreturn(m);
				goto again;
			} else if(head == m->car){
				m->reg2 = vmload(m, m->expr, 1);
				m->reg2 = vmload(m, m->reg2, 0);
				m->valu = vmload(m, m->reg2, 0);
				vmreturn(m);
				goto again;
			} else if(head == m->cdr){
				m->reg2 = vmload(m, m->expr, 1);
				m->reg2 = vmload(m, m->reg2, 0);
				m->valu = vmload(m, m->reg2, 1);
				vmreturn(m);
				goto again;
			} else if(head == m->cons){
				m->reg2 = vmload(m, m->expr, 1);
				m->reg3 = vmload(m, m->reg2, 1);
				m->reg2 = vmload(m, m->reg2, 0);
				m->reg3 = vmload(m, m->reg3, 0);
				m->valu = mkcons(m, m->reg2, m->reg3);
				vmreturn(m);
				goto again;
			} else if(head == m->extcall){
				m->inst = CONTINUE;
				return 1;
			} else if(issymbol(m, head)){
				fprintf(stderr, "apply: head is a symbol: %s\n", (char *)pointer(m, head));
				vmreturn(m);
				goto again;
			} else if(iscons(m, head)){
	case BETA:
				// save old environment to stack.
				// TODO: unless we were called from a tail position
				m->stak = mkcons(m, m->envr, m->stak);

				// form is ((beta (lambda...)) args)
				// ((beta (lambda args . body) . envr) args) -> (body),
				// with a new environment

				ref_t beta, lambda;
				ref_t *argnames = &m->reg2;
				ref_t *args = &m->reg3;

				beta = vmload(m, m->expr, 0);
				lambda = vmload(m, vmload(m, beta, 1), 0);
				m->envr = vmload(m, vmload(m, beta, 1), 1);

				*argnames = vmload(m, lambda, 1);
				*args = vmload(m, m->expr, 1); // args = cdr expr
				if(iscons(m, *argnames)){
					// loop over argnames and args simultaneously, cons
					// them as pairs to the environment
					*argnames = vmload(m, *argnames, 0);
					while(iscons(m, *argnames) && iscons(m, *args)){
						ref_t pair = mkcons(m,
							vmload(m, *argnames, 0),
							vmload(m, *args, 0));
						m->envr = mkcons(m, pair, m->envr);
						*argnames = vmload(m, *argnames, 1);
						*args = vmload(m, *args, 1);
					}
				}

				// scheme-style variadic: argnames list terminates in a
				// symbol instead of nil, associate the rest of argslist
				// with it. notice: (lambda x (body)) also lands here.
				if(*argnames != NIL){
					ref_t pair = mkcons(m, *argnames, *args);
					m->envr = mkcons(m, pair, m->envr);
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
	case BETA1:
				m->reg2 = vmload(m, m->stak, 0);
				if(m->reg2 != NIL){
					m->expr = vmload(m, m->reg2, 0);
					m->reg2 = vmload(m, m->reg2, 1);
					vmstore(m, m->stak, 0, m->reg2);
					// if reg2 == nil, this is a tail position.
					vmcall(m, BETA1, EVAL);
					goto again;
				}
				m->stak = vmload(m, m->stak, 1); // pop expr (body-list).
				m->envr = vmload(m, m->stak, 0); // restore environment
				m->stak = vmload(m, m->stak, 1); // pop environment
				vmreturn(m);
				goto again;
			} else {
				fprintf(stderr, "apply: head not symbol: ");
				eval_print(m);
				fprintf(stderr, "\n");
				vmreturn(m);
				goto again;
			}
		}
		fprintf(stderr, "vmstep eval: unrecognized form\n");
		m->valu = ERROR;
		vmreturn(m);
		goto again;
	case LISTEVAL:
		m->reg2 = mkcons(m, m->expr, NIL);
		m->reg3 = m->expr;
		m->stak = mkcons(m, m->reg2, m->stak);
		goto listeval_first;
	case LISTEVAL1:
		m->reg2 = vmload(m, m->stak, 0);
		m->reg3 = vmload(m, m->reg2, 1);
		m->reg3 = mkcons(m, m->valu, m->reg3);
		vmstore(m, m->reg2, 1, m->reg3);
		m->reg3 = vmload(m, m->reg2, 0);
listeval_first:
		if(m->reg3 != NIL){
			m->expr = vmload(m, m->reg3, 0);
			m->reg3 = vmload(m, m->reg3, 1);
			vmstore(m, m->reg2, 0, m->reg3);
			vmcall(m, LISTEVAL1, EVAL);
			goto again;
		}
		m->valu = vmload(m, m->reg2, 1);

		// all done, reverse the value list.
		m->reg3 = NIL;
		while((m->reg2 = vmload(m, m->valu, 1)) != NIL){
			vmstore(m, m->valu, 1, m->reg3);
			m->reg3 = m->valu;
			m->valu = m->reg2;
		}
		vmstore(m, m->valu, 1, m->reg3);

		m->reg2 = NIL;
		m->reg3 = NIL;
		m->stak = vmload(m, m->stak, 1);
		vmreturn(m);
		goto again;
	}
}

int
main(void)
{
	Mach m;
	ref_t list;
	size_t i;

	memset(&m, 0, sizeof m);

	// install empty environment.
	m.envr = mkcons(&m, NIL, NIL);

	// register built-in symbols.
	m._if = mkstring(&m, "if", SYMBOL);
	m.beta = mkstring(&m, "beta", SYMBOL);
	m.define = mkstring(&m, "define", SYMBOL);
	m.lambda = mkstring(&m, "lambda", SYMBOL);
	m.quote = mkstring(&m, "quote", SYMBOL);

	m.extcall = mkstring(&m, "extcall", SYMBOL);

	m.cons = mkstring(&m, "cons", SYMBOL);
	m.car = mkstring(&m, "car", SYMBOL);
	m.cdr = mkstring(&m, "cdr", SYMBOL);
	m.add = mkstring(&m, "+", SYMBOL);
	m.sub = mkstring(&m, "-", SYMBOL);
	m.mul = mkstring(&m, "*", SYMBOL);
	m.div = mkstring(&m, "/", SYMBOL);
	m.rem = mkstring(&m, "%", SYMBOL);

	m.eq = mkstring(&m, "eq?", SYMBOL);
	m.null = mkstring(&m, "null?", SYMBOL);
	m.less = mkstring(&m, "<", SYMBOL);

	m.truth = mkstring(&m, "#t", SYMBOL);
	m.untruth = mkstring(&m, "#f", SYMBOL);

	for(;;){
		printf("> "); fflush(stdout);
		m.expr = listparse(&m, stdin, 1);
		if(iserror(&m, m.expr))
			break;
		vmcall(&m, RETURN, EVAL);
		while(vmstep(&m) == 1){
			fprintf(stderr, "extcall: ");
			eval_print(&m);
			m.valu = vmload(&m, m.expr, 1);
			fprintf(stderr, "\n");
		}
		m.expr = m.valu;
		eval_print(&m);
		printf("\n");
		if(iserror(&m, m.valu))
			break;
	}
	exit(1);
	return 0;
}