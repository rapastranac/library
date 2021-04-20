#!/bin/bash
# ---------------------------------------------------------------------
echo "Current working directory: `pwd`"
echo "Starting run at: `date`"

#to debug
#xhost +node1 +manager
#mpirun -hostfile hostfile -np 2 xterm -hold -fa 'Monospace' DISPLAY=manager -fs 12 -e gdb -ex=run ./a.out

#mpirun -n 2 xterm -fa 'Monospace' -bg white -fg black -fs 12 -display :0 -e gdb -x gdb_commands --args a.out -N 10 -P 5 -I input/prob_4/400/00400_1

#mpirun -n 2 xterm -fa 'Monospace' -bg white -fg black -fs 12 -display :0 -e valgrind --leak-check=yes ./a.out -N 10 -P 5 -I input/prob_4/400/00400_1

#mpirun -n 2 xterm -fa 'Monospace' -bg white -fg black -fs 12 -e gdb -x gdb_commands --args a.out -N 1 -P 5 -I input/p_hat1000_2

## MINI CLUSTER SETUP
#mpirun -hostfile hostfile -np 3 ./a.out -N 1 -P 5 -I input/prob_4/400/00400_1
#mpirun -hostfile hostfile -np 3 ./a.out -N 1 -P 5 -I input/DSJC500_5

#mpirun -n 5 --bind-to core --map-by numa:PE=2 --report-bindings xterm -fa 'Monospace' -bg white -fg black -fs 12 -e gdb -x gdb_commands --args a.out
#mpirun -hostfile hostfile -np 10 ./a.out -N 1
#mpirun -n 5  --bind-to core --map-by numa --report-bindings ./a.out
#mpirun -n 5 -host manager:3,node1:2  --bind-to core --map-by numa:PE=2 --report-bindings ./a.out
#mpirun -n 5 -display-map --bind-to hwthread --map-by numa:PE=2 --report-bindings ./a.out

mpirun -n 5 -display-map --bind-to hwthread --map-by numa:PE=2 --report-bindings xterm -fa 'Monospace' -bg white -fg black -fs 12 -display :0 -e gdb -x gdb_commands --args a.out
# ---------------------------------------------------------------------
echo "Finishing run at: `date`"
# ---------------------------------------------------------------------
#mpirun -np 2 ./a.out -N 6 -P 5 -I input/prob_4/400/00400_1