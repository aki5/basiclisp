
typedef unsigned short lispref_t;
typedef unsigned int lispport_t;
typedef struct Mach Mach;
typedef lispref_t (lispapplier_t)(void *, void *, lispref_t);
typedef lispref_t (lispgetter_t)(void *, void *, lispref_t);
typedef lispref_t (lispsetter_t)(void *, void *, lispref_t, lispref_t);

// these are macros because enums are signed and these fiddle with the MSB.
#define LISP_TAG_BIT ((lispref_t)1<<(8*sizeof(lispref_t)-1))
#define	LISP_VAL_MASK ((lispref_t)LISP_TAG_BIT-1)
#define LISP_NIL (lispref_t)(LISP_TAG_BIT>>LISP_TAG_PAIR)

enum {
	LISP_CAR_OFFSET = 0,
	LISP_CDR_OFFSET = 1,

	LISP_TOK_INTEGER = 1000,
	LISP_TOK_SYMBOL,
	LISP_TOK_STRING,

	LISP_TAG_PAIR = 0,	// 1...... 32k cells (pairs)
	LISP_TAG_INTEGER,	// 01..... 16k unsigned ints
	LISP_TAG_SYMBOL,	// 001....  8k symbols (offset to the name table)
	LISP_TAG_EXTREF,	// 0001...  4k external objects
	LISP_TAG_BUILTIN,	// 00001..  2k built-in functions (enumerated below)

	LISP_BUILTIN_IF = 0,
	LISP_BUILTIN_BETA,
	LISP_BUILTIN_CONTINUE,
	LISP_BUILTIN_DEFINE,
	LISP_BUILTIN_CAPTURE,
	LISP_BUILTIN_LAMBDA,
	LISP_BUILTIN_QUOTE,
	LISP_BUILTIN_CALLCC,
	LISP_BUILTIN_SET,
	LISP_BUILTIN_SETCAR,
	LISP_BUILTIN_SETCDR,
	LISP_BUILTIN_CONS,
	LISP_BUILTIN_CAR,
	LISP_BUILTIN_CDR,
	LISP_BUILTIN_EVAL,
	// arithmetic
	LISP_BUILTIN_ADD,
	LISP_BUILTIN_SUB,
	LISP_BUILTIN_MUL,
	LISP_BUILTIN_DIV,
	LISP_BUILTIN_BITIOR,
	LISP_BUILTIN_BITAND,
	LISP_BUILTIN_BITXOR,
	LISP_BUILTIN_BITNOT,
	LISP_BUILTIN_REM,
	// predicates
	LISP_BUILTIN_ISPAIR,
	LISP_BUILTIN_ISEQ,
	LISP_BUILTIN_ISLESS,
	LISP_BUILTIN_ISERROR,
	LISP_BUILTIN_TRUE,
	LISP_BUILTIN_FALSE,
	// io
	LISP_BUILTIN_PRINT1,
	// error
	LISP_BUILTIN_ERROR,
	LISP_NUM_BUILTINS,

	LISP_BUILTIN_FORWARD,	// special builtin for pointer forwarding

	// states for lispstep()
	LISP_STATE_APPLY,
	LISP_STATE_BETA1,
	LISP_STATE_CONTINUE,
	LISP_STATE_DEFINE1,
	LISP_STATE_SET1,
	LISP_STATE_SET2,
	LISP_STATE_RETURN,
	LISP_STATE_EVAL,
	LISP_STATE_IF1,
	LISP_STATE_LISTEVAL,
	LISP_STATE_LISTEVAL1,
	LISP_STATE_LISTEVAL2,
	LISP_STATE_HEAD1,

};

struct Mach {
	lispref_t inst; // state of the lispstep state machine

	lispref_t reg0; // temp for mkcons.
	lispref_t reg1; // temp for mkcons.
	lispref_t reg2;
	lispref_t reg3;
	lispref_t reg4;

	lispref_t regs[32];
	uint32_t reguse;

	lispref_t value; // return value
	lispref_t expr; // expression being evaluated
	lispref_t envr; // current environment, a stack of a-lists
	lispref_t stack; // call stack

	struct {
		lispref_t *ref;
		size_t len;
		size_t cap;
	} idx, mem, copy;

	struct {
		char *p;
		size_t len;
		size_t cap;
	} strings;

	struct {
		struct {
			void *obj;
			void *type;
		} *p;
		size_t len;
		size_t cap;
	} extrefs;

	char *tok;
	size_t toklen;
	size_t tokcap;

	int lineno;

	int gclock;

	struct {
		void *context;
		int (*writebyte)(int ch, void *context);
		int (*readbyte)(void *context);
		int (*unreadbyte)(int ch, void *context);
	} *ports;
	size_t portslen;
	size_t portscap;
};

void lispinit(Mach *m);
int lispsetport(Mach *m, lispport_t port, int (*writebyte)(int ch, void *ctx), int (*readbyte)(void *ctx), int (*unreadbyte)(int ch, void *ctx), void *ctx);
lispref_t lispparse(Mach *m, int justone);
int lisperror(Mach *m, lispref_t a);
void lispcall(Mach *m, int ret, int inst);
int lispstep(Mach *m);
void lispcollect(Mach *m);
lispref_t lispcar(Mach *m, lispref_t base);
lispref_t lispcdr(Mach *m, lispref_t base);
int lispprint1(Mach *m, lispref_t aref, lispport_t port);
lispref_t *lispregister(Mach *m, lispref_t val);
void lisprelease(Mach *m, lispref_t *reg);
int lispsymbol(Mach *m, lispref_t a);
int lispnumber(Mach *m, lispref_t a);
int lispbuiltin(Mach *mach, lispref_t a, int builtin);
int lispextref(Mach *m, lispref_t a);
int lisppair(Mach *m, lispref_t a);
lispref_t lispmksymbol(Mach *m, char *str);
lispref_t lispmkbuiltin(Mach *m, int val);
void lispdefine(Mach *m, lispref_t sym, lispref_t val);

lispref_t lispextalloc(Mach *m);
int lispextset(Mach *m, lispref_t ext, void *obj, void *type);
int lispextget(Mach *m, lispref_t ext, void **obj, void **type);

int lispgetint(Mach *m, lispref_t num);
lispref_t lispmknumber(Mach *m, int);
