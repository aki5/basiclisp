(define loop (lambda(n)
	(if (equal? n 0)
		#t
		(loop (- n 1))
	)
))
(loop 100000)
(print 1 "\n")

