#include "die.h"
#include <stdio.h>
#include <string.h>

void eatnewline(char * const s){
	if(strlen(s)==0) return;
	if(s[strlen(s)-1]=='\n') s[strlen(s)-1]='\0'; }

void read_settings
	(
		char const * const file,
		char * const sender_comp_id,
		char * const target_comp_id,
		char * const target_sub_id,
		char * const username,
		char * const password,
		char * const account
	){
		FILE * stream;
		char buf[81];
		if(!(stream=fopen(file,"r"))) DIE;
		while(!feof(stream)){
			if(fgets(buf,81,stream)==NULL) break;
			if(sscanf(buf,"SenderCompID=%s",sender_comp_id)==1) continue;
			if(sscanf(buf,"TargetCompID=%s",target_comp_id)==1) continue;
			if(sscanf(buf,"TargetSubID=%s",target_sub_id)==1) continue;
			if(sscanf(buf,"username=%s",username)==1) continue;
			if(sscanf(buf,"password=%s",password)==1) continue;
			if(sscanf(buf,"account=%s",account)==1) continue; }
		fclose(stream); }

