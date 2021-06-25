#!/bin/bash

#phat_1000
cd jobs_test/test1 && sbatch job.sh &
cd jobs_test/test2 && sbatch job.sh &
cd jobs_test/test3 && sbatch job.sh &