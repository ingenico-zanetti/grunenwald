#define _LARGEFILE64_SOURCE
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <math.h>
#include <getopt.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

/* Parse S according to FORMAT and store binary time information in TP.
   The return value is a pointer to the first unparsed character in S.  */
extern char *strptime (const char *__restrict __s,
		       const char *__restrict __fmt, struct tm *__tp)
     __THROW;



static unsigned int logedMagLUT[256][256];

static void LUTInit(void){
	for(int i = 0 ; i < 256 ; i++){
		int centered_i = i -128;
		for(int q = 0 ; q < 256 ; q++){
			int centered_q = q - 128;
			unsigned int magP2 = ((centered_i * centered_i) + (centered_q * centered_q));
			float mag = sqrtf((float)magP2);
			if(mag > 1.0f){
				logedMagLUT[i][q] = (int)logf(mag);
			}else{
				logedMagLUT[i][q] = 0;
			}
		}
	}
}

typedef struct {
	int size;
	int index;
	int *data;
	int somme;
	int previousSomme;
	int average;
} SlidingWindow;

static void slidingWindowInit(SlidingWindow *w, int size, int initialValue){
	w->size = size;
	if(size > 1){
		w->index = w->size - 1;
		w->data = (int*)calloc(w->size, sizeof(int));
		for(int i = 0 ; i < w->size ; i++){
			w->data[i] = initialValue;
		}
		w->somme = w->size * initialValue;
	}else{
		w->data = NULL;
		w->somme = initialValue;
	}
}

static void slidingWindowUpdate(SlidingWindow *w, int newData){
	if(w->size > 1){
		w->previousSomme = w->somme;
		w->somme -= w->data[w->index];
		w->somme += newData;
		w->data[w->index] = newData;
		if(0 == w->index){
			w->index = w->size - 1;
		}else{
			w->index--;
		}
		w->average = w->somme / w->size;
	}else{
		w->somme = newData;
		w->average = newData;
	}
}

static void slidingWindowFree(SlidingWindow *w){
	if(w->data){
		free(w->data);
	}
}

typedef void(*OutputFunction)(int , int);

typedef struct iq_sample {
	unsigned char I;
	unsigned char Q;
} iq_sample;

typedef struct {
	SlidingWindow powerFilter;
	SlidingWindow phaseFilter;
	iq_sample     previousSample;
	long long int sampleCount;
	int sampleRate;
	int positives;
	int nuls;
	int negatives;
} FMDemoder;

void FMDemoderReset(FMDemoder *decoder){
	decoder->previousSample.I = decoder->previousSample.Q = 128;
	decoder->positives = 0;
	decoder->nuls = 0;
	decoder->negatives = 0;
}

void FMDemoderInit(FMDemoder *decoder, int sampleRate, int powerFilterSize, int phaseFilterSize, int inputFilterSize){
	FMDemoderReset(decoder);

	// Phase filter
	slidingWindowInit(&(decoder->phaseFilter), phaseFilterSize, 0);

	// Power threshold and filter
	slidingWindowInit(&(decoder->powerFilter), powerFilterSize, 0);

	decoder->sampleCount = 0LL;
	decoder->sampleRate = sampleRate;
}

void FMDemoderFree(FMDemoder *decoder){
	slidingWindowFree(&(decoder->phaseFilter));
	slidingWindowFree(&(decoder->powerFilter));
}

int crossProduct(struct iq_sample *ancien, struct iq_sample *nouveau){
	int u1 = (ancien->I - 128);
	int u2 = (ancien->Q - 128);
	int v1 = (nouveau->I - 128);
	int v2 = (nouveau->Q - 128);
	int value = u1 * v2 - u2 * v1;
// fprintf(stdout, "||<%4d,%4d>x<%4d,%4d>||=%6d", u1, u2, v1, v2, value);
	return(value);
}

unsigned char intToUChar(int value){
	if(value < -128){
		value = -128;
	}else if(value > 127){
		value = 127;
	}
	return((unsigned char)(value + 128));
}

int uCharToInt(unsigned char in){
	int result = (int)in;
	result -= 128;
	return(result);
}


