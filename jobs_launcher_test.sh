#!/bin/bash

#phat_1000_2
cd jobs/phat1000_29n && sbatch test.sh &
cd jobs/phat1000_58n && sbatch test.sh &

#phat700_1
cd jobs/phat700_1n && sbatch test.sh &
cd jobs/phat700_29n && sbatch test.sh &
cd jobs/phat700_58n && sbatch test.sh &
cd jobs/phat700_116n && sbatch test.sh &
cd jobs/phat700_232n && sbatch test.sh &
cd jobs/phat700_464n && sbatch test.sh &

#frb30_15_1.mis
cd jobs/frb3015_29n && sbatch test.sh &
cd jobs/frb3015_58n && sbatch test.sh &
cd jobs/frb3015_116n && sbatch test.sh &
cd jobs/frb3015_232n && sbatch test.sh &
cd jobs/frb3015_464n && sbatch test.sh &
