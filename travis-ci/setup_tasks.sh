#!/bin/sh

TRAVISUID=`id -u`
exec sudo docker run --rm=false -v `pwd`:/gct:rw ${IMAGE} /bin/bash -x /gct/travis-ci/run_task_inside_docker.sh ${TRAVISUID} ${TASK} ${COMPONENTS}

