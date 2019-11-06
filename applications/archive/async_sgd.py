import argparse
import numpy as np
import os
import subprocess
import torch
import torch.nn as nn

import async_train
import ipc_ops

python_sem_name = "/python_sem"
cpp_sem_name = "/cpp_sem"

gradient_shm_name = "gradient_shm"
model_shm_name = "model_shm"

def parse_args():
  """
  Parse arguments from commandline.
  We only need --num_nodes
  """
  parser=argparse.ArgumentParser(description='RDMA For ML')
  parser.add_argument(
    '--num-nodes', 
    type=str, 
    required=True)
  return parser.parse_args()

def read_derecho_config(config_file_name):
  """
  Read Derecho configuration file.
  @Parameter:
    @config_file_name: The name of configuration file.
  @Return various configuration parameters
  """

  assert os.path.exists(config_file_name)

  def is_comment(line):
    return len(line.strip()) == 0 or "#" == line.strip()[0]

  with open(config_file_name, 'r') as config_file:
    for line in config_file:
      if is_comment(line):
        continue
      words = list(map(lambda w: w.strip(), line.strip().split("=")))
      if words[0] == 'local_id':
        node_id = int(words[1])
      elif words[0] == 'leader_ip':
        leader_ip = words[1]
      elif words[0] == 'local_ip':
        node_ip = words[1]
      elif words[0] == 'leader_gms_port':
        leader_gms_port = words[1]
      elif words[0] == 'gms_port':
        node_gms_port = words[1]
      elif words[0] == 'num_in_features':
        num_in_features = int(words[1])
      elif words[0] == 'num_out_features':
        num_out_features = int(words[1])
      elif words[0] == 'num_epochs':
        num_epochs = words[1]
      elif words[0] == 'derecho_path':
        derecho_path = words[1]
  is_server = leader_ip == node_ip and leader_gms_port == node_gms_port
  return node_id, is_server, \
    num_in_features, num_out_features, \
    num_epochs, derecho_path

def launch_worker_derecho(derecho_path, num_nodes, num_params):
  worker_derecho = subprocess.Popen([
    derecho_path,
    num_nodes,
    num_params,
    python_sem_name,
    cpp_sem_name,
    model_shm_name,
    gradient_shm_name])

  bufs = []
  buf_names = [model_shm_name] * 3

  for idx, n in enumerate(buf_names):
    buf_names[idx] = n + "_BUF_" + str(idx)

  # we first map SST table
  mapfile, size = ipc_ops.wait_shm_init(model_shm_name)

  # map all buffers
  for name in buf_names:
    bufs.append(ipc_ops.wait_shm_init(name))

  return worker_derecho, mapfile, size//2, bufs

def main():
  num_nodes = parse_args().num_nodes
  node_id, is_server, \
  num_in_features, num_out_features, \
  num_epochs, derecho_path = read_derecho_config("derecho.cfg")

  num_params = str(num_in_features * num_out_features)
  if is_server:
    # run derecho
    parameter_server = subprocess.Popen([derecho_path,
                                         num_nodes,
                                         num_params,
                                         python_sem_name,
                                         cpp_sem_name,
                                         model_shm_name,
                                         gradient_shm_name])
    parameter_server.wait()
  else:
    # lock sem until shared memory initialization finished.
    python_sem = ipc_ops.init_sem(python_sem_name)

    model0 = nn.Linear(num_in_features, num_out_features, bias=False)
    model1 = nn.Linear(num_in_features, num_out_features, bias=False)
    model2 = nn.Linear(num_in_features, num_out_features, bias=False)

    # prepare for training
    worker_derecho, mapfile, rowlen, bufs = launch_worker_derecho(
      derecho_path,
      num_nodes,
      num_params)

    # Offset is 4 because the sequence number is uin32_t in the C++ code without any padding
    ipc_ops.move_model_shm(model0, bufs[0][0], 4)
    ipc_ops.move_model_shm(model1, bufs[1][0], 4)
    ipc_ops.move_model_shm(model2, bufs[2][0], 4)

    ipc_ops.move_gradients_shm(model0, mapfile, rowlen)
    ipc_ops.share_gradients(model0, [model1, model2])
    # -8 instead of -4 because SST adds padding. Customize it later.
    buf_num = ipc_ops.create_tensor_shm((1,), np.int32, mapfile, rowlen*2-8)

    criterion = nn.CrossEntropyLoss()
    optimizer0 = torch.optim.SGD(model0.parameters(), lr=0.1, weight_decay=1e-4)
    optimizer1 = torch.optim.SGD(model1.parameters(), lr=0.1, weight_decay=1e-4)
    optimizer2 = torch.optim.SGD(model2.parameters(), lr=0.1, weight_decay=1e-4)

    # start training
    worker = async_train.Worker(
    [(model0, optimizer0), (model1, optimizer1), (model2, optimizer2)], 
    criterion, node_id,
    int(num_nodes) - 1, buf_num, 
    python_sem, ipc_ops.init_sem(cpp_sem_name))
    worker.train()
    worker_derecho.wait()

if __name__ == "__main__":
  main()
