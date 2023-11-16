#define _LARGEFILE64_SOURCE
#include <stdlib.h>
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
	int average;
} SlidingWindow;

static void slidingWindowInit(SlidingWindow *w, int size){
	w->size = size;
	if(size > 1){
		w->index = w->size - 1;
		w->data = (int*)calloc(w->size, sizeof(int));
	}else{
		w->data = NULL;
	}
	w->somme = 0;
}

static void slidingWindowUpdate(SlidingWindow *w, int newData){
	if(w->size > 1){
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
	OutputFunction signalPowerCallBack;
	OutputFunction crossProductCallBack;
	OutputFunction outputCallBack;
	int sampleRate;
} FMDecoder;

void FMDecoderReset(FMDecoder *decoder){
	decoder->previousSample.I = decoder->previousSample.Q = 128;
}

void FMDecoderInit(FMDecoder *decoder, int sampleRate, int powerFilterSize, int phaseFilterSize, int inputFilterSize){
	FMDecoderReset(decoder);

	// Phase filter
	slidingWindowInit(&(decoder->phaseFilter), phaseFilterSize);

	// Power threshold and filter
	slidingWindowInit(&(decoder->powerFilter), powerFilterSize);

	decoder->sampleCount = 0LL;
	decoder->signalPowerCallBack = NULL;
	decoder->crossProductCallBack = NULL;
	decoder->outputCallBack = NULL;
#if 0
	// 1Hz lookup table
	// For higher frequency, simply skip some sample
	decoder->sinusLUT = (float*)calloc(sampleRate, sizeof(float));
	decoder->cosineLUT = (float*)calloc(sampleRate, sizeof(float));
	for(int i = 0 ; i < sampleRate ; i++){
		float angle = (float)i * 2 * M_PI / (float)sampleRate;
		decoder->cosineLUT[i] = cos(angle);
		decoder->sinusLUT[i]  = sin(angle);
	}
	decoder->shiftFrequency = 0;
	decoder->shiftSkip = 0;
	decoder->shiftIndex = 0;
#endif
	decoder->sampleRate = sampleRate;
}

#if 0
void FMDecoderSetShiftFrequency(FMDecoder *decoder, int frequency){
	fprintf(stderr, "%s(%d)" "\n", __func__, frequency);
	if(frequency < 0 && (-frequency > decoder->sampleRate)){
		frequency = -decoder->sampleRate;
	}
	if(frequency > 0 && (frequency > decoder->sampleRate)){
		frequency = decoder->sampleRate;
	}
	decoder->shiftFrequency = frequency;
	decoder->shiftSkip = frequency;
	fprintf(stderr, "%s:sF=%d,sS=%d" "\n", __func__, decoder->shiftFrequency, decoder->shiftSkip);
}

/*
 * in and out can point to the same sample
 */
void FMDecoderShiftFrequency(FMDecoder *decoder, iq_sample *in, iq_sample *out){
	if(decoder->shiftFrequency){
		float a = ((float)in->I - 128.0) / 128.0; // normalize in [-1 .. 1]
		float b = ((float)in->Q - 128.0) / 128.0; // normalize in [-1 .. 1]
		float c = decoder->cosineLUT[decoder->shiftIndex];
		float d = decoder->sinusLUT[decoder->shiftIndex];
		out->I = 128 + 128.0 * (a * c - b * d);
		out->Q = 128 + 128.0 * (a * d + b * c);
		decoder->shiftIndex += decoder->shiftSkip;
		if(decoder->shiftIndex < 0){
			decoder->shiftIndex += decoder->sampleRate;
		}
		if(decoder->shiftIndex >= decoder->sampleRate){
			decoder->shiftIndex -= decoder->sampleRate;
		}
	}else{
		*out = *in;
	}
}
#endif

void FMDecoderFree(FMDecoder *decoder){
	slidingWindowFree(&(decoder->phaseFilter));
	slidingWindowFree(&(decoder->powerFilter));
}

int crossProduct(struct iq_sample *ancien, struct iq_sample *nouveau){
	int u1 = (ancien->I - 128);
	int u2 = (ancien->Q - 128);
	int v1 = (nouveau->I - 128);
	int v2 = (nouveau->Q - 128);
	int value = u1 * v2 - u2 * v1;
// fprintf(stderr, "||<%4d,%4d>x<%4d,%4d>||=%6d", u1, u2, v1, v2, value);
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

static int uCharToInt(unsigned char in){
	int result = (int)in;
	result -= 128;
	return(result);
}


void FMDecoderUpdate(FMDecoder *decoder, iq_sample *new){
	iq_sample output = {.I = 128, .Q = 128};
	iq_sample filtered = {.I = new->I, .Q = new->Q};
	
	int logedMag = logedMagLUT[filtered.I][filtered.Q];
	slidingWindowUpdate(&decoder->powerFilter, logedMag);
	
	int deltaPhase = crossProduct(&(decoder->previousSample), &filtered);
	decoder->previousSample = filtered;

	slidingWindowUpdate(&(decoder->phaseFilter), deltaPhase);
	if(decoder->crossProductCallBack){
		decoder->crossProductCallBack(decoder->phaseFilter.average, deltaPhase);
	}

	if(decoder->powerFilter.average > 1){
		if(decoder->phaseFilter.somme < 0){
			output.I = 128 - 100;
		}else if(decoder->phaseFilter.somme > 0){
			output.I = 128 + 100;
		}
	}else{
		output.I = 128;
	}

	if(decoder->outputCallBack){
		decoder->outputCallBack(uCharToInt(output.I), decoder->powerFilter.average);
	}
	decoder->sampleCount++;
}

typedef enum {
	PARITY_NONE,
	PARITY_EVEN,
	PARITY_ODD,
	PARITY_DONT_CARE
} SerialDecoderParity;

typedef enum {
	STOP_1_BIT,
	STOP_1DOT5_BIT,
	STOP_2BITS
} SerialDecoderStopBits;

struct SerialDecoder;

typedef void(*SerialDecoderCallBack)(struct SerialDecoder*);

typedef struct SerialDecoder{
	// Configuration
	unsigned int dataBits;
	SerialDecoderParity parity;
	SerialDecoderStopBits stopBits;
	unsigned int expectedBits;
	unsigned int baudrate;
	unsigned int samplePerBit;
	unsigned int idleSamples;

	// Decoding
	unsigned long long int idleSampleCounter;
	unsigned int sampleCounter;
	int bitCounter;
	char bits[16];

	// Output decoded data
	SerialDecoderCallBack startOfFrameCallBack;
	// Unchecked
	SerialDecoderCallBack uncheckedDataCallBack;
	// Check
	SerialDecoderCallBack checkedDataCallBack;

	unsigned long long int absoluteSampleCounter;
	unsigned long long int lastStartOfFrameSampleCounter;
	unsigned long int sampleRate;
}SerialDecoder;

void SerialDecoderReset(SerialDecoder *sd){
	sd->idleSampleCounter = 0ULL;
	sd->sampleCounter = 0;
	sd->bitCounter = -1;
	memset(sd->bits, 0, sizeof(sd->bits));
}

void SerialDecoderInit(SerialDecoder *sd, unsigned int dataBits, int parityKind, int stopBits, int baudRate, int sampleRate){
	sd->dataBits = dataBits;
	sd->parity   = parityKind;
	sd->stopBits = stopBits;
	sd->expectedBits = 1 + dataBits + ((PARITY_NONE == parityKind) ? 0 : 1) + ((STOP_1_BIT == stopBits) ? 1 : 2);
	sd->baudrate = baudRate;
	sd->samplePerBit = sampleRate / baudRate;
	// fprintf(stderr, "samplePerBit=%d" "\n", sd->samplePerBit);
	sd->sampleRate = sampleRate;

	sd->idleSamples = 2 * dataBits * sd->samplePerBit;

	sd->startOfFrameCallBack = NULL;
	sd->uncheckedDataCallBack = NULL;
	sd->checkedDataCallBack = NULL;

	sd->absoluteSampleCounter = 0ULL;
	sd->lastStartOfFrameSampleCounter = 0ULL;

	SerialDecoderReset(sd);
}

void SerialDecoderPrintFrame(SerialDecoder *sd){
	int ones = 0;
	unsigned char value = 0;
	fprintf(stderr, "%s: 0b", __func__);
	for(int i = 0 ; i < sd->bitCounter ; i++){
		fputc((sd->bits[i]) ? '1' : '0', stderr);
	}
	fprintf(stderr, ", START=%d, data=0b", sd->bits[0]);
	for(int i = 1; i <= sd->dataBits ; i++){
		char c = '0';
		if(sd->bits[i]){
		   c = '1';
		   value |= (1 << (i - 1));
		   ones++;
		}
		fputc(c, stderr);
	}
	fprintf(stderr, " (0x%02X, %c)", value, isprint(value) ? value : '.');
	if(sd->parity != PARITY_NONE){
		int parityBit = sd->bits[1 + sd->dataBits];
		if(parityBit){
			ones++;
		}
		fprintf(stderr, ", PARITY=%d(%c), STOP=%d", parityBit, ones&1?'O':'E', sd->bits[2 + sd->dataBits]);
	}else{
		fprintf(stderr, ", STOP=%d", sd->bits[1 + sd->dataBits]);
	}
	fputc('\n', stderr);
}

int checkData(SerialDecoder *sd){
	if(sd->bits[0]){ // START bit
		return(1);
	}
	int stopBitIndex = 1 + sd->dataBits;
	if(PARITY_NONE != sd->parity){
		stopBitIndex++;
		if(PARITY_DONT_CARE != sd->parity){
			int ones = 0;
			for(int i = 1; i <= sd->dataBits + 1 ; i++){
				if(sd->bits[i]){
				   ones++;
				}
			}
			if(PARITY_ODD == sd->parity){
				if(0 == (ones & 1)){
					return(1);
				}
			}
			if(PARITY_EVEN == sd->parity){
				if(1 == (ones & 1)){
					return(1);
				}
			}
		}
		if(0 == sd->bits[stopBitIndex]){ // STOP bit
			return(1);
		}
	}
	return(0);
}

void SerialDecoderOutput(SerialDecoder *sd){
	if(sd->uncheckedDataCallBack){
		sd->uncheckedDataCallBack(sd);
	}
	if(sd->checkedDataCallBack){
		if(0 == checkData(sd)){
			sd->checkedDataCallBack(sd);
		}
	}
}

void SerialDecoderSOFCallBack(SerialDecoder *sd){
	fprintf(stderr, "\n" "%s:SOF after %llu idle samples" "\n", __func__, sd->idleSampleCounter);
}

void SerialDecoderUpdate(SerialDecoder *sd, int sample){
	// fprintf(stderr, "%s(%14lld;%d)" "\n", __func__, sd->absoluteSampleCounter, sample);
	if(sd->bitCounter < 0){
		if(sample >= 0){ // 0 means no enough energy, so it treated just as >0 for idle phase
			sd->idleSampleCounter++;
		}else{
			if(sd->idleSampleCounter > (unsigned long long)sd->idleSamples){
				sd->bitCounter = 0;
				if(sd->idleSampleCounter > (sd->expectedBits * sd->samplePerBit * 2)){
					if(sd->startOfFrameCallBack){
						sd->startOfFrameCallBack(sd);
					}
					sd->lastStartOfFrameSampleCounter = sd->absoluteSampleCounter;
				}
			}
			sd->idleSampleCounter = 0ULL;
			sd->sampleCounter = sd->samplePerBit / 2; // sample at middle of bit
		}
	}else{
		sd->sampleCounter--;
		if(0 == sd->sampleCounter){
			int sampledBit = (sample > 0) ? 1 : 0;
			if(sampledBit && (0 == sd->bitCounter)){
				// fprintf(stderr, "Framing error" "\n");
				SerialDecoderReset(sd);
				sd->idleSamples = 1;
			}else{
				sd->sampleCounter = sd->samplePerBit; // next sample at middle of next bit
				sd->bits[sd->bitCounter] = sampledBit;
				sd->bitCounter++;
				if(sd->bitCounter == sd->expectedBits){
					SerialDecoderOutput(sd);
					SerialDecoderReset(sd);
					sd->idleSamples = 1;
				}
			}
		}
	}
	sd->absoluteSampleCounter++;
}

#define GRUNENWALD_MAX_DATA (256)

typedef struct Grunenwald {
	int offset;
	unsigned char data[GRUNENWALD_MAX_DATA];
	unsigned long long startOfFrameSampleCounter;
} Grunenwald;

static void GrunenwaldReset(Grunenwald *g){
	g->offset = 0;
}

static void GrunenwaldInit(Grunenwald *g){
	GrunenwaldReset(g);
}

static void GrunenwaldUpdate(Grunenwald *g, SerialDecoder *sd, unsigned char octet){
	if((0 == g->offset) && (NULL != sd)){
		g->startOfFrameSampleCounter = sd->lastStartOfFrameSampleCounter;
	}
	if(g->offset < GRUNENWALD_MAX_DATA){
		g->data[g->offset] = octet;
		g->offset++;
	}
}

void GrunenwaldDumpDataHex(Grunenwald *g, int start, int stop){
	char nibbleToCharUpperCase(unsigned char nibble){
		nibble &= 0x0F;
		if(nibble <= 9){
			return('0' + nibble);
		}
		return(('A' - 10) + nibble);
	}

	unsigned char *p = g->data + start;
	int i = (stop - start) + 1;
	while(i-- > 0){
		unsigned char octet = *p++;
		char chaine[4];
		chaine[0] = nibbleToCharUpperCase(octet >> 4);
		chaine[1] = nibbleToCharUpperCase(octet);
		chaine[2] = (i & 3) ? ' ' : '|';
		chaine[3] = '\0';
		fwrite(chaine, 3, 1, stdout);
	}
}

static struct tm startTime;
static int hasStartTime = 0;

static void printTimeStamp(Grunenwald *g, SerialDecoder *sd){
	if(hasStartTime){
		unsigned int sampleSeconds = (unsigned int)(g->startOfFrameSampleCounter / sd->sampleRate);
		unsigned int sampleRemainder = (unsigned int)(g->startOfFrameSampleCounter - (sampleSeconds * sd->sampleRate));
		sampleSeconds += (startTime.tm_hour * 3600 + startTime.tm_min * 60 + startTime.tm_sec);
		unsigned int minutes = sampleSeconds / 60;
		unsigned int reste_secondes = sampleSeconds - (60 * minutes);
		unsigned int heures = minutes / 60;
		unsigned int reste_minutes = minutes - (heures * 60);
		fprintf(stdout, "%4d:%02d:%02d+%8u: ", heures, reste_minutes, reste_secondes, sampleRemainder);
	}else{
		struct timeval tv;
		gettimeofday(&tv, NULL);
		fprintf(stdout, "%10ld.%06ld  : ", tv.tv_sec, tv.tv_usec);
	}
}

static int nibblesToDigit(unsigned char nibbles){
	switch(nibbles){
		case 0x55:
			return('0');
		case 0x56:
			return('1');
		case 0x59:
			return('2');
		case 0x5A:
			return('3');
		case 0x65:
			return('4');
		case 0x66:
			return('5');
		case 0x69:
			return('6');
		case 0x6A:
			return('7');
		case 0x95:
			return('8');
		case 0x96:
			return('9');
		default:
			return(' ');
	}
}

static void GrunenwaldDecodeVolleyball(Grunenwald *g){
	int right_score_digit_0 = nibblesToDigit(g->data[50]); // LSB
	int right_score_digit_1 = nibblesToDigit(g->data[51]); // MSB

	int left_to_digit = nibblesToDigit(g->data[32]); //  bit 0 is first TO, bit 1 second TO and bit 2 is serve
	int right_to_digit = nibblesToDigit(g->data[31]); // bit 0 is first TO, bit 1 second TO and bit 2 is serve

	int left_to_mask = nibblesToDigit(g->data[26]); //  bit 0 is first TO, bit 1 second TO (used during TO countdown, probably a blink mask)
	int right_to_mask = nibblesToDigit(g->data[25]); // bit 0 is first TO, bit 1 second TO (used during TO countdown, probably a blink mask)

	int left_score_digit_0 = nibblesToDigit(g->data[53]); // LSB
	int left_score_digit_1 = nibblesToDigit(g->data[54]); // MSB
	int set_digit = nibblesToDigit(g->data[45]); // count set(s) already played
	int left_set_digit = nibblesToDigit(g->data[56]); // count set(s) won by left
	int right_set_digit = nibblesToDigit(g->data[49]); // count set(s) won by right
	int set1[4];
	for(int i = 0 ; i < 4 ; i++){
		set1[i] = nibblesToDigit(g->data[61 + i]);
	}
	int set2[4];
	for(int i = 0 ; i < 4 ; i++){
		set2[i] = nibblesToDigit(g->data[57 + i]);
	}
	int timer[4];
	for(int i = 0 ; i < 4 ; i++){
		timer[i] = nibblesToDigit(g->data[44 - i]);
	}
	fprintf(stdout, "st[%c %c%c%c:%c%c%c %c] [%c] sc[%c%c %c%c] 1[%c%c:%c%c] 2[%c%c:%c%c] TO[%c %c]", left_set_digit, (left_to_mask & 0x03) ? 'T' : ' ', timer[0], timer[1], timer[2], timer[3], (right_to_mask & 0x03) ? 'T' : ' ',right_set_digit, set_digit, left_score_digit_1, left_score_digit_0, right_score_digit_1, right_score_digit_0, set1[3], set1[2], set1[1], set1[0], set2[3], set2[2], set2[1], set2[0], left_to_digit, right_to_digit);
}

void GrunenwaldSOF(Grunenwald *g, SerialDecoder *sd){
	// Analyse previous frame (if any)
	int length = g->offset;
	// fprintf(stderr, "%s:length=%d, ", __func__, length);
	if(length > 21){
		if(memcmp(g->data + 5, "\x55\x55\x55\x55\x55\x55\x55\xF1", 8)){
			// fprintf(stderr, ": Sync pattern not found" "\n");
		}else{
			int valide = 0;
			unsigned char kind = g->data[13];
			if((0x6A == kind) && (28 == length)){
				printTimeStamp(g, sd);
				GrunenwaldDumpDataHex(g, 0, 27);
				valide = 1;
			}
			if((0xA5 == kind) && (70 == length)){
				printTimeStamp(g, sd);
				GrunenwaldDumpDataHex(g, 0, 69);
				fprintf(stdout, ": ");
				GrunenwaldDecodeVolleyball(g);
				valide = 1;
			}
			if(1 == valide){
				fputc('\n', stdout);
			}
		}
	}else{
		// fprintf(stderr, ": ShortFrame:%d" "\n", g->offset);
	}
	GrunenwaldReset(g);
}

#define NB_SAMPLE (1024)

int main(int argc, char *argv[]){
	const char *inputFileName = NULL;
	const char *outputFileName = NULL;
	const char *powerFileName = NULL;
	const char *crossProductFileName = NULL;
	int verbose = 0;
	unsigned int sampleRate = 2048000;
	memset(&startTime, 0, sizeof(startTime));

	while (1){
		int option_index = 0;
		static struct option long_options[] = {
		{"inputfile",   required_argument, 0,  'i' },
		{"outputfile",   required_argument, 0,  'o' },
		{"powerfile",   required_argument, 0,  'p' },
		{"crossproductfile",   required_argument, 0,  'c' },
		{"rate",    required_argument, 0,  'r' },
		{"starttime", required_argument, 0, 't' },
		{NULL,         0,                 0,  0 }
		};

		int c = getopt_long(argc, argv, "i:o:p:c:r:t:", long_options, &option_index);
		if (c == -1)
		break;

		switch (c) {
			case 'i':
				inputFileName = strdup(optarg);
			break;
			case 'o':
				outputFileName = strdup(optarg);
			break;
			case 'p':
				powerFileName = strdup(optarg);
			break;
			case 'c':
				crossProductFileName = strdup(optarg);
				fopen(crossProductFileName, "wb+");
			break;
			case 'v':
				verbose++;
			break;
			case 'r':
				sampleRate = strtol(optarg, NULL, 0);
			break;
			case 't':
				hasStartTime = (strptime(optarg, "%H%M%S", &startTime) != NULL);
				break;
			default:
				break;
		}
	}

	int fd = -1;
	FILE *of = NULL;
	FILE *powerFile = NULL;

	Grunenwald g;

	void serialOutputHex(SerialDecoder *sd){
		unsigned char value = 0;
		for(int i = 1; i <= sd->dataBits ; i++){
			if(sd->bits[i]){
			   value |= (1 << (i - 1));
			}
		}
		// fprintf(stderr, "%02X", value);
		GrunenwaldUpdate(&g, sd, value);
	}


	GrunenwaldInit(&g);

	void grunenwaldSOFCallBack(SerialDecoder *sd){
		// fprintf(stderr, "\n" "%20llu: ", sd->absoluteSampleCounter);
		GrunenwaldSOF(&g, sd);
	}

	FMDecoder fm;
	FMDecoderInit(&fm, sampleRate, 4, 4, 0);
	SerialDecoder sd;
	SerialDecoderInit(&sd, 8, PARITY_DONT_CARE, STOP_1_BIT, 39400, sampleRate);
	sd.checkedDataCallBack = serialOutputHex;
	sd.startOfFrameCallBack = grunenwaldSOFCallBack;

	void outputCallBack(int I, int Q){
		if(of){
		iq_sample out;
		out.I = intToUChar(I);
		out.Q = intToUChar(Q);
		fwrite(&out, sizeof(out), 1, of);
		}
		SerialDecoderUpdate(&sd, I);
	}

	void powerCallBack(int I, int Q){
		iq_sample out;
		out.I = intToUChar(I);
		out.Q = intToUChar(Q);
		fwrite(&out, sizeof(out), 1, powerFile);
	}

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
	if(outputFileName){
		of = fopen(outputFileName, "wb+");
	}
	if(powerFileName){
		powerFile = fopen(powerFileName, "wb+");
	}

	LUTInit();
	iq_sample in_sample[NB_SAMPLE];
	fm.outputCallBack = outputCallBack;
	if(powerFile){
		fm.crossProductCallBack = powerCallBack;
	}

	for(;;){
		int lus = read(fd, in_sample, sizeof(in_sample));
		if(lus > 0){
			lus /= sizeof(in_sample[0]);
			for(int i = 0 ; i < lus; i++){
				FMDecoderUpdate(&fm, in_sample + i);
			}
		}else{
			break;
		}

	}
	FMDecoderFree(&fm);
	close(fd);
	return(0);
}

