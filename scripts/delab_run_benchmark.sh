#!/bin/bash

# Execute
# ./delab_run_benchmark.sh <benchmark_executable_name> <run_name> <... additional args ...>
# prerequisites:
#       (basic dependencies (g++, clang, make, ...))
#  - <run_name> must be unique.

source $(dirname "${BASH_SOURCE[0]}")/config.sh

# Check if benchmark with same name was already executed.
if [ -d "$PROJECT_PATH/results/${2}" ]; then
    echo "Benchmark-Run with name ${2} was already executed!"
    exit 1
fi

cd $PROJECT_PATH
git reset --hard
git pull --recurse-submodules
git submodule update

mkdir -p $PROJECT_PATH/results/${2}
git rev-parse HEAD > $PROJECT_PATH/results/${2}/git_sha
echo "$@"  > $PROJECT_PATH/results/${2}/run_arguments

for node_conf in ${node_config[@]}; do
    mkdir -p $PROJECT_PATH/results/${2}/$node_conf

    echo "submitting task for config ${node_conf}"

	OUTPUT_DIR="${PROJECT_PATH}/results/${2}/${node_conf}"
    # Write SBATCH script
    SBATCH_SCRIPT="${OUTPUT_DIR}/run.benchmark"
    cat <<- EOT > ${SBATCH_SCRIPT}
	#!/bin/bash
	#SBATCH --account=${SLURM_ACCOUNT}
	#SBATCH --partition=${partitions[$node_conf]}
	#SBATCH --nodelist=${nodenames[$node_conf]}
	#SBATCH --nodes=1
	#SBATCH --cpus-per-task=${num_cpus[$node_conf]}
	#SBATCH --time=${TIME}
	#SBATCH --container-image=${IMAGES_DIR}/${images[${arch[$node_conf]}]}
	#SBATCH --container-mounts=${PROJECT_PATH}:${PROJECT_PATH}
	#SBATCH --output=${OUTPUT_DIR}/output.log
	#SBATCH --error=${OUTPUT_DIR}/error.log
	${additional_config[$node_conf]}

	bash ${PROJECT_PATH}/scripts/delab_benchmark_pipeline.sh ${node_conf} ${1} ${2} ${@:3}
	EOT

    # Submit the SBATCH script
    sbatch $SBATCH_SCRIPT &
done