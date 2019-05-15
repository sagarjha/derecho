#!/bin/bash

echo Server or worker argument is $1
echo Number of nodes is $2

cmd="python3 ~/derecho/applications/archive/setup.py --$1 --num-nodes $2 --python-path ~/derecho/applications/archive/train.py --derecho-path ./rdma_for_ml2_async"

echo Running $cmd

$cmd
