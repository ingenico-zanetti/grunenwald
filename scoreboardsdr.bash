#!/bin/bash
# Requires a modified rtl_sdr with software AGC (triggered by negative gain),
# available at https://github.com/ingenico-zanetti/rtl-sdr.git
export PATH="/usr/local/bin:$PATH:$HOME/bin/"
while sleep 10
do
	date >> $HOME/bin/scoreboardsdr.log
	(rtl_sdr -f 433.92e6 -s 1024e3 -g -24 - | u8iqfilter | demod2 --rate 1024000 --inputfile - | tee -a scoreboard.log | nc -w 60 127.0.0.1 8366) 2>>  $HOME/bin/scoreboardsdr.log
done
