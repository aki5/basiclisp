
typedef unsigned int lispref_t;
typedef unsigned int lispport_t;
#define LISP_NIL (lispref_t)0
enum {
	LISP_TAG_PAIR = 0,	// first so that NIL value of 0 is a cons.
	LISP_TAG_INTEGER,
	LISP_TAG_STRING,
	LISP_TAG_SYMBOL,
	LISP_TAG_ERROR,
	LISP_TAG_BUILTIN,

	// todo: instead of these, we should have LISP_TAG_EXTERN and
	// 
	LISP_TAG_BIGINT,
	LISP_TAG_FLOAT,

	LISP_BUILTIN_IF = 0,
	LISP_BUILTIN_BETA,
	LISP_BUILTIN_CONTINUE,
	LISP_BUILTIN_DEFINE,
	LISP_BUILTIN_LAMBDA,
	LISP_BUILTIN_QUOTE,
	LISP_BUILTIN_CALLEXT,
	LISP_BUILTIN_CALLCC,
	LISP_BUILTIN_SET,
	LISP_BUILTIN_SETCAR,
	LISP_BUILTIN_SETCDR,
	LISP_BUILTIN_CONS,
	LISP_BUILTIN_CAR,
	LISP_BUILTIN_CDR,
	LISP_BUILTIN_CLEANENV,
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
	LISP_STATE_RETURN,
	LISP_STATE_EVAL,
	LISP_STATE_IF1,
	LISP_STATE_LISTEVAL,
	LISP_STATE_LISTEVAL1,
	LISP_STATE_LISTEVAL2,
	LISP_STATE_HEAD1,

};

typedef struct Mach Mach;
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
	lispref_t cleanenvr; // clean environment (just the builtins)

	lispref_t *idx;
	size_t idxlen;
	size_t idxcap;

	lispref_t *mem;
	size_t memlen;
	size_t memcap;

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
lispref_t lispload(Mach *m, lispref_t base, size_t offset);
int lispstep(Mach *m);
void lispgc(Mach *m);
