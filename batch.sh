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
#mpirun --oversubscribe -n 5 -display-map --bind-to none --map-by numa:PE=2 --report-bindings xterm -fa 'Monospace' -bg white -fg black -fs 12 -e gdb -x gdb_commands --args a.out -I input/prob_4/600/00600_1


#mpirun --oversubscribe -n 5 -display-map --bind-to none --map-by core --report-bindings xterm -fa 'Monospace' -bg white -fg black -fs 12 -e gdb -x gdb_commands --args a.out -I input/prob_4/600/00600_1
mpirun --oversubscribe -n 5 -display-map --bind-to none --map-by core --report-bindings ./a.out -N 4 -I input/prob_4/600/00600_1
#mpirun --oversubscribe -n 3 -display-map --bind-to none --map-by core --report-bindings ./a.out -N 5 -I input/p_hat700_1
#mpirun --oversubscribe -n 5 -display-map --bind-to none --map-by core --report-bindings ./a.out -N 4 -I input/p_hat500_3
#mpirun --oversubscribe -n 129 -host manager:1,node1:1 -display-map --bind-to none --map-by core --report-bindings ./a.out -N 4 -I input/frb30_15_mis/frb30_15_1.mis
#mpirun --oversubscribe -n 129 -display-map --bind-to none --map-by core --report-bindings ./a.out -N 4 -I input/frb30_15_mis/frb30_15_1.mis

#mpirun --oversubscribe -n 4 -host node1:1,manager:3 -display-map --bind-to none --map-by core --report-bindings ./a.out -N 3 -I input/p_hat700_1
#mpirun --oversubscribe -n 5 -display-map --bind-to none --map-by core --report-bindings ./a.out -N 4 -I input/p_hat700_1

#mpirun --oversubscribe -n 5 -display-map --bind-to none --map-by core --report-bindings ./a.out -N 4 -I input/p_hat1000_2



#mpirun --oversubscribe -n 3 -display-map --bind-to none --map-by core --report-bindings xterm -fa 'Monospace' -bg white -fg black -fs 12 -e gdb -x gdb_commands --args a.out -N 5 -I input/p_hat700_1



#mpirun -hostfile hostfile -np 10 ./a.out -N 1
#mpirun -n 2  --bind-to core:overload-allowed --map-by numa:PE=2 --report-bindings ./a.out -N 6
#mpirun -n 2 -host manager:1,node1:1  --bind-to core:overload-allowed --map-by numa:PE=6 --report-bindings ./a.out -N 6
#mpirun -n 17 -host manager:16,node1:1  --bind-to none --map-by core --report-bindings ./a.out -N 1  -I input/prob_4/600/00600_1
#mpirun -n 2  -host manager:1,node1:1   --bind-to none --map-by core --report-bindings ./a.out -N 16 -I input/prob_4/600/00600_1


#mpirun -n 5 -host manager:3,node1:2 -display-map --bind-to hwthread --map-by numa:PE=2 --report-bindings ./a.out -N 2
#mpirun -n 17 -hostfile hostfile xterm -fa 'Monospace' -bg white -fg black -fs 12 -display :0 -e gdb -x gdb_commands --args a.out

#mpirun -n 9 -hostfile hostfile a.out >LOG

#mpirun -n 5 -display-map --bind-to hwthread --map-by numa:PE=2 --report-bindings a.out -N 1
#mpirun --oversubscribe -n 5 a.out -N 10

#mpirun -n 2 -display-map --bind-to hwthread --map-by numa:PE=2 --report-bindings xterm -fa 'Monospace' -bg white -fg black -fs 12 -display :0 -e gdb -x gdb_commands --args a.out -N 1
# ---------------------------------------------------------------------
echo "Finishing run at: `date`"
# ---------------------------------------------------------------------
#mpirun -np 2 ./a.out -N 6 -P 5 -I input/prob_4/400/00400_1