int FMDemoderUpdate(FMDemoder *decoder, iq_sample *new, int powerThreshold){
	iq_sample filtered = {.I = new->I, .Q = new->Q};
	int output = 0;
	
	int logedMag = logedMagLUT[filtered.I][filtered.Q];
	slidingWindowUpdate(&decoder->powerFilter, logedMag);
	
	int deltaPhase = crossProduct(&(decoder->previousSample), &filtered);
	decoder->previousSample = filtered;

	slidingWindowUpdate(&(decoder->phaseFilter), deltaPhase);

	if(decoder->powerFilter.average > powerThreshold){
		int decision = decoder->phaseFilter.somme;
		if(0 == decision){
			decision = decoder->phaseFilter.previousSomme;
		}
		if(decision < 0){
			output = -1;
		}else{
			output = +1;
		}
	}
	decoder->sampleCount++;
#if 0
	iq_sample debug = { intToUChar(decoder->powerFilter.average), intToUChar(decoder->phaseFilter.somme) };
	fwrite(&debug, 1, sizeof(debug), stderr);
#endif
	return output;
}

struct BitAndDuration {
	int bitValue;
	int bitLength;
	uint64_t sampleCount;
};

struct FrameDecoder {
	struct BitAndDuration *syncPattern;
	int syncPatternMaxLength;
	int syncPatternLength;
	struct BitAndDuration *dataPattern;
	int dataPatternMaxLength;
	int dataPatternLength;
	int dataStartIndex;
};

struct FrameDecoder *frameDecoderAlloc(int syncPatternMaxBitLength, int dataPatternMaxBitLength){
	struct FrameDecoder *decoder = (struct FrameDecoder*)calloc(1, sizeof(struct FrameDecoder));
	if(decoder){
		void *pattern = calloc(syncPatternMaxBitLength, sizeof(struct BitAndDuration));
		if(pattern){
			decoder->syncPattern = (struct BitAndDuration*)pattern;
			pattern = calloc(dataPatternMaxBitLength, sizeof(struct BitAndDuration));
			if(pattern){
				decoder->dataPattern = (struct BitAndDuration*)pattern;
				decoder->syncPatternMaxLength = syncPatternMaxBitLength;
				decoder->syncPatternLength = 0;
				decoder->dataPatternMaxLength = dataPatternMaxBitLength;
				decoder->dataPatternLength = 0;
			}else{
				free(decoder->syncPattern);
				free(decoder);
				decoder = NULL;
			}
		}else{
			free(decoder);
			decoder = NULL;
		}
	}
	return(decoder);
};

void frameDecoderFree(struct FrameDecoder *decoder){
	if(decoder){
		if(decoder->syncPattern){
			free(decoder->syncPattern);
		}
		if(decoder->dataPattern){
			free(decoder->dataPattern);
		}
		free(decoder);
	}
}

void frameDecoderReset(struct FrameDecoder *decoder){
	if(decoder){
		decoder->dataPatternLength = 0;
	}
}

int frameDecoderAddSyncBit(struct FrameDecoder *decoder, int bitValue, int bitLength){
	if(decoder->syncPatternLength < decoder->syncPatternMaxLength){
		decoder->syncPattern[decoder->syncPatternLength++] = (struct BitAndDuration){bitValue, bitLength};
	}else{
		return 1;
	}
	return 0;
}

void dumpPattern(const char *title, struct BitAndDuration *pattern, int length){
	if(title){
		fprintf(stdout, "%s(%d): ", title, length);
	}else{
		fprintf(stdout, "(%d): ", length);
	}
	for(int i = 0 ; i < length ; i++){
		unsigned char bitValue = pattern[i].bitValue;
		unsigned char bitLength = pattern[i].bitLength;
		fprintf(stdout, "%02X.%02X|", bitValue, bitLength);
	}
	fputc('\n', stdout);
}

void frameDecoderDumpSyncPattern(struct FrameDecoder *decoder){
	dumpPattern("SYNC_PATTERN", decoder->syncPattern, decoder->syncPatternLength);
}

int patternCompare(struct BitAndDuration *sync, struct BitAndDuration *data, int length){
	struct BitAndDuration *syncPtr = sync;
	struct BitAndDuration *dataPtr = data;
	int i = 0;
	while(i++ < length){
		if((syncPtr->bitValue != dataPtr->bitValue)||(syncPtr->bitLength != dataPtr->bitLength)){
			return i;
		}
		syncPtr++;
		dataPtr++;
	}
	return 0;
}

