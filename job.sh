#!/bin/bash
#SBATCH --job-name=p_hat1000_2_10nodes
#SBATCH --output=report/%x-%j.out
#SBATCH --account=def-mlafond
#SBATCH --nodes=5
#SBATCH --ntasks-per-socket=1
#SBATCH --ntasks-per-node=2
#SBATCH --cpus-per-task=20
#SBATCH --time=0-00:16:00           	# time limit (DD-HH:MM)
#SBATCH --mail-user=pasr1602@usherbrooke.ca
#SBATCH --mail-type=BEGIN
#SBATCH --mail-type=END
#SBATCH --mail-type=FAIL
#SBATCH --mail-type=REQUEUE
#SBATCH --constraint=[dragonfly5] # for niagara only cascadelake Intel 6248, 468 nodes
# ---------------------------------------------------------------------

cd $SLURM_SUBMIT_DIR

module --force purge
module load CCEnv
module load StdEnv/2020  gcc/9.3.0
module load openmpi/4.0.3
module load boost/1.72.0
module list

#gcc --version
#mpirun --version
echo "Current working directory: `pwd`"
echo "Starting run at: `date`"

#srun ./a.out -N 19  -I input/00600_1
srun ./a.out -N 19 -P 4 -I input/p_hat1000_2
#srun ./a.out -N 19 -P 4 -I input/600_cell


#srun ./a.out -N 39 -P 75 -I input/DSJC500_5
#srun ./a.out -N 1 -P 4 -I input/prob_4/600/00600_1

#srun ./a.out -N 19 -P 4 -I input/prob_4/600/0600_93
#srun ./a.out -N 48 -P 4 -I input/edges22k.txt
#srun ./a.out -N 40 -P 82 -I input/keller6
# ---------------------------------------------------------------------
echo "Finishing run at: `date`"
# ---------------------------------------------------------------------
