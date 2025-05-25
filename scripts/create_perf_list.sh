#!/bin/bash

source $(dirname "${BASH_SOURCE[0]}")/config.sh

# Check if benchmark with same name was already executed.
if [ -d "$HOME/prefetching/results/${2}" ]; then
    echo "Benchmark-Run with name ${2} was already executed!"
    exit 1
fi

cd ${HOME}/prefetching

mkdir -p $HOME/prefetching/results/${2}
git rev-parse HEAD > $HOME/prefetching/results/${2}/git_sha
echo "$@"  > $HOME/prefetching/results/${2}/run_arguments

for node_conf in ${node_config[@]}; do
    RESULT_FILE="$HOME/prefetching/results/${2}/$node_conf"

    mkdir -p $HOME/prefetching/results/${2}/$node_conf

    echo "submitting task for config ${node_conf}"

    # Write SBATCH script
    SBATCH_SCRIPT="$HOME/prefetching/results/${2}/$node_conf/run.benchmark"
    cat <<- EOT > $SBATCH_SCRIPT
	#!/bin/bash
	#SBATCH --account=rabl
	#SBATCH --partition=${partitions[$node_conf]}
	#SBATCH --nodelist=${nodenames[$node_conf]}
	#SBATCH --nodes=1
	#SBATCH --cpus-per-task=2
	#SBATCH --time=36:00:00
	#SBATCH --container-image=${HOME}/${images[${arch[$node_conf]}]}
	#SBATCH --container-mounts=${HOME}/prefetching:/prefetching
	#SBATCH --output=$HOME/prefetching/results/${2}/$node_conf/output.log
	#SBATCH --error=$HOME/prefetching/results/${2}/$node_conf/error.log
	#SBATCH --container-writable
	#SBATCH --container-remap-root
	${additional_config[$node_conf]}

    git clone https://github.com/jmuehlig/perf-cpp
    cd perf-cpp
    python3 script/create_perf_list.py
    cp perf_list.csv "/prefetching/results/${2}/$node_conf"
	EOT

    # Submit the SBATCH script
    sbatch $SBATCH_SCRIPT &
done