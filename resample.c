#include <stdlib.h>
#include <stdio.h>

typedef struct {
	unsigned char I;
	unsigned char Q;
}iq_sample_s;

int main(int argc, char *argv[]){
	if(argc > 1){
		int n = atoi(argv[1]);
		if(n > 1){
			iq_sample_s sample;
			int skip = n - 1;
			while(fread(&sample, sizeof(sample), 1, stdin)){
				if(0 == skip){
					fwrite(&sample, sizeof(sample), 1, stdout);
					skip = (n - 1);
				}else{
					skip--;
				}
			}
		}
	}else{
		fprintf(stderr, "Usage %s <resample factor>" "\n", argv[0]);
	}
}


