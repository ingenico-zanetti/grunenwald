# grunenwald
Decoding utility for Grunenwald scorer remote control

The unit whose signal was reverse-engineered is a GD 2100 ECO
It has been found to transmit a BFSK signal around 433.92MHz (frequency deviation is about 30KHz from center frequency)
Baudrate is around 39400. The data seem to be transmitted asynchronously, with 1 Start, 8 data, 1 parity and 1 stop.
Encoding is rather special, using a 2-bit ON par nibble, so 4-bit ON per byte, one byte representing 1 digit on the scoreboard

The program is intended to run on a Pi3A+, so it has been split into a low pass filter (averaging filter on I and Q channel)
and a demodulator/decoder program (that outputs an ASCII representation of the decoded data) to run on multiple core.
The input is an usigned char IQ stream coming from a cheap RTL-SDR dongle, piped from rtl_sdr to u8iqfilter then to demod.
Signal capture, filtering and demodulating / decoding uses less than a core at 1Msps.

The BPSK decoding runs on IQ and simply detects the sign of the frequency offset using cross-product of successive samples.
So it should be extremely robust to carrier frequency shift and jitter (up to the actual Frequency offset).

