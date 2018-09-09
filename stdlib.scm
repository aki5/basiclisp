(define(apply fn args) (fn . args))
(define(list . args) args)
(define(not x) (if x #f #t))
(define(null? x) (equal? x '()))
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
(define(caar ls) (car (car ls)))
(define(cadr ls) (car (cdr ls)))
(define(cddr ls) (cdr (cdr ls)))
(define(cdar ls) (cdr (car ls)))
(define(inject fn key val) (set-cdr! (cdr fn) (cons (cons key val) (cdr (cdr fn)))))
(define(print port . args)
	(define(print-sp needsp)
		(if needsp (print1 port " ") '()))
	(define(print-pair obj needsp)
		(define head (car obj))
		(if(pair? head)
			(if(equal? (car head) beta)
				((lambda() (print-sp needsp) (print1 port "(") (print-obj (car (cdr head)) #f) (print1 port ")")))
				((lambda() (print-sp needsp) (print1 port "(") (print-obj head #f) (print1 port ")"))))
			((lambda() (print-sp needsp) (print1 port head))))
		(print-obj (cdr obj) #t))
	(define(print-obj obj needsp)
		(if(pair? obj)
			(print-pair obj needsp)
			(if(null? obj) '()
				((lambda() (print1 port " . ") (print1 port obj))))))
	(print-obj args #f)
	args)
(define(map fn ls)
	(if(null? ls) '()
		(cons(fn(car ls))(map fn(cdr ls)))))
(define(seq a b)
	(if(equal? a b) '()
		(cons a(seq(+ a 1) b))))
(define(reverse ls)
	(define(rev ls r)
		(if(null? ls) r
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
		(if(null? lls) lls
			(if(null?(cdr lls)) lls
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
		(if(equal? n 0) '()
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
				(if(less? x 2) 1
					(* x(fn(- x 1))))))))
(define yfib
	(Y
		(lambda(fn)
			(lambda(x)
				(if(less? x 2)
					x
					(+(fn(- x 1))(fn(- x 2))))))))
(define(primegen)
	(define next 2)
	(define primelist '())
	(define (prime? n lst)
		(if(null? lst) #t
			(if(equal? (remainder n (car lst)) 0) #f
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
	(if(equal? n 0) '()
		(cons (gen) (getn gen (- n 1)))))
(define(primes n)
	(getn (primegen) n))
(define(factor n)
	(define(fact1 n k)
		(if(equal? n 1) '()
			(if(equal? (remainder n k) 0)
				(cons k (fact1 (/ n k) k))
				(fact1 n (+ k 1)))))
	(fact1 n 2))
(define(flip sign) (if(equal? sign '+) '- '+))
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
		(if(equal? method 'val) val
			'())))
(define(make-vector len)
	(if(less? len 2) 0
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
		(set! xorval (bitwise-xor s1 s0 (/ s1 2048) (/ s0 1073741824)))
		(set-car! first xorval)
		(* xorval 1181783497276652981)))
(define random (make-prng '(0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15)))
(define(iabs x) (if(less? x 0) (- x) x))
(define (frandom)
	(define(conv i f)
		(if(less? i 2) f (conv (/ i 2) (if(equal? (remainder i 2) 0) (/ f 2.0) (+ (/ f 2.0) 0.5)))))
	(conv (iabs (random)) 0.0))
(define(pow x y) (if(equal? y 0) 1 (* x (pow x (- y 1)))))
(define(fact x)(if(equal? x 1) 1 (* x (fact (- x 1)))))
(define(bitwise-popcnt res)
	(define(popcnt-step masks div res)
		(if(null? masks) res
			(popcnt-step (cdr masks) (* div div)
				(+ (bitwise-and (car masks) res) (bitwise-and (car masks) (/ res div))))))
	(popcnt-step '(
		0x5555555555555555 0x3333333333333333 0x0f0f0f0f0f0f0f0f
		0x00ff00ff00ff00ff 0x0000ffff0000ffff 0x00000000ffffffff) 2 res))
(define(trig-step sign xi fi x2 fs res)
	(if(less? fs 30.0)
		(trig-step (- sign) (* xi x2) (* fi fs (+ fs 1.0)) x2 (+ fs 2.0) (+ res (/ (* sign xi) fi)))
		res))
(define(sin x) (trig-step 1.0 x 1.0 (* x x) 2.0 0.0))
(define(cos x) (- 1.0 (trig-step 1.0 (* x x) 2.0 (* x x) 3.0 0.0)))
(define(env-syms env)
	(if(null? env) '()
		(if(null? (car env))
			(env-syms (cdr env))
			(cons (car (car env)) (env-syms (cdr env))))))
(define(env-vals env)
	(if(null? env) '()
		(if(null? (car env))
			(env-vals (cdr env))
			(cons (cdr (car env)) (env-vals (cdr env))))))

(define threads '())

(define(append-thread fn)
	(set! threads (append threads (list fn))))

(define(run-threads)
	(if(null? threads) '()
		((lambda()
			(define fn (car threads))
			(set! threads (cdr threads))
			(fn)))))

(define(thread-yield)
	(define (yield! fn)
		(append-thread fn)
		(run-threads))
	(call-with-current-continuation yield!))

(define(start-thread fn . args)
	(append-thread (lambda() (apply fn args) (run-threads))))

(define(prloop msg seq)
	(print 1 msg seq "\n")
	(thread-yield)
	(if(less? seq 1) '()
		(prloop msg (- seq 1))))

