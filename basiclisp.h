
typedef unsigned short lispref_t;
typedef unsigned int lispport_t;
typedef struct Mach Mach;
typedef lispref_t (lispapplier_t)(void *, void *, lispref_t);
typedef lispref_t (lispgetter_t)(void *, void *, lispref_t);
typedef lispref_t (lispsetter_t)(void *, void *, lispref_t, lispref_t);

#define LISP_NIL (lispref_t)0
enum {
	LISP_CAR_OFFSET = 0,
	LISP_CDR_OFFSET = 1,

	LISP_TAG_BITS = 3,
	LISP_TAG_MASK = (1<<LISP_TAG_BITS)-1,

	LISP_TAG_PAIR = 0,	// first so that NIL value of 0 is a cons
	LISP_TAG_INTEGER,	// a 29-bit signed int posturing as a reference
	LISP_TAG_SYMBOL,	// symbol (has a name in the string table)
	LISP_TAG_BUILTIN,	// built-in function (enumerated below)
	LISP_TAG_STRING,	// string literal
	LISP_TAG_FORWARD,	// for garbage collection
	LISP_TAG_ERROR,		// error (explanation in the string table)
	LISP_TAG_EXTREF,	// external object

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
	LISP_NUM_BUILTINS,

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
			lispapplier_t *apply;
			lispsetter_t *set;
			lispgetter_t *get;
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
void lispcall(Mach *m, lispref_t ret, lispref_t inst);
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
lispref_t lispstrtosymbol(Mach *m, char *str);
void lispdefine(Mach *m, lispref_t sym, lispref_t val);

lispref_t lispextalloc(Mach *m);
int lispextset(Mach *m, lispref_t ext, void *obj, lispapplier_t *apply, lispsetter_t *set, lispgetter_t *get);
int lispextget(Mach *m, lispref_t ext, void **obj, lispapplier_t **apply, lispsetter_t **set, lispgetter_t **get);

int lispgetint(Mach *m, lispref_t num);
lispref_t lispint(Mach *m, int);
