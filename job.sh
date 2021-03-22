#!/bin/bash
#SBATCH --job-name=p_hat1000_2
#SBATCH --output=report/%x-%j.out
#SBATCH --account=def-mlafond
#SBATCH --nodes=3
#SBATCH --tasks=3
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=80
#SBATCH --mem=MaxMemPerNode         # memory limit per compute node
##SBATCH --mem-per-cpu=2000M        # memory; default unit is megabytes
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

#gcc --version
#mpirun --version
echo "Current working directory: `pwd`"
echo "Starting run at: `date`"

#srun ./a.out -N 2 -P 5 -I input/prob_4/600/0600_93
#srun ./a.out -N 48 -P 4 -I input/edges22k.txt
srun ./a.out -N 80 -P 70 -I input/p_hat1000_2
# ---------------------------------------------------------------------
echo "Finishing run at: `date`"
# ---------------------------------------------------------------------
