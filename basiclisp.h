
typedef unsigned int LispRef;
typedef unsigned int LispPort;
typedef struct LispMachine LispMachine;
typedef LispRef (LispApplier)(void *, void *, LispRef);
typedef LispRef (LispGetter)(void *, void *, LispRef);
typedef LispRef (LispSetter)(void *, void *, LispRef, LispRef);
typedef LispRef (LispBinaryOp)(void *, LispRef, LispRef);
typedef LispRef (LispTernaryOp)(void *, LispRef, LispRef, LispRef);

// these are macros because enums are signed and these fiddle with the MSB.
#define LISP_TAG_BIT ((LispRef)1<<(8*sizeof(LispRef)-1))
#define	LISP_VAL_MASK ((LispRef)LISP_TAG_BIT-1)
#define LISP_NIL (LispRef)(LISP_TAG_BIT>>LISP_TAG_PAIR)

enum {
	LISP_CAR_OFFSET = 0,
	LISP_CDR_OFFSET = 1,

	LISP_TOK_INTEGER = 1000,
	LISP_TOK_SYMBOL,
	LISP_TOK_STRING,

	LISP_TAG_PAIR = 0,	// 1...... 32k (2B) cells (pairs)
	LISP_TAG_INTEGER,	// 01..... 16k (1B) unsigned ints
	LISP_TAG_EXTREF,	// 001....  8k (512M) external objects
	LISP_TAG_SYMBOL,	// 0001...  4k (256M) symbols (offset to the name table)
	LISP_TAG_BUILTIN,	// 00001..  2k (128M) built-in functions (enumerated below)

	LISP_BUILTIN_IF = 0,
	LISP_BUILTIN_BETA,
	LISP_BUILTIN_CONTINUE,
	LISP_BUILTIN_LET,
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
	LISP_BUILTIN_ISEQUAL,
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
	LISP_STATE_LET0,
	LISP_STATE_LET1,
	LISP_STATE_SET0,
	LISP_STATE_SET1,
	LISP_STATE_SET2,
	LISP_STATE_RETURN,
	LISP_STATE_EVAL,
	LISP_STATE_IF0,
	LISP_STATE_IF1,
	LISP_STATE_LISTEVAL0,
	LISP_STATE_LISTEVAL1,
	LISP_STATE_LISTEVAL2,
	LISP_STATE_BUILTIN0,
	LISP_STATE_SPECIAL_FORMS,

};

struct LispMachine {
	LispRef inst; // state of the lispstep state machine

	LispRef regs[8];
	uint32_t reguse;
	int regCount;
	int maxRegCount;

	LispRef value; // return value
	LispRef expr; // expression being evaluated
	LispRef envr; // current environment, a stack of a-lists
	LispRef stack; // call stack

	struct {
		LispRef *ref;
		size_t len;
		size_t cap;
	} stringIndex, mem, copy;

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

	struct {
		char *buf;
		size_t len;
		size_t cap;
	} token;

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

void lispInit(LispMachine *m);
int lispSetPort(LispMachine *m, LispPort port, int (*writebyte)(int ch, void *ctx), int (*readbyte)(void *ctx), int (*unreadbyte)(int ch, void *ctx), void *ctx);
LispRef lispParse(LispMachine *m, int justone);
int lispIsError(LispMachine *m, LispRef a);
void lispCall(LispMachine *m, int ret, int inst);
int lispStep(LispMachine *m);
void lispCollect(LispMachine *m);
LispRef lispCar(LispMachine *m, LispRef base);
LispRef lispCdr(LispMachine *m, LispRef base);
int lispPrint1(LispMachine *m, LispRef aref, LispPort port);
LispRef *lispRegister(LispMachine *m, LispRef val);
void lispRelease(LispMachine *m, LispRef *reg);
int lispIsSymbol(LispMachine *m, LispRef a);
int lispIsNumber(LispMachine *m, LispRef a);
int lispIsBuiltin(LispMachine *mach, LispRef a, int builtin);
int lispIsExtRef(LispMachine *m, LispRef a);
int lispIsPair(LispMachine *m, LispRef a);
int lispIsNull(LispMachine *m, LispRef a);
LispRef lispSymbol(LispMachine *m, char *str);
LispRef lispBuiltin(LispMachine *m, int val);
void lispDefine(LispMachine *m, LispRef sym, LispRef val);

LispRef lispExtAlloc(LispMachine *m);
int lispExtSet(LispMachine *m, LispRef ext, void *obj, void *type);
int lispExtGet(LispMachine *m, LispRef ext, void **obj, void **type);

int lispGetInt(LispMachine *m, LispRef num);
LispRef lispNumber(LispMachine *m, int);

LispRef lispCons(LispMachine *m, LispRef a, LispRef d);
