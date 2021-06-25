#!/bin/bash

#phat_1000_2
#cd jobs/phat1000_29n/build && cmake .. && make -j 14 &
#cd jobs/phat1000_58n/build && cmake .. && make -j 14 &
#
##phat700_1
#cd jobs/phat700_1n/build && cmake .. && make -j 14 &
#cd jobs/phat700_29n/build && cmake .. && make -j 14 &
#cd jobs/phat700_58n/build && cmake .. && make -j 14 &
#cd jobs/phat700_116n/build && cmake .. && make -j 14 &
#cd jobs/phat700_232n/build && cmake .. && make -j 14 &
#cd jobs/phat700_464n/build && cmake .. && make -j 14 &
#
##frb30_15_1.mis
#cd jobs/frb3015_29n/build && cmake .. && make -j 14 &
#cd jobs/frb3015_58n/build && cmake .. && make -j 14 &
#cd jobs/frb3015_116n/build && cmake .. && make -j 14 &
#cd jobs/frb3015_232n/build && cmake .. && make -j 14 &
#cd jobs/frb3015_464n/build && cmake .. && make -j 14 &


#frb30_15_1.mis special, super scalability
cd super/frb3015_26n/build && cmake .. && make -j 14 &
cd super/frb3015_32n/build && cmake .. && make -j 14 &
cd super/frb3015_64n/build && cmake .. && make -j 14 &
cd super/frb3015_128n/build && cmake .. && make -j 14 &
cd super/frb3015_256n/build && cmake .. && make -j 14 &
cd super/frb3015_512n/build && cmake .. && make -j 14 &
cd super/frb3015_1024n/build && cmake .. && make -j 14 &