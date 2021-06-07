#!/bin/bash
#SBATCH --job-name=p_hat1000_nonice_16threads
#SBATCH --output=report/%x-%j.out
#SBATCH --account=def-mlafond
#SBATCH --nodes=4
#SBATCH --tasks=8
#SBATCH --ntasks-per-socket=1
#SBATCH --ntasks-per-node=2
#SBATCH --cpus-per-task=16
#SBATCH --mem=128000M
##SBATCH --mem-per-cpu=4500M     	# memory; default unit is megabytes
##SBATCH --mem=MaxMemPerNode		# memory limit per compute node
#SBATCH --time=0-00:45:00           	# time limit (DD-HH:MM)
#SBATCH --mail-user=pasr1602@usherbrooke.ca
#SBATCH --mail-type=BEGIN
#SBATCH --mail-type=END
#SBATCH --mail-type=FAIL
#SBATCH --mail-type=REQUEUE
#SBATCH --constraint=broadwell
# ---------------------------------------------------------------------

#module --force purge
#module load StdEnv/2020
#module load gcc/10.2.0
#module load openmpi/4.0.5
#module list

module --force purge
module load StdEnv/2020
module load gcc/9.3.0
module load openmpi/4.0.3
module load boost/1.72.0
module list

#gcc --version
#mpirun --version
echo "Current working directory: `pwd`"
echo "Starting run at: `date`"

#srun ./a.out -N 40 -P 4 -I input/prob_4/600/00600_1
srun ./a.out -N 16 -P 4 -I input/p_hat1000_2
#srun ./a.out -N 19 -P 4 -I input/600_cell


#srun ./a.out -N 39 -P 75 -I input/DSJC500_5
#srun ./a.out -N 1 -P 4 -I input/prob_4/600/00600_1

#srun ./a.out -N 19 -P 4 -I input/prob_4/600/0600_93
#srun ./a.out -N 48 -P 4 -I input/edges22k.txt
#srun ./a.out -N 40 -P 82 -I input/keller6
# ---------------------------------------------------------------------
echo "Finishing run at: `date`"
# ---------------------------------------------------------------------
