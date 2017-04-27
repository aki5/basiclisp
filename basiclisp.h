
typedef unsigned int ref_t;
#define NIL (ref_t)0
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
	BLT_ISERROR,
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

void lispinit(Mach *m);
ref_t listparse(Mach *m, FILE *fp, int justone);
int iserror(Mach *m, ref_t a);
void vmcall(Mach *m, ref_t ret, ref_t inst);
ref_t vmload(Mach *m, ref_t base, size_t offset);
int vmstep(Mach *m);
