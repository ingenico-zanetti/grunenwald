all:demod highlight resample u8iqfilter

#CC_OPT=-pg
CC_OPT=-O3

demod: demod.c
	$(CC) -Wall -Werror $(CC_OPT) -o demod demod.c -lm

highlight: highlight.c
	$(CC) -Wall -Werror -O3 -o highlight highlight.c -lm

resample: resample.c
	$(CC) -Wall -Werror -O3 -o resample resample.c

u8iqfilter: u8iqfilter.c
	$(CC) -Wall -Werror -O3 -o u8iqfilter u8iqfilter.c

