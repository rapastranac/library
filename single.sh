#!/bin/bash
#SBATCH --job-name=p_hat1000-2_32_threads
#SBATCH --output=report/%x-%j.out
#SBATCH --account=def-mlafond
#SBATCH --nodes=1
#SBATCH --tasks=1
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=48
##SBATCH --mem-per-cpu=2000M      # memory; default unit is megabytes
#SBATCH --constraint=[skylake|cascade] # 48 cores
#SBATCH --time=0-02:00:00           # time (DD-HH:MM)
#SBATCH --mail-user=pasr1602@usherbrooke.ca
#SBATCH --mail-type=BEGIN
#SBATCH --mail-type=END
#SBATCH --mail-type=FAIL
#SBATCH --mail-type=REQUEUE
# ---------------------------------------------------------------------


echo "Current working directory: `pwd`"
echo "Starting run at: `date`"

srun ./a.out -N 48 -P 4 -I input/p_hat1000-2
# ---------------------------------------------------------------------
echo "Finishing run at: `date`"
# ---------------------------------------------------------------------
