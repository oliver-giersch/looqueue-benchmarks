#!/bin/sh

#SBATCH --job-name=scqd_micro
#SBATCH --time 01:30:00
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --partition=standard96
#SBATCH -L ansys:1

sh ./micro/run_pairs_and_bursts.sh scqd 10M 100