int frameDecoderMatchSyncPattern(struct FrameDecoder *decoder){
	int maxOffset = decoder->dataPatternLength - decoder->syncPatternLength;
	if(maxOffset > 0){
		int offset = 0;
		for(;;){
			if(offset <= maxOffset){
				// fprintf(stdout, "%s(): try at offset %i" "\n", __func__, offset);
				int diffOffset = patternCompare(decoder->syncPattern, decoder->dataPattern + offset, decoder->syncPatternLength);
				if(0 == diffOffset){
					// fprintf(stdout, "%s(): match at offset %i" "\n", __func__, offset);
					decoder->dataStartIndex = (offset + decoder->syncPatternLength);
					return(1);
				}else{
					// fprintf(stdout, "%s(): sequences differ at offset %i" "\n", __func__, diffOffset);
					// dumpPattern("sync: ", decoder->syncPattern, diffOffset);
					// dumpPattern("data: ", decoder->dataPattern + offset, diffOffset);
				}
			}else{
				break;
			}
			offset++;
		}
	}
	// fprintf(stdout, "%s(): NO MATCH" "\n", __func__);
	return(0);
}

enum Parity {
	PARITY_NONE,
	PARITY_EVEN,
	PARITY_ODD,
	PARITY_DONT_CARE
};

enum SerialDecoderState {
	SERIAL_DECODER_STATE_WAIT_FOR_START = -1,
};

struct SerialDecoder {
	enum Parity parityKind;
	int parityBit;
	int stopBits;
	int dataBits;
	int startBits;
	int state;
	int ones;
	uint16_t data;
};

void serialDecoderInit(struct SerialDecoder *decoder, int startBits, int dataBits, enum Parity parityKind, int stopBits){
	decoder->startBits = startBits;
	decoder->dataBits = dataBits;
	decoder->parityKind = parityKind;
	decoder->stopBits = stopBits;
	decoder->state = SERIAL_DECODER_STATE_WAIT_FOR_START;
	decoder->data = -1;
	decoder->ones = 0;
}

int serialDecoderPush(struct SerialDecoder *decoder, int bitValue, int bitCount, uint64_t startCounter){
	// fprintf(stdout, "%s(value=%i, length=%i)" "\n", __func__, bitValue, bitCount);
	int octet = -1;
	for(int i = 0 ; i < bitCount ; i++){
		if(-1 == decoder->state){
			if(-1 == bitValue){
				decoder->state = 0;
				decoder->data = 0;
				decoder->ones = 0;
	// fprintf(stdout, "%s(value=%i) start detected at index=%i)" "\n", __func__, bitValue, i);
			}
		}else{
			if(decoder->state < decoder->dataBits){
#ifdef __LSb_FIRST__
				if(1 == bitValue){
					decoder->data |= (bitValue << (decoder->state));
					decoder->ones++;
				}
#else
				decoder->data <<= 1;
				if(1 == bitValue){
					decoder->data |= 1;
					decoder->ones++;
				}
#endif
			}else if(decoder->state == decoder->dataBits){
				if(PARITY_NONE == decoder->parityKind){
					// This should be a stop bit
					if(-1 == bitValue){
						// fprintf(stdout, "\n" "%s@%d: Framing error @%lu, stop bit not at 1" "\n", __func__, __LINE__, startCounter);
						octet = -2;
					}else{
						octet = decoder->data;
					}
					// fprintf(stdout, "%s@%d: decoded 0x%02X" "\n", __func__, __LINE__, decoder->data);
					decoder->state = -2; // Wait for start bit
				}else{
					// This should be a parity bit
					decoder->parityBit = bitValue;
				}
			}else{
				// This should be a stop bit
				if(-1 == bitValue){
					// fprintf(stdout, "\n" "%s:@%d: Framing error @%lu, stop bit not at 1" "\n", __func__, __LINE__, startCounter);
					octet = -2;
				}else{
					octet = decoder->data;
				}
				// fprintf(stdout, "%s@%d: decoded 0x%02X" "\n", __func__, __LINE__, decoder->data);
				decoder->state = -2; // Wait for start bit
			}
			decoder->state++;
		}
	}
	return octet;
}

#define GRUNENWALD_MAX_FRAME_BYTES (128)

const unsigned char xorPattern[58] = {
	0, 0,
	0x55,
	0x55,
	0x55,
	0x55,
	0x55,
	0x55,
	0x55,
	0x55,
	0x55,
	0x55,
	0x55,
	0x55,
	0x55,
	0x55,
	0x55,
	0x55,
	0x55,
	0x55,
	0x55,
	0x55,
	0x55,
	0x55,
	0x55,
	0x55,
	0x55,
	0x55,
	0x55,
	0x55,
	0xAA,
	0x55,
	0xAA,
	0x55,
	0xAA,
	0xAA,
	0xAA,
	0x55,
	0x55,
	0x55,
	0xAA,
	0x55,
	0x55,
	0xAA,
	0x55,
	0x55,
	0x55,
	0x55,
	0x55,
	0x55,
	0x55,
	0x55,
	0x55,
	0x55,
	0x55,
	0, 0, 0
};

