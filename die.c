#include <stdio.h>
#include <stdlib.h>

void die(const char * const file, unsigned int line){
	fprintf(stderr,"a runtime error occurred.  see source file %s at line %u\n",file,line);
	exit(-1); }
