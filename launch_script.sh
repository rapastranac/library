#!/bin/bash

cd jobs/phat1000_27n && sbatch phat1000_27n.sh &
cd jobs/phat1000_54n && sbatch phat1000_54n.sh &
cd jobs/phat700_1n && sbatch phat700_1n.sh &
cd jobs/phat700_27n && sbatch phat700_27n.sh &
cd jobs/phat700_54n && sbatch phat700_54n.sh &
cd jobs/phat700_108n && sbatch phat700_108n.sh &
cd jobs/phat700_216n && sbatch phat700_216n.sh &
cd jobs/phat700_432n && sbatch phat700_432n.sh &
cd jobs/frb3015_27n && sbatch frb3015_27n.sh &
cd jobs/frb3015_54n && sbatch frb3015_54n.sh &
cd jobs/frb3015_108n && sbatch frb3015_108n.sh &
cd jobs/frb3015_216n && sbatch frb3015_216n.sh &
cd jobs/frb3015_432n && sbatch frb3015_432n.sh &