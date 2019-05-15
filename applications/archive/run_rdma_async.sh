#!/bin/bash

echo Server or worker argument is $1
echo Number of nodes is $2

cmd="python3 /home/rdma_for_ml/derecho/applications/archive/setup.py --$1 --num-nodes $2 --python-path /home/rdma_for_ml/derecho/applications/archive/train.py --derecho-path ./rdma_for_ml2_async"

echo Running $cmd

$cmd
