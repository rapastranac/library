#!/bin/bash
#SBATCH --job-name=p_hat1000_2_10nodes
#SBATCH --output=report/%x-%j.out
#SBATCH --account=def-mlafond
#SBATCH --nodes=10
#SBATCH --ntasks-per-socket=1
#SBATCH --ntasks-per-node=2
#SBATCH --cpus-per-task=20
#SBATCH --time=0-00:16:00           	# time limit (DD-HH:MM)
#SBATCH --mail-user=pasr1602@usherbrooke.ca
#SBATCH --mail-type=BEGIN
#SBATCH --mail-type=END
#SBATCH --mail-type=FAIL
#SBATCH --mail-type=REQUEUE
#SBATCH --constraint=[dragonfly1|dragonfly2|dragonfly3|dragonfly4] # for niagara only cascadelake Intel 6248, 468 nodes
# ---------------------------------------------------------------------

cd $SLURM_SUBMIT_DIR

module --force purge
module load CCEnv
module load StdEnv/2020  gcc/9.3.0
module load openmpi/4.0.3
module load boost/1.72.0
module list


#module --force purge
#module load NiaEnv
#module load intel/2019u4 intelmpi/2019u4
#module load cmake/3.17.3
#module load boost/1.73.0
#module list


#gcc --version
#mpirun --version
echo "Current working directory: `pwd`"
echo "Starting run at: `date`"

srun ./a.out -N 19 -I input/p_hat1000_2

# ---------------------------------------------------------------------
echo "Finishing run at: `date`"
# ---------------------------------------------------------------------
