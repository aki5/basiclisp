
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
	(sort2 (split ls)))

(define(append head tail)
	(if(null? head) tail
		(cons(car head)(append(cdr head) tail))))
