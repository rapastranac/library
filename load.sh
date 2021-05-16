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




 
xterm -e 'ssh nia0027 -t 'htop'' &
xterm -e 'ssh nia0089 -t 'htop'' &
xterm -e 'ssh nia0170 -t 'htop'' &
xterm -e 'ssh nia0188 -t 'htop'' &
xterm -e 'ssh nia0233 -t 'htop'' &
xterm -e 'ssh nia0234 -t 'htop'' &
xterm -e 'ssh nia0239 -t 'htop'' &
xterm -e 'ssh nia0258 -t 'htop'' &
xterm -e 'ssh nia0311 -t 'htop'' &
xterm -e 'ssh nia0315 -t 'htop'' &
xterm -e 'ssh nia0334 -t 'htop'' &
xterm -e 'ssh nia0367 -t 'htop'' &
xterm -e 'ssh nia0383 -t 'htop'' &
xterm -e 'ssh nia0396 -t 'htop'' &
xterm -e 'ssh nia0541 -t 'htop'' &
xterm -e 'ssh nia0568 -t 'htop'' &
xterm -e 'ssh nia0635 -t 'htop'' &
xterm -e 'ssh nia0675 -t 'htop'' &
xterm -e 'ssh nia0857 -t 'htop'' &
xterm -e 'ssh nia1091 -t 'htop'' &