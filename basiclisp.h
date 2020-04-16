
typedef unsigned int LispRef;
typedef unsigned int LispPort;
typedef struct LispMachine LispMachine;

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

	LISP_INLINE_SYMBOL = 0x110000,  // all single unicode codepoints
	//LISP_INLINE_SYMBOL = 0x80, // all ascii characters
	//LISP_INLINE_SYMBOL = 0, // nothing

	LISP_TAG_PAIR = 0,	// 1... .... 32k (2B) cells (pairs)
	LISP_TAG_INTEGER,	// 01.. .... 16k (1B) unsigned ints
	LISP_TAG_EXTREF,	// 001. ....  8k (512M) external objects
	LISP_TAG_SYMBOL,	// 0001 ....  4k (256M) symbols (unicode codepoint or offset to name table)
	LISP_TAG_BUILTIN,	// 0000 1...  2k (128M) built-in functions (enumerated below)

	LISP_BUILTIN_CALLCC = 0,
	LISP_BUILTIN_CAR,
	LISP_BUILTIN_CDR,
	LISP_BUILTIN_COMPARE,
	LISP_BUILTIN_CONS,
	LISP_BUILTIN_CONTINUE,
	LISP_BUILTIN_EVAL,
	LISP_BUILTIN_FUNCTION,
	LISP_BUILTIN_IF,
	LISP_BUILTIN_LAMBDA,
	LISP_BUILTIN_LET,
	LISP_BUILTIN_QUOTE,
	LISP_BUILTIN_SCOPE,
	LISP_BUILTIN_SET,
	LISP_BUILTIN_SETCAR,
	LISP_BUILTIN_SETCDR,

	// arithmetic
	LISP_BUILTIN_ADD,
	LISP_BUILTIN_SUB,
	LISP_BUILTIN_MUL,
	LISP_BUILTIN_DIV,

	// predicates
	LISP_BUILTIN_ISPAIR,
	LISP_BUILTIN_ISEQUAL,
	LISP_BUILTIN_ISLESS,
	LISP_BUILTIN_ISERROR,

	// (equal? a b) ->
	LISP_BUILTIN_TRUE,	// #true
	LISP_BUILTIN_FALSE, // #false
	LISP_BUILTIN_NIL, // #nil

	// (compare a b) ->
	LISP_BUILTIN_ABOVE, // #above
	LISP_BUILTIN_EQUAL, // #equal
	LISP_BUILTIN_BELOW, // #below

	// io
	LISP_BUILTIN_PRINT1,

	// error
	LISP_BUILTIN_ERROR,
	LISP_NUM_BUILTINS,

	LISP_BUILTIN_FORWARD,	// special builtin for pointer forwarding

	// states for lispstep()
	LISP_STATE_APPLY,
	LISP_STATE_BETA0,
	LISP_STATE_BETA1,
	LISP_STATE_BETA2,
	LISP_STATE_BETA3,
	LISP_STATE_CONTINUE,
	LISP_STATE_LET0,
	LISP_STATE_LET1,
	LISP_STATE_SET0,
	LISP_STATE_SET1,
	LISP_STATE_SET2,
	LISP_STATE_SET3,
	LISP_STATE_RETURN,
	LISP_STATE_EVAL,
	LISP_STATE_IF0,
	LISP_STATE_IF1,
	LISP_STATE_EVAL_ARGS0,
	LISP_STATE_EVAL_ARGS1,
	LISP_STATE_EVAL_ARGS2,
	LISP_STATE_EVAL_ARGS3,
	LISP_STATE_SYM_LOOKUP0,
	LISP_STATE_SYM_LOOKUP1,
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
