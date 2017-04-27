
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <math.h>
#include "basiclisp.h"

int
main(int argc, char *argv[])
{
	Mach m;
	ref_t list;
	size_t i;

	memset(&m, 0, sizeof m);
	lispinit(&m);

	for(i = 1; i < (size_t)argc; i++){
		FILE *fp;
		fp = fopen(argv[i], "rb");
		if(fp == NULL){
			fprintf(stderr, "cannot open %s\n", argv[i]);
			return 1;
		}
		for(;;){
			m.expr = listparse(&m, fp, 1);
			if(iserror(&m, m.expr))
				break;
			vmcall(&m, INS_RETURN, INS_EVAL);
			while(vmstep(&m) == 1)
				;
			m.expr = NIL;
			m.valu = NIL;
		}

		fclose(fp);
	}
	if(argc == 1){
		for(;;){
			printf("> "); fflush(stdout);
			m.expr = listparse(&m, stdin, 1);
			if(iserror(&m, m.expr))
				break;
			vmcall(&m, INS_RETURN, INS_EVAL);
			while(vmstep(&m) == 1){
				fprintf(stderr, "call-external: ");
				m.valu = vmload(&m, m.expr, 1);
				fprintf(stderr, "\n");
			}
			if(iserror(&m, m.valu))
				break;
			m.valu = NIL;
			m.expr = NIL;
		}
	}
	return 0;
}