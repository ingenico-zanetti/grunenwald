#!/bin/bash
# Requires a modified rtl_sdr with software AGC (triggered by negative gain),
# available at https://github.com/ingenico-zanetti/rtl-sdr.git
export PATH="/usr/local/bin:$PATH:$HOME/bin/"
while sleep 10
do
	date >> $HOME/bin/scoreboardsdr.log
	# (rtl_sdr -f 433.92e6 -s 1024e3 -g -24 - | tee $(date +%Y%m%d-%H%M%S.iq) | u8iqfilter | demod3 --rate 1024000 --inputfile - | tee -a scoreboard.log | nc -w 60 127.0.0.1 8366) 2>> $HOME/bin/$(date +%Y%m%d-%H%M%S.scoreboardsdr.log)
	(rtl_sdr -f 433.92e6 -s 1024e3 -g -24 - | u8iqfilter | demod3 --rate 1024000 --inputfile - | tee -a $(date +%Y%m%d-%H%M%S.scoreboard.log) | socat - TCP4:127.0.0.1:8366,nodelay) 2>> $HOME/bin/$(date +%Y%m%d-%H%M%S.scoreboardsdr.log)
done
