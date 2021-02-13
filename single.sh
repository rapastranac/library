#!/bin/bash
#SBATCH --job-name=only_threading
#SBATCH --output=report/%x-%j.out
#SBATCH --account=def-mlafond
#SBATCH --nodes=1
#SBATCH --tasks=1
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=40
##SBATCH --mem-per-cpu=2000M      # memory; default unit is megabytes
#SBATCH --time=0-02:00:00           # time (DD-HH:MM)
#SBATCH --mail-user=pasr1602@usherbrooke.ca
#SBATCH --mail-type=BEGIN
#SBATCH --mail-type=END
#SBATCH --mail-type=FAIL
#SBATCH --mail-type=REQUEUE
# ---------------------------------------------------------------------


echo "Current working directory: `pwd`"
echo "Starting run at: `date`"

srun ./a.out
# ---------------------------------------------------------------------
echo "Finishing run at: `date`"
# ---------------------------------------------------------------------
