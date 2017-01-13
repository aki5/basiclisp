(define(apply fn args) (fn . args))
(define(list . args) args)
(define(not x) (if x #f #t))
(define(null? x) (eq? x '()))
(define(begin . args) (if(null? args) '() (if(null? (cdr args)) (car args) (apply begin (cdr args)))))
(define(and . args)
	(if(null? args) #t
		(if(not(car args)) #f
			(apply and (cdr args)))))
(define(or . args)
	(if(null? args) #f
		(if(car args) #t
			(apply or (cdr args)))))
(define(list? lst)
	(if(pair? lst)
		(if(null? lst) #t (list? (cdr lst)))
		(null? lst)))
(define(append head tail)
	(if(null? head) tail
		(cons(car head)(append(cdr head) tail))))
(define(print . args)
	(define(print-sp needsp)
		(if needsp (print1 " ") '()))
	(define(print-pair obj needsp)
		(define head (car obj))
		(if(pair? head)
			(list (print1 "(") (print-obj head #f) (print1 ")"))
			(list (print-sp needsp) (print1 head)))
		(print-obj (cdr obj) #t))
	(define(print-obj obj needsp)
		(if(pair? obj)
			(print-pair obj needsp)
			(if(null? obj) '()
				(list (print1 " . ") (print1 obj)))))
	(print-obj args #f)
	args)
(define(map fn ls)
	(if(null? ls) '()
		(cons(fn(car ls))(map fn(cdr ls)))))
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
	(define(split lls)
		(if(null? lls) '()
			(cons(cons(car lls) '())(split(cdr lls)))))
	(sort2(split ls)))
(define(fib n)
	(define(fib2 p1 p2 n)
		(if(eq? n 0) '()
			(cons(+ p1 p2)(fib2 p2(+ p1 p2)(- n 1)))))
	(fib2 1 1 n))
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
(define(primegen)
	(define next 2)
	(define primelist '())
	(define (prime? n lst)
		(if(null? lst) #t
			(if(eq? (remainder n (car lst)) 0) #f
				(prime? n (cdr lst)))))
	(define generator (lambda()
		(if(prime? next primelist)
			((lambda(rval)
				(set! primelist (cons next primelist))
				(set! next (+ rval 1))
				rval) next)
			((lambda(rval)
				(set! next (+ rval 1))
				(generator)) next))))
	generator)
(define(getn gen n)
	(if(eq? n 0) '()
		(cons (gen) (getn gen (- n 1)))))
(define(primes n)
	(getn (primegen) n))
(define(factor n)
	(define(fact1 n k)
		(if(eq? n 1) '()
			(if(eq? (remainder n k) 0)
				(cons k (fact1 (/ n k) k))
				(fact1 n (+ k 1)))))
	(fact1 n 2))
(define(flip sign) (if(eq? sign '+) '- '+))
(define(det2x2 sign fact
	a0 a1
	b0 b1)
	(list (list sign (list '* a0 b1 . fact)) (list (flip sign) (list '* b0 a1 . fact))))
(define(det3x3 sign fact
	a0 a1 a2
	b0 b1 b2
	c0 c1 c2)
	(append (det2x2 sign (cons a0 fact) b1 b2 c1 c2)
	(append	(det2x2 (flip sign) (cons a1 fact) b0 b2 c0 c2)
		(det2x2 sign (cons a2 fact) b0 b1 c0 c1))))
(define(make-object val)
	(lambda(method)
		(if(eq? method 'val) val
			'())))
(define(make-vector len)
	(if(< len 2) 0
		(cons (make-vector (/ len 2)) (make-vector (/ len 2)))))
(define(make-prng first)
	(define(findlst lst)
		(if(null? (cdr lst)) lst
			(findlst (cdr lst))))
	(define last (findlst first))
	(define s0 0)
	(define s1 0)
	(define xorval 0)
	(lambda()
		(set-cdr! last first)
		(set! last first)
		(set! first (cdr first))
		(set! s0 (car last))
		(set! s1 (car first))
		(set! s1 (bitwise-xor s1 (* s1 2147483648)))
		(print "s0:" s0 " s1:" s1 "\n")
		(set! xorval (bitwise-xor s1 s0 (/ s1 2048) (/ s0 1073741824)))
		(set-car! first xorval)
		(* xorval 1181783497276652981)))
(define random (make-prng '(0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15)))
(define (pow x y) (if(eq? y 0) 1 (* x (pow x (- y 1)))))
