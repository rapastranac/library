#!/bin/bash
# ---------------------------------------------------------------------
echo "Current working directory: `pwd`"
echo "Starting run at: `date`"

#to debug
#xhost +node1 +manager
#mpirun -hostfile hostfile -np 2 xterm -hold -fa 'Monospace' DISPLAY=manager -fs 12 -e gdb -ex=run ./a.out

mpirun -n 6 xterm -fa 'Monospace' -bg white -fg black -fs 12 -display :0 -e gdb -x gdb_commands --args a.out -N 1 -P 5 -I input/prob_4/600/00600_1


#mpirun -hostfile hostfile -np 10 ./a.out
#mpirun -n 3 a.out
# ---------------------------------------------------------------------
echo "Finishing run at: `date`"
# ---------------------------------------------------------------------
