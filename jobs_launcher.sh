#!/bin/bash


#sbatch 1n.sh
#sbatch 2n.sh
sbatch 4n.sh
sbatch 8n.sh
sbatch 16n.sh
sbatch 32n.sh
sbatch 64n.sh
sbatch 128n.sh

squeue -u pasr1602
