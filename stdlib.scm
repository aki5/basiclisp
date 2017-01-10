
(define(seq a b)
	(if(eq? a b) '()
		(cons a(seq(+ a 1) b))))
(define(reverse ls)
	(define(rev ls r)
		(if(null? ls)
			r
			(rev(cdr ls)(cons(car ls) r))))
	(rev ls '()))
(define(sort cmp ls)
	(define(merge ls1 ls2)
		(if(null? ls1) ls2
			(if(null? ls2) ls1
				(if(cmp(car ls1)(car ls2))
					(cons(car ls1)(merge(cdr ls1) ls2))
					(cons(car ls2)(merge ls1(cdr ls2)))))))
	(define(pass lls)
		(if(null? lls) '()
			(if(null?(cdr lls))(cons(car lls) '())
				(cons(merge(car lls)(car(cdr lls)))
					(pass(cdr(cdr lls)))))))
	(define(sort2 lls)
		(if(null?(cdr lls))(car lls)
			(sort2(pass lls))))
	(define(split ls)
		(if(null? ls) '()
			(cons(cons(car ls) '())(split(cdr ls)))))
	(sort2(split ls)))

(define(append head tail)
	(if(null? head) tail
		(cons(car head)(append(cdr head) tail))))
(define(fib n)
	(define(fib2 p1 p2 n)
		(if(eq? n 0) '()
			(cons(+ p1 p2)(fib2 p2(+ p1 p2)(- n 1)))))
	(fib2 1 1 n))
(define(map fn ls)
	(if(null? ls) '()
		(cons(fn(car ls))(map fn(cdr ls)))))
(define Y
	(lambda(h)
		((lambda(x)(x x))
			(lambda(g)(h(lambda args((g g) . args)))))))
(define yfac
	(Y
		(lambda(fn)
			(lambda(x)
				(if(< x 2) 1
					(* x(fn(- x 1))))))))
(define yfib
	(Y
		(lambda(fn)
			(lambda(x)
				(if(< x 2)
					x
					(+(fn(- x 1))(fn(- x 2))))))))
