
## Things to do

- [x]Introduce an integer accessor for lists as functions, as follows
  - `lispref_t lispindex(Mach *m, lisptref_t ls, lispref_t i);` and
  - `lispref_t lispsetindex(Mach *m, lispref_t ls, lispref_t i, lispref_t v);`
- [ ] Call them from the approppriate places.
  - hesitating now that it seems like the required stack gymnastics might be
    significant.
- [x] Introduce stack accessors
	- `lispref_t lisppush(Mach *, lispref_t val);` and
	- `lispref_t lisppop(Mach *);`
- [ ] Get rid of the register file and rewrite the state machine to use the stack
  instead. Use `lispindex` and `lispsetindex` from above everywhere and define
  local variables by pushing them on the stack.
- Note that `lisppush` needs to allocate, so a lispref_t is not valid across it.
  Haevy use of lisppush means everything needs to live in the vm stack instead
  of the C runtime stack. It is already nearly true, but the entire code needs a
  look.
- [ ] Now that the register file is gone, introduce an external environment which
  consists of `(sym . val)` pairs just like any evaluation environment. This
  list is intended to provide an association so that an external actor can refer
  to a local variable (object) without being broken by local garbage collections
  or vm restarts. The sym in this context is _not_ meant to arrive verbatim from
  a remote entity. Instead sym is only _locally_ unique, but the name it
  corresponds to has global meaning in this context, and it is possible to
  transmit those names between vm's without interference from garbage collection
  or restarts.
- [ ] Implement unique identifiers as external objects (see below) to facilitate
  dynamic object creation by external entities.. For example mutable iterators
  to local structures.
- Create something useful that works with external references in a
  complicated setting. A quad-edge data structure seems like a solid candidate.
	- iterator objects (extref),
		- `('data iter)` accessor,
		- `(iter 'next)` method,
		- `(iter 'rot)` method,
		- `(iter 'splice iter2)` method
	- mesh garbage collector, iterators are root pointers
	- would it be possible to have lisp references as edge data? as a direct
	  approach this seems doomed, the extern environment would blow up in size,
	  but if we had a _map_ object as an extref in the mesh object and it
	  stored stable keys (ints) into that map... the lisp array would need to be
	  explicitly collected (entries removed by extern calls by mesh gc).
- [ ] Make the garbage collector incremental.
  - The plan is to have one extra bit to indicate a cell has already been
    converted. This is to avoid falsely converting cells allocated while gc is
    running.
- To maintain the number of references we support (or more), it may be time to
  make the tag field variable in length as follows

## Identifying external objects

If extrefs need to refer to objects with changing identities, for example
because the object is a vm itself and performs compacting garbage collection,
or because the other vm is on a different computer that got restarted, cases
like that.

The following figure illustrates a solution in basiclisp. The figure shows a
situation where one VM has a reference to an object inside another. The same
principle applies to any kind of external object referenced from basiclisp:
only stable identifiers should be stored in extref. Note that storing pointers
in extrefs is fine for objects that don't move during a vm's lifespan.

![Alt text](stable-extref.svg)

The idea is to create a locally unique identifier which can be safely
exported and is never reused, an expression like `(new 'uid)` seems nice enough.
Local uniqueness will suffice since the uid is always localized to the
object pointed to in extref. The object must maintain an association list
between the unique identifier and a (local) object reference, in other words,
an environment. In a vm, uids can be implemented as symbols to make the
association list identical to an environment: a list of (sym . val) at its
simplest. These made up symbols don't necessarily need to have a name. Due
to their eternal lifespan, a lot of them may be required in case the external
api allows creating new ones. Using 64bit integers as symbol identifiers seems
more than sufficient: it takes over 500 years to transfer 2^64 bytes at 1GB/s.
Since these identifiers are intended to be localized to a 1:1 connection, there
is no particular need to make them difficult to guess: we might even make it
possible to enumerate them. Instead, security should be based on access control
lists in variables, proper authentication at connections and end-to-end
encryption of transferred data.

A special encoding is warranted for devices with very little memory, where
local references are possibly 16bit. For this reason, it is probably best to
implement unique identifiers as external objects in the extref table, which
brings us back full circle.

If we implement lookup as a state in the vm, it should be easy to allow
external objects in place of symbols. All that is needed is a working
`(equal? obj1 obj2)` routine between them, since that's what the lookup is
based on. It only ever needs to return true if obj1 and obj2 are of the same
type, since since objects of different type differ by definition. Basiclisp will
escape to caller when it encounters exterefs in a built-in expression, allowing
any implementation strategy for the overload.

