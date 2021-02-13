#!/bin/bash
#SBATCH --job-name=library
#SBATCH --output=report/%x-%j.out
#SBATCH --account=def-mlafond
#SBATCH --nodes=2
#SBATCH --tasks=40
#SBATCH --ntasks-per-node=20
#SBATCH --cpus-per-task=2
##SBATCH --mem-per-cpu=2000M      # memory; default unit is megabytes
#SBATCH --time=0-02:00:00           # time (DD-HH:MM)
#SBATCH --mail-user=pasr1602@usherbrooke.ca
#SBATCH --mail-type=BEGIN
#SBATCH --mail-type=END
#SBATCH --mail-type=FAIL
#SBATCH --mail-type=REQUEUE
# ---------------------------------------------------------------------
gcc --version
mpirun --version
echo "Current working directory: `pwd`"
echo "Starting run at: `date`"

srun ./a.out
# ---------------------------------------------------------------------
echo "Finishing run at: `date`"
# ---------------------------------------------------------------------
