#!/bin/bash
#IDFVER=v5.3.2
IDFVER=latest
# make sure we can coexist with docker
chmod a+w,g+s,o+t $(dirname "$0")
# run the docker
exec docker run --rm -v $PWD:/project -w /project -u $UID -e HOME=/tmp -it docker.io/espressif/idf:$IDFVER "$@"
