#!/bin/sh -xe

# This script starts docker

sudo docker run --rm=true -v `pwd`:/gct:rw ${IMAGE} /bin/bash -c "bash -xe /gct/travis-ci/run_task_inside_docker.sh ${IMAGE} ${TASK} ${COMPONENTS}"

