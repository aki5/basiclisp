/*
 *	Basiclisp is a very small lisp interpreter, intended for non-intrusive
 *	embedding in larger programs that could use a small scripting
 *	language.
 *
 *	The code intends to be easy to understand but to the the point, so
 *	we'll start with a short description of what a lisp interpreter does.
 *
 *	Lisp evaluation:
 *
 *		1. takes a list as input,
 *		2. recursively evaluates all sublists,
 *		3. evaluates the list itself by applying the built-in
 *		   indicated by the head element.
 *
 *	Lambda is the new function factory of lisp. It's so named to refer
 *	the Church lambda calculus, but calling it func or function might
 *	be more proper.
 *
 *		(lambda (arg-name1 arg-name2 ..) (body1) (body2) ...)
 *
 *	Ie. it takes a parameter list a function-body as arguments. The
 *	function-body may have references to symbols other than those listed
 *	as arguments, in which case they refer to the variables as they were
 *	at definition time.
 *
 *	Evaluation of a lambda-expr '(lambda (args) (body1) (body2) ...)' takes
 *	place as follows
 *
 *		1. find the current environment (envr)
 *		2. construct a beta-expr '(beta lambda-expr . envr)'
 *		3. return 2 as the evaluated result.
 *
 *	So, a lambda will evaluate into a beta that still encloses the original
 *	lambda expression, like this
 *
 *		((beta (lambda (arg-names) (body1) ...) . envr) arg-values)'
 *
 *	So, when a function which has been defined as a lambda gets called,
 *	it is beta that does the work, and beta in our case looks as follows
 *
 *		1. create a new environment, chain it to 'envr'
 *		   from the list and install it as the evaluation environment. 
 *		2. pair arg-values with arg-names and define them in the new
 *		   envr
 *		3. evaluate each body1 body2 ... in sequence
 *		4. take the value of the last body as the result.
 *
 *	In short, lambda-evaluation is just about binding the environment
 *	as it existed at definition-time, and beta-evaluation is just about
 *	binding function arguments to a new environment and popping both the
 *	lambda and the beta off the list head.
 *
 *	Notice that none of this really needs a stack. Reading through the
 *	source code, you'll surely find that most of the code is in fact
 *	dealing with memory allocation, symbol string deduplication and parsing
 *	of s-expressions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NIL (ref_t)0
#define nelem(x) (sizeof(x)/sizeof(x[0]))

enum {
	CONS,	// first so that NIL value of 0 is a cons.
	BIGINT,
	FLOAT,
	INTEGER,
	STRING,
	SYMBOL,
	ERROR,
	BUILTIN,
	//??
	//??
};

typedef unsigned int ref_t;

typedef struct Mach Mach;
struct Mach {
	ref_t reg0; 
	ref_t reg1;
	ref_t reg2; 
	ref_t reg3;
	ref_t reg4;
	ref_t expr;
	ref_t envr; // current environment, a stack of a-lists.

	// the following are our built-ins.
	ref_t and;
	ref_t atom; // atom?
	ref_t beta;
	ref_t car;
	ref_t cdr;
	ref_t cons;
	ref_t eq; // eq?
	ref_t _if;
	ref_t lambda;
	ref_t print;
	ref_t quote;
	ref_t set;

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
load(Mach *m, ref_t base, size_t offset)
{
	ref_t *p = pointer(m, base);
	return p[offset];
}

static ref_t
store(Mach *m, ref_t base, size_t offset, ref_t obj)
{
	ref_t *p = pointer(m, base);
	p[offset] = obj;
	return obj;
}

static int
iscons(Mach *m, ref_t a)
{
	return reftag(a) == CONS && a != NIL;
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
mkint(Mach *m, char *str)
{
	long long v;
	ref_t ref;

	v = strtoll(str, NULL, 0);
	ref = mkref(v, INTEGER);
	if((long long)refval(ref) == v)
		return ref;

	return mkany(m, &v, sizeof v, BIGINT);
}

static ref_t
mkfloat(Mach *m, char *str)
{
	double v;

	v = strtod(str, NULL);
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
	store(m, ref, 0, m->reg0);
	store(m, ref, 1, m->reg1);
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
			//printf("self(%c)", ltok);
			break;
		case INTEGER:
			nval = mkint(m, m->tok);
			goto append;
		case FLOAT:
			nval = mkfloat(m, m->tok);
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
					store(m, prev, 1, cons);
				else 
					list = cons;
				prev = cons;
			} else if(dot == 1){
				store(m, prev, 1, nval);
				dot++;
			} else {
				fprintf(stderr, "malformed s-expression, bad use of '.'\n");
				return list;
			}
			break;
		}
	}
	// should return error I think, but it's perhaps convenient
	// to treat EOF as end-of-list.
	return list;
}


static int
atomprint(Mach *m, ref_t aref, FILE *fp)
{
	int tag = reftag(aref);
	switch(tag){
	default:
		fprintf(stderr, "listprint: unknown atom: val:%zx tag:%x\n", refval(aref), reftag(aref));
		break;
	case CONS:
		if(aref == NIL)
			printf("nil");
		else
			printf("cons(#x%x)", aref);
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
	case BUILTIN:
	case SYMBOL:
		fprintf(fp, "%s", (char *)pointer(m, aref));
		break;
	}
	return tag;
}

// (print ...)
static void
eval_print(Mach *m, FILE *fp)
{
	int isfirst = 1;
	ref_t *stak = &m->reg2;
	ref_t *list = &m->reg3;
	ref_t *valu = &m->reg4;

	// skip over the 'print symbol in head position.
	// *valu = load(m, m->expr, 1);
	*valu = m->expr;
	*stak = NIL;
	*list = NIL;
	for(;;){
		if(iscons(m, *valu)){
			printf("(");
			*stak = mkcons(m, *list, *stak);
			*list = *valu;
			*valu = load(m, *list, 0);
			isfirst = 1;
		} else {
			if(!isfirst)
				printf(" ");
			atomprint(m, *valu, fp);
			isfirst = 0;
unwind:
			while(*list == NIL){
				if(*stak == NIL)
					goto done;
				printf(")");
				*list = load(m, *stak, 0);
				*stak = load(m, *stak, 1);
			}
			*list = load(m, *list, 1);
			if(*list == NIL)
				goto unwind;
			if(!iscons(m, *list)){
				printf(" .");
				*valu = *list;
				*list = NIL;
			} else {
				*valu = load(m, *list, 0);
			}
		}
	}
done:
	*stak = NIL;
	*list = NIL;
	*valu = NIL;
}

// case (car expr) == quote
// (quote args) -> args
void
eval_quote(Mach *m)
{
	m->expr = load(m, load(m, m->expr, 1), 0); // expr = cdar expr
}

// case (car expr) == lambda
// (lambda args body) -> (beta (lambda args body) envr)
void
eval_lambda(Mach *m)
{
	m->expr = mkcons(m, m->beta, mkcons(m, m->expr, m->envr));
}

// case (caar expr) == beta
// ((beta (lambda args . body) . envr) args) -> (body), with a new environment
void
eval_beta(Mach *m)
{
	ref_t beta, lambda;

	beta = load(m, m->expr, 0); // beta = car expr
	lambda = load(m, load(m, beta, 1), 0); // lambda = cdar beta
	m->envr = load(m, load(m, beta, 1), 1); // env = cddr beta

	// loop over argnames and args simultaneously, cons them as pairs
	// to the environment
	m->reg2 = load(m, load(m, lambda, 1), 0); // argnames = cdar lambda
	m->reg3 = load(m, m->expr, 1); // args = cdr expr
	while(iscons(m, m->reg2) && iscons(m, m->reg3)){
		ref_t pair = mkcons(m, load(m, m->reg2, 0), load(m, m->reg3, 0));
		m->envr = mkcons(m, pair, m->envr);
		m->reg2 = load(m, m->reg2, 1);
		m->reg3 = load(m, m->reg3, 1);
	}

	// scheme-style variadic: argnames list terminates in a symbol instead
	// of nil, associate the rest of args-list with it.
	if(m->reg2 != NIL){
		ref_t pair = mkcons(m, m->reg2, m->reg3);
		m->envr = mkcons(m, pair, m->envr);
	}
	// remember to clear the temps, so that gc can free them.
	m->reg2 = NIL;
	m->reg3 = NIL;

	// parameters are bound, pull body from lambda to m->expr. 
	beta = load(m, m->expr, 0); // beta = car expr
	lambda = load(m, load(m, beta, 1), 0); // lambda = cdar beta
	m->expr = load(m, load(m, lambda, 1), 1); // body = cddr lambda
}


int
main(void)
{
	Mach m;
	ref_t list;
	size_t i;

	memset(&m, 0, sizeof m);
	m._if = mkstring(&m, "if", BUILTIN);
	m.beta = mkstring(&m, "beta", BUILTIN);
	m.eq = mkstring(&m, "eq?", BUILTIN);
	m.lambda = mkstring(&m, "lambda", BUILTIN);
	m.print = mkstring(&m, "print", BUILTIN);
	m.quote = mkstring(&m, "quote", BUILTIN);
	m.set = mkstring(&m, "set!", BUILTIN);

	if(isatty(0)){
		for(;;){
			printf("> "); fflush(stdout);
			m.expr = listparse(&m, stdin, 1);
			ref_t head = load(&m, m.expr, 0);
			if(head == m.lambda){
				eval_lambda(&m);
			} else if(iscons(&m, head) && load(&m, head, 0) == m.beta){
				eval_beta(&m);
			}
			eval_print(&m, stdout);
			printf("\n");
		}
		exit(1);
	}
	list = listparse(&m, stdin, 0);

	printf("mach: memlen %zd memcap %zd idxlen %zd idxcap %zd\n", m.memlen, m.memcap, m.idxlen, m.idxcap);

	for(i = 0; i < m.idxcap; i++){
		ref_t ref = m.idx[i];
		if(ref != NIL)
			printf("%zd: %zd.%d: '%s'\n", i, refval(ref), reftag(ref), (char *)pointer(&m, ref));
	}

	m.expr = list;
	eval_print(&m, stdout);

	printf("mach: memlen %zd memcap %zd idxlen %zd idxcap %zd\n", m.memlen, m.memcap, m.idxlen, m.idxcap);

	return 0;
}