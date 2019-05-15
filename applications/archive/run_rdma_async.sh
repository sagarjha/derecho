#!/bin/bash

echo Argument 1 is $1
echo Argument 2 is $2

cmd="python3 ~/derecho/applications/archive/setup.py --$1 --num-nodes $2 --python-path ~/derecho/applications/archive/train.py --derecho-path ./rdma_for_ml2_async"

echo $cmd
