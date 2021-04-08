#!/bin/bash
#module --force purge
#module load StdEnv/2020
#module load gcc/10.2.0
#module load openmpi/4.0.5
#module load boost/1.72.0 #incompatible with gcc-10 on niagara
#module list


module --force purge
module load StdEnv/2020
module load gcc/9.3.0
module load openmpi/4.0.3
module load boost/1.72.0
module list
