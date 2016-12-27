
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
	BUILTIN,
	BIGINT,
	CONS,
	FLOAT,
	INTEGER,
	STRING,
	SYMBOL,
	VECTOR,
};

typedef unsigned int ref_t;

typedef struct Mach Mach;
struct Mach {

	
	ref_t stack; // "current continuation"
	ref_t value; // return value, of course temporarily a temp,

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

static ref_t NIL = 0;

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
mksymbol(Mach *m, char *str)
{
	uint32_t hash, hash2;
	ref_t ref;
	hash = fnv32a(str, 0);
	if((ref = idxlookup(m, hash, str)) == NIL){
		ref = mkany(m, str, strlen(str)+1, SYMBOL);
		idxinsert(m, hash, ref);
	}
	return ref;
}

static ref_t
mkstring(Mach *m, char *str)
{
	uint32_t hash;
	ref_t ref;

	hash = fnv32a(str, 0);
	if((ref = idxlookup(m, hash, str)) == NIL){
		ref = mkany(m, str, strlen(str)+1, STRING);
		idxinsert(m, hash, ref);
	}
	return ref;
}

static ref_t
mkcons(Mach *m, ref_t a, ref_t d)
{
	ref_t ref;

	ref = allocate(m, 2, CONS);
	store(m, ref, 0, a);
	store(m, ref, 1, d);
	return ref;
}
/*
 *	       .---.    .----.
 *	ref -> |car| -> |caar|
 *	       +---+    +----+
 *	       |cdr| -. |cadr|
 *	       '---'  | '----'
 *	              |   .----.
 *	              '-> |cdar|
 *	                  +----+
 *	                  |cddr|
 *                        '----'
 */

/*
 *	the calling convention on our lisp machine is a singly-linked
 *	cons-chain of stack frames. a stack frame is a tuple, with the
 *	situation looking as follows
 *
 *	Mach
 *	.-------.     .------.     .------.
 *	| stack | --> | next | --> | next | --> ...
 *	'-------'     +------+     +------+     .---.     .---.
 *	              | head | -.  | head | --> |car| --> | + |
 *	              '------'  |  '------'     +---+     '---'
 *	                        |               |cdr| --.  .---.
 *                              |               '---'   '->|car| -> number0
 *	                        '-> .---.     .--------.   +---+
 *	                            |car| --> | lambda |   |cdr| -> next-cons
 *	                            +---+     '--------'   '---'
 *	                            |cdr| --> .---.
 *	                            '---'     |car| -> args-list
 *	                                      +---+    .---.
 *	                                      |cdr| -> |car| -> body-list
 *	                                      '---'    '---'
 *
 *	Ie. the head field references the list under evaluation, with
 *	car(head) being the instruction (ie. builtin-lambda).
 */
static ref_t
listparse(Mach *m, FILE *fp)
{
	ref_t list = NIL, prev = NIL, cons, nval;
	int ltok;

	while((ltok = lex(m, fp)) != -1){
		printf(" ");
		tokappend(m, '\0');
		switch(ltok){
		default:
			printf("unknown token %d: '%c' '%s'\n", ltok, ltok, m->tok);
			break;
		case ')':
			return list;
		case '(':
			nval = listparse(m, fp);
			goto append;
		case '.': case '\'': case ',':
			printf("self(%c)", ltok);
			break;

		case INTEGER:
			nval = mkint(m, m->tok);
			goto append;
		case FLOAT:
			nval = mkfloat(m, m->tok);
			goto append;
		case STRING:
			nval = mkstring(m, m->tok);
			goto append;
		case SYMBOL:
			nval = mksymbol(m, m->tok);
		append:
			cons = mkcons(m, nval, NIL);
			if(prev != NIL)
				store(m, prev, 1, cons);
			else 
				list = cons;
			prev = cons;
			break;
		}
	}
	// should return error I think, but it's perhaps convenient
	// to treat EOF as end-of-list.
	return list;
}

// this is heading in the wrong direction already, I think the builtin
// should be 'print1' and a library function should do the recursion.
static int
listprint(Mach *m, FILE *fp)
{

	ref_t cons, aref;

	if(m->stack == NIL){
		fprintf(stderr, "listprint1: called with nil stack\n");
		abort();
	}

	// print list head.
	cons = load(m, m->stack, 0);
	if(cons != NIL){
		aref = load(m, cons, 0);
		switch(reftag(aref)){
		default:
			fprintf(stderr, "listprint: unknown a: val:%zx tag:%x\n", refval(aref), reftag(aref));
			break;
		case BUILTIN:
			switch(refval(aref)){
			default:
				fprintf(stderr, "listprint: unknown builtin %zd\n", refval(aref));
				break;
			case 0:
				fprintf(fp, "nil");
				break;
			}
			break;
		case CONS:
			printf("(");
			// push new cons to stack, return.
			m->stack = mkcons(m, aref, m->stack);
			return -1;
		case INTEGER:
			fprintf(fp, "%zd\n", refval(aref));
			break;
		case BIGINT:
			fprintf(fp, "%lld\n", *(long long *)pointer(m, aref));
			break;
		case FLOAT:
			fprintf(fp, "%f\n", *(double *)pointer(m, aref));
			break;
		case STRING:
			fprintf(fp, "\"%s\"\n", (char *)pointer(m, aref));
			break;
		case SYMBOL:
			fprintf(fp, "%s\n", (char *)pointer(m, aref));
			break;
		}
	}
	// current list ran out, pop the stack until we find a cons that hasn't
	while(cons == NIL){
		printf(")\n");
		// if stack is empty, we're done.
		// pop the stack (current cons), load cons from it.
		m->stack = load(m, m->stack, 1);
		if(m->stack == NIL)
			return 0;
		cons = load(m, m->stack, 0);
	}

	// advance current cons and update it in the stack
	// before returning.
	cons = load(m, cons, 1);
	store(m, m->stack, 0, cons);
	return -1;
}

int
main(void)
{
	Mach m;
	ref_t list;

	memset(&m, 0, sizeof m);
	printf("sizeof(ref_t) = %zd\n", sizeof(ref_t));
	list = listparse(&m, stdin);

	printf("mach: memlen %zd memcap %zd idxlen %zd idxcap %zd\n", m.memlen, m.memcap, m.idxlen, m.idxcap);

	size_t i;
	for(i = 0; i < m.idxcap; i++){
		ref_t ref = m.idx[i];
		if(ref != NIL)
			printf("%zd: %zd.%d: '%s'\n", i, refval(ref), reftag(ref), (char *)pointer(&m, ref));
	}

	m.stack = mkcons(&m, list, NIL);
	while(listprint(&m, stdout) == -1)
		;

	return 0;
}