## Security

Data security is based on access control lists in variables. Unique identifiers
are only usable in an association list, each entry of which is a variable with
an access control list. A connection will have an association list to provide a
context for evaluating remote expressions.

The connection itself needs to be properly authenticated to enforce access
control. It will also be necessary to properly encrypt the connection to avoid
connection hijacking and malicious expression injection.

## VM serialization

the extref type system should be made safe to serialize and deserialize. given
the objects themselves can be serialized and deserialized via required functions
in the type, the only question of safety is about function pointers inside the
type(s). given the state can be received by a binary of different build than the
sender (quite intentionally), it is important to have a safe way of referring
to the types themselves. if the types were simply in an array, it would be too
easy to mess up the array indices and cause objects to be interpreted as
something they were not.. hence it is probably safer to have an enum giving
a persistent id to each type, and make sure that things are only ever added to
the end of the enum.. this way it should be possible to remove types from the
array (but never the enum) once there are no references to them anywhere in the
universe.

if serialization format for a type is changed, it should become a new type
instead of modifying the old one, and there should be a converter for converting
an old object into a new one and vice versa. one can then ship the new type
without anything happening and trigger a system-wide object conversion after the
feature has shipped.

if the old object implemented an external api, the old api can be kept alive if
the system can automatically find the new object and call approppriate
conversion routines for both set and get.

## Continuations

Continuations are why we can't have nice things like freeing a stack cell on
pop. A continuation gets a reference to the current stack frame when created,
after which one does not simply free stack cells: when re-used they would modify
the continuation in undefined ways.

This is a big price to pay for continuations, but we are doing it. There may be
solutions to this, including copying the entire stack when a continuation is
created. We could also somehow mark the stack cells chained to the continuation
as referenced, but copying the stack seems like a good idea by comparison.

Continuations do to the stack what lambdas do to the environment. To make
lambdas more economical we introduced capture, which makes a copy of the
environment with only specified variables. Could there be something similar for
continuations? At least allow chopping the stack off for continuations created
in infinite loops and such somehow?

## Old text, out of date

Basic-lisp is my 2016 christmas holidays project, bleeding
into 2017. It is a small scheme interpreter, intended for
embedding in larger programs that could use a small scripting
language.

Small footprint and a single c-file implementation were some sort
of objectives, while high performance was not. The interpreter is
an abstract lisp machine, and runs on a fixed amount of C runtime
stack.

The interpreter supports double precision floating point and
long long arithmetic, but will encode small integer values
in the referenecs (16-bit by default).

The garbage collector is of the breadth-first semi-space kind,
and could use some optimization, as it calls malloc during gc
to grow the new to-space and to allocate bitmaps for book-keeping.

More than half of the code is just dealing with s-expression parsing
and printing, both of which are yet to be refactored into the GC
compatible lisp state machine (they currently lock the gc).

### Lisp evaluation:

1. takes a list as input,
2. recursively evaluates all sublists,
3. evaluates the list itself by applying the built-in
	indicated by the head element.

Lambda is the function factory of lisp. It is called lambda due to
the Church lambda calculus, but calling it func or function might
have been just as proper.

`(lambda (argname1 argname2 ..) (body1) (body2) ...)`

Ie. it takes a parameter list a function-body as arguments. The
function-body may have references to symbols other than those listed
as arguments, in which case they refer to the variables as they were
at definition time.

Evaluation of a lambda-expr `(lambda (args) (body1) (body2) ...)` takes
place as follows

1. find the current environment (envr)
2. construct a beta-expr '(beta lambda-expr . (nil . envr))'
3. return 2 as the evaluated result.

So, a lambda will evaluate into a beta that still encloses the original
lambda expression, like this

`((beta (lambda (argnames) (body1) ...) . (nil . envr)) argvals)'`

So, when a function which has been defined as a lambda gets called,
it is beta that does the work, and beta in our case looks as follows

1. create a new environment, chain it to 'envr' from the list and install it
   as the evaluation environment.
2. pair argvals with argnames and define them in the new envr.
3. evaluate each body1 body2 ... in sequence
4. take the value of the last body as the result.

The `(nil . envr)` pair is a technicality allowing us to extend a
current environment without creating a new one. You can think of it as
the head element of an environment. The nil could be replaced with
anything a lookup routine can efficiently skip over.

In short, lambda-evaluation binds the environment as it existed at
definition-time, and beta-evaluation binds function arguments to a
new environment, popping both the lambda and the beta off the list
head and evaluating the expression in the environment from beta.

