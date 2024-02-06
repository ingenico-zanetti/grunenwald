#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define BLOCK_SIZE (1204)

typedef struct {
	unsigned char I;
	unsigned char Q;
} u8iq_sample_s;

typedef struct {
	unsigned short somme;
	int logSize;
	int size;
	int index;
	unsigned short *data;
} u16filter_s;

static void u16filterInit(u16filter_s *f, int logSize){
	f->logSize = logSize;
	f->size = (1 << logSize);
	f->data = (unsigned short *)calloc(f->size, sizeof(unsigned short));
	f->index = f->size - 1;
	f->somme = 0;
}

static unsigned short u16filterUpdate(u16filter_s *f, unsigned short sample){
	f->somme -= f->data[f->index];
	f->somme += sample;
	f->data[f->index] = sample;
	if(0 == f->index){
		f->index = f->size - 1;
	}else{
		f->index--;
	}
	return(f->somme >> f->logSize);
}

int main(int argc, char *argv[]){
	int filterLogSize = 2;
	if(argc > 1){
		int arg = atoi(argv[1]);
		if(arg > 0){
			filterLogSize = arg;
		}
	}
	u8iq_sample_s input[BLOCK_SIZE];

	u16filter_s iFilter;
	u16filter_s qFilter;

	// fprintf(stderr, "filterLogSize=%d" "\n", filterLogSize);
	u16filterInit(&iFilter, filterLogSize);
	u16filterInit(&qFilter, filterLogSize);

	for(;;){
		int byteRead = read(STDIN_FILENO, input, sizeof(input));
		if(byteRead > 0){
			int sampleRead = (byteRead >> 1);
			for(int i = 0 ; i < sampleRead ; i++){
				input[i].I = u16filterUpdate(&iFilter, (unsigned short)(input[i].I));
				input[i].Q = u16filterUpdate(&qFilter, (unsigned short)(input[i].Q));
			}
			if(write(STDOUT_FILENO, input, byteRead) < 0){
				break;
			}
		}else{
			break;
		}
	}
}

