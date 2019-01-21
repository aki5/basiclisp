
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

Lisp evaluation:

	1. takes a list as input,
	2. recursively evaluates all sublists,
	3. evaluates the list itself by applying the built-in
	   indicated by the head element.

Lambda is the function factory of lisp. It is called lambda due to
the Church lambda calculus, but calling it func or function might
have been just as proper.

	(lambda (argname1 argname2 ..) (body1) (body2) ...)

Ie. it takes a parameter list a function-body as arguments. The
function-body may have references to symbols other than those listed
as arguments, in which case they refer to the variables as they were
at definition time.

Evaluation of a lambda-expr '(lambda (args) (body1) (body2) ...)' takes
place as follows

	1. find the current environment (envr)
	2. construct a beta-expr '(beta lambda-expr . (nil . envr))'
	3. return 2 as the evaluated result.

So, a lambda will evaluate into a beta that still encloses the original
lambda expression, like this

	((beta (lambda (argnames) (body1) ...) . (nil . envr)) argvals)'

So, when a function which has been defined as a lambda gets called,
it is beta that does the work, and beta in our case looks as follows

	1. create a new environment, chain it to 'envr'
	   from the list and install it as the evaluation environment.
	2. pair argvals with argnames and define them in the new
	   envr.
	3. evaluate each body1 body2 ... in sequence
	4. take the value of the last body as the result.

The (nil . envr) pair is a technicality allowing us to extend a
current environment without creating a new one. You can think of it as
the head element of an environment. The nil could be replaced with
anything a lookup routine can efficiently skip over.

In short, lambda-evaluation binds the environment as it existed at
definition-time, and beta-evaluation binds function arguments to a
new environment, popping both the lambda and the beta off the list
head and evaluating the expression in the environment from beta.

