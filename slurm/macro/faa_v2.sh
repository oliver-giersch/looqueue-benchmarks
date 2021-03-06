#!/bin/sh

#SBATCH --job-name=faa_v2_macro
#SBATCH --time 01:00:00
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --partition=standard96:test
#SBATCH -L ansys:1

sh ./macro/run_reads_and_writes.sh faa_v2 10M 100
