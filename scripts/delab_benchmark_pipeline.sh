#!/bin/bash

# Execute
# ./delab_benchmark_pipeline.sh <conf_name> <benchmark_executable_name> <run_name> <... additional args ...>

# Required:
sudo apt-get update
sudo apt-get install --no-install-recommends -y autoconf cmake make gcc-12 g++-12 clang libnuma-dev libnuma1 libboost-all-dev libboost-all-dev

source $(dirname "${BASH_SOURCE[0]}")/config.sh

RESULTS_FOLDER="${PROJECT_PATH}/results/${3}/${1}"
RESULT_FILE="${RESULTS_FOLDER}/${2}.json"
HOSTNAME=$(hostname)
echo "Running on node: ${HOSTNAME}"
echo $(date)

cd $PROJECT_PATH

mkdir -p cmake-build-release-${HOSTNAME}
cd cmake-build-release-${HOSTNAME}

cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=/usr/bin/gcc-12 -DCMAKE_CXX_COMPILER=/usr/bin/g++-12 ..  # gcc 12.3.0
#cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=/usr/bin/clang-15 -DCMAKE_CXX_COMPILER=/usr/bin/clang++-15 .. # clang 15.0.7

if [ $? -ne 0 ]; then
    echo "cmake command failed" >&2
    exit 1
fi

make -j
if [ $? -ne 0 ]; then
    echo "make command failed" >&2
    exit 1
fi

if ./benchmark/${2} --out="$RESULT_FILE" ${@:4}; then
    echo "success"
else
    echo "error"
fi

echo $(date)