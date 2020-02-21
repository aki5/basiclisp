((lambda()
	(define(bitwise-shift-left x a)
		(if (equal? a 0)
			x
			(bitwise-shift-left (* 2 x) (- a 1))))
	(define(basis a)
		(bitwise-shift-left 1 a))
	(define(pairs als bls)
		(if (null? als)
			'()
			(cons
				(cons (car als) (car bls))
				(pairs (cdr als) (cdr bls)))))

	(define(for ls fn)
		(if (null? ls)
			'()
			((lambda()
				(fn (car ls))
				(for (cdr ls) fn)
			))
		)
	)

	(for (seq 1 10) (lambda(i) (print1 1 i) (print1 1 ".")))
	(print 1 "\n")

	(define ids (seq 0 20))
	(print 1 (pairs ids (map basis ids)) "\n")

	(define syms0 '((6 . e0) (2 . e1) ( 77 . e2)))
	(define syms1 '(e0 e1 e2 e3 e4 e5))
	(print 1 (sort less? (reverse syms1)) "\n")
	(print 1 (less? 'e0 'e1))
	(define e0 "123")
	(define q 'e0)
	(print 1 (eval q) "." q "\n")

	(define(traverse ls)
		(if(pair? ls)
			((lambda()
				(traverse (car ls))
				(traverse (cdr ls))))
			(print 1 ls "\n")))

	(define x 1)
	(define(function-body fn)
		(car (cdr fn)))
	(traverse (function-body traverse))
	(print "\n")

	; the pre-macro language extension system in LISP is the fexpr, which is a
	; special form which gets its arguments un-evaluated.

	; if we were to introduce vectors, we should make them somehow include but generalize
	; the cons, which is a pair. ie.
	(equal? (load 0 x) (car x)) ; should be true.
	(equal? (load 1 x) (cdr x)) ; should be true.
	; what should the vector accessors be? and how should we define one?
	(define v2 (vector 'e1 'e2))
	(define v1 (cons 'e1 'e2))
	(seti! 0 v1 'e3)
	(set-car! v1 'e3)
	(geti 0 v1) vs. (car v1)
	; if we got really crazy, we could simply interpret integers in the function position as
	; indexingn operations, making something like
	(0 v1) the same as (car v1), and
	(1 v1) the same as (cdr v1).
	; furthermore, if the first position was an arbitrary expression evaluating to an integer,
	; we could still use it to index the second element. why not?
	; since we are defining a language for computational geometry, we might as well choose 
))