void serialDecode(struct FrameDecoder *decoder){
	// fprintf(stdout, "%s(firstDataSample@%lu, firstActualDataSample@%lu)" "\n", __func__, decoder->dataPattern[0].sampleCount, decoder->dataPattern[decoder->dataStartIndex].sampleCount); 
	struct SerialDecoder serialDecoder;
	serialDecoderInit(&serialDecoder, 1, 8, PARITY_DONT_CARE, 1); // looks like stop is actually 3 bits, but this can also be 1-stop+2-idle or 2-stop+1-idle
	// Decode serial data start in [dataStartIndex .. dataPatternLength - 1]
	int abort = 0;
	unsigned char decodedFrame[GRUNENWALD_MAX_FRAME_BYTES];
	int length = 0;
	for(int index = decoder->dataStartIndex ; index < decoder->dataPatternLength ; index++){
		int octet = serialDecoderPush(&serialDecoder, decoder->dataPattern[index].bitValue, decoder->dataPattern[index].bitLength, decoder->dataPattern[index].sampleCount);
		if((0 <= octet) && (length < sizeof(decodedFrame))){
			decodedFrame[length++] = (unsigned char)octet;
		}else if(-2 == octet){
			// fprintf(stdout, "Framing error detected @%lu, aborting" "\n", decoder->dataPattern[index].sampleCount);
			abort = 1;
			break;
		}
	}
	if(0 == abort){
		// Check Frame
		// Sync Word seams to be 0xF1A5
		if(length > 3){
			if(0xF1 == decodedFrame[length - 1]){
				// Complet frame
				if((0x8F == decodedFrame[0]) && (0xA5 == decodedFrame[1])){
					// Looks like a valide frame
					fprintf(stdout, "%s: score (l=%02d), ", __func__, length);
					for(int i = 0 ; i < length ; i++){
						fprintf(stdout, "%02X ", decodedFrame[i]);
					}
					fputc('\n', stdout);
#ifdef __XOR__
					fprintf(stdout, "%s: _XOR_ (l=%02d), ", __func__, length);
					for(int i = 0 ; i < length ; i++){
						fprintf(stdout, "%02X ", decodedFrame[i] ^ 0x55);
					}
					fputc('\n', stdout);
#endif
				}
				if((0x8F == decodedFrame[0]) && (0x56 == decodedFrame[1])){
					// Looks like a valide frame
					fprintf(stdout, "%s: clock (l=%02d), ", __func__, length);
					for(int i = 0 ; i < length ; i++){
						fprintf(stdout, "%02X ", decodedFrame[i]);
					}
					fputc('\n', stdout);
				}
			}
		}
	}
}

int frameDecoderUpdate(struct FrameDecoder *decoder, int bitValue, int bitLength, uint64_t sampleCount){
	// fprintf(stdout, "%s(%i, %i): dataPatternLength=%i" "\n", __func__, bitValue, bitLength, decoder->dataPatternLength);
	if(0 == bitValue){
		// try do decode what we have
		if(frameDecoderMatchSyncPattern(decoder)){
			serialDecode(decoder);
			// fputc('\n', stdout);
		}
		frameDecoderReset(decoder);
	}else{
		if(decoder->dataPatternLength < decoder->dataPatternMaxLength){
			decoder->dataPattern[decoder->dataPatternLength++] = (struct BitAndDuration){bitValue, bitLength, sampleCount};
		}
	}
	return 0;
}

int sampleLengthToBitLength(int length, int sampleRate, int bitRate, int *confidence, int shift){
	int evaluation = (int)(((int64_t)bitRate * (int64_t)(length << shift)) / (int64_t)sampleRate);
	int powerOfTwo = (1 << shift);
	int half = ((powerOfTwo) >> 1) - 1;
	int bitLength = (evaluation + half) >> shift;
	int mask = (powerOfTwo) - 1;
	int remainder = evaluation & mask;
	// if remainder is "negative", use "absolute" value of remainder
	if(remainder > half){ // MSb set
		remainder = powerOfTwo - remainder;
	}
	// Confidence must be scaled down the longer the bitLength. We use a log2 function here
	int value = bitLength;
	int log2 = 0;
	for(;;){
		value >>= 1;
		if(value > 0){
			log2++;
		}else{
			break;
		}
	}
	// fprintf(stdout, "bitLength=%i, log2=%i, remainder %i -> ", bitLength, log2, remainder);
	if(bitLength > 1){
		remainder /= bitLength;
	}
	// fprintf(stdout, "%i" "\n", remainder);
	*confidence = remainder;
	return bitLength;
}

