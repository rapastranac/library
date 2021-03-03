#!/bin/bash
#SBATCH --job-name=test
#SBATCH --output=report/%x-%j.out
#SBATCH --account=def-mlafond
#SBATCH --nodes=21
#SBATCH --tasks=21
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
module --force purge
module load StdEnv/2020
module load gcc/10.2.0
module load openmpi/4.0.5
module list

echo "Current working directory: `pwd`"
echo "Starting run at: `date`"

srun ./a.out 40 input/prob_4/600/0600_93
# ---------------------------------------------------------------------
echo "Finishing run at: `date`"
# ---------------------------------------------------------------------
