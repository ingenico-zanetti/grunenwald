#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define LINE_MAX (1024)

int main(int argc, char *argv[]){
	char cour[LINE_MAX] = "";
	char prev[LINE_MAX] = "";
	char xor[LINE_MAX] = "";
	char offset[LINE_MAX] = "";

	memset(offset, ' ', sizeof(offset) - 1);
	offset[sizeof(offset) - 1] = '\0';

	int ligne = 0;

	while(fgets(cour, sizeof(cour) - 1, stdin)){
		int i = 0;
		int n = 0;
		while(i < LINE_MAX){
			char c = cour[i];
			char p = prev[i];
			if(('\n' == c) || ('\r' == c)){
				cour[i] = c = '\0';
			}
			if(('\n' == p) || ('\r' == p)){
				prev[i] = p = '\0';
			}
			if(c && p){
				char diff = c ^ p;
				if(diff){
					n++;
					xor[i] = '|';
				}else{
					xor[i] = ' ';
				}
			}else{
				xor[i] = '\0';
				break;
			}
			i++;
		}
		if(n > 0){
			for(int j = 0 ; j < (i - 26) ; j++){
				if(1 == (j % 3)){
					sprintf(offset + j + 19, "+%02d ", j / 3);
				}
			}
			if(0 == ligne){
				fprintf(stdout, "%s" "\n", prev);
			}
			fprintf(stdout, "%s" "\n", xor);
			fprintf(stdout, "%s" "\n", offset);
			fprintf(stdout, "%s" "\n", xor);
			fprintf(stdout, "%s" "\n", cour);
			ligne++;
		}
		strcpy(prev, cour);
	}
	return(0);
}