#define NB_SAMPLE (1024)

int main(int argc, char *argv[]){
	const char *inputFileName = NULL;
	// const char *outputFileName = NULL;
	int verbose = 0;
	unsigned int sampleRate = 2048000;
	unsigned int bitRate = 39400;

	while (1){
		int option_index = 0;
		static struct option long_options[] = {
		{"inputfile",   required_argument, 0,  'i' },
		{"outputfile",   required_argument, 0,  'o' },
		{"rate",    required_argument, 0,  'r' },
		{"starttime", required_argument, 0, 't' },
		{NULL,         0,                 0,  0 }
		};

		int c = getopt_long(argc, argv, "i:o:r:t:", long_options, &option_index);
		if (c == -1)
		break;

		switch (c) {
			case 'i':
				inputFileName = strdup(optarg);
			break;
			case 'o':
				// outputFileName = strdup(optarg);
			break;
			case 'v':
				verbose++;
			break;
			case 'r':
				sampleRate = strtol(optarg, NULL, 0);
			break;
			case 't':
				break;
			default:
				break;
		}
	}

	int fd = -1;

	FMDemoder fm;
	FMDemoderInit(&fm, sampleRate, 4, 4, 0);

	if(inputFileName){
		if(strcmp(inputFileName, "-")){
			fd = open(inputFileName, O_RDONLY | O_LARGEFILE);
		}else{
			fd = STDIN_FILENO;
		}
	}
	if(fd < 0){
		perror(inputFileName);
		exit(1);
	}

	LUTInit();
	iq_sample in_sample[NB_SAMPLE];

	struct RleEncoder{
		int previousValue;
		int length;
	} rleEncoder = { 0, 0};

	struct FrameDecoder *frameDecoder = frameDecoderAlloc(256, 4096);

	// Build sync pattern
	// Capture suggest up-to 10 0x55 bytes, but worst case scenario is we can decode only 8 because of power ramp
	for(int i = 0 ; i < 8 ; i++){
		frameDecoderAddSyncBit(frameDecoder, +1, 3);
		for(int j = 0 ; j < 4 ; j++){
			frameDecoderAddSyncBit(frameDecoder, -1, 1);
			frameDecoderAddSyncBit(frameDecoder, +1, 1);
		}
		frameDecoderAddSyncBit(frameDecoder, -1, 2);
	}
	frameDecoderAddSyncBit(frameDecoder, +1, 16);
	// frameDecoderDumpSyncPattern(frameDecoder);

	for(;;){
		int lus = read(fd, in_sample, sizeof(in_sample));
		if(lus > 0){
			lus /= sizeof(in_sample[0]);
			for(int i = 0 ; i < lus; i++){
				int demoded = FMDemoderUpdate(&fm, in_sample + i, 1);
				// fprintf(stdout, "%14llu: %i -> %i" "\n", fm.sampleCount, rleEncoder.previousValue, demoded);
				if(rleEncoder.previousValue == demoded){
					rleEncoder.length++;
				}else{
					int confidence;
					int bitLength = sampleLengthToBitLength(rleEncoder.length, sampleRate, bitRate, &confidence, 4);
					// fprintf(stdout, "%14llu: %2i -> %2i, rleEncoder.length %i bitLength %i, confidence %i%c" "\n", fm.sampleCount, rleEncoder.previousValue, demoded, rleEncoder.length, bitLength, confidence, (confidence > 2) ? '!' : ' ');
					if((rleEncoder.previousValue != 0) && (confidence <= 2) && (bitLength > 0)){
						frameDecoderUpdate(frameDecoder, rleEncoder.previousValue, bitLength, (fm.sampleCount - rleEncoder.length));
					}
					if(0 == demoded){
						// fprintf(stdout, "%14llu: ", fm.sampleCount);
						frameDecoderUpdate(frameDecoder, 0, 0, 0);
					}
					rleEncoder.length = 1;
					rleEncoder.previousValue = demoded;
				}
			}
		}else{
			break;
		}

	}
	FMDemoderFree(&fm);
	close(fd);
	return(0);
}

