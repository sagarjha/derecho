import posix_ipc
import subprocess
import argparse
import os


PREFIX = "RDMA_FOR_ML_"
MODEL_SEM_NAME = PREFIX+"MODEL_SEM"
GRAD_SEM_NAME = PREFIX+"GRAD_SEM"
MODEL_SHM_NAME = PREFIX+"MODEL_SHM"
GRAD_SHM_NAME = PREFIX+"GRAD_SHM"


def unlink(func, name):
  try:
    func(name)
    print("unlinked: "+name)
  except:
    pass


def parseArgs():
  """
  Parse arguments from commandline.
  Currently, we only use --server to determine whether this
  node is a server or a client. Default is client.
  """
  parser=argparse.ArgumentParser(description='RDMA for ml')
  parser.add_argument(
      '--server', 
      type=bool, 
      default=False)
  return parser.parse_args()


def read_derecho_cfg(config_fn="derecho.cfg"):
  """
  Read Derecho configuration file.
  @Parameter:
    @config_fn: The name of configuration file.
  @Return a id and names of semaphores and a name of shared memory.
  """
  pwd = os.getcwd()
  cfg_filepath = os.path.join(pwd, config_fn)
  print("cfg_filepath {}".format(cfg_filepath))

  assert os.path.exists(cfg_filepath)

  def is_comment(line):
    return len(line.strip()) == 0 or "#" == line.strip()[0]

  with open(cfg_filepath, 'r') as f:
    for line in f:
      if not is_comment(line):
        words = list(map(lambda w: w.strip(), line.strip().split("=")))
        if words[0] == 'model_sem_name':
          model_sem = words[1]
        elif words[0] == 'grad_sem_name':
          grad_sem = words[1]
        elif words[0] == 'model_shm_name':
          model_shm = words[1]
        elif words[0] == 'local_id':
          my_rank = words[1]
  return (my_rank, model_sem, grad_sem, model_shm)


def main():

  args = parseArgs()
  my_rank, model_sem, grad_sem, model_shm = read_derecho_cfg()
  MY_RANK = my_rank

  TRAINING_NN_NAME = "python ./train.py"
  DERECHO_NAME = "./rdma_for_ml2_async"

  NUM_NODE = '2'
  NUM_PARAM = str(784*10)
  ITEMSIZE = str(4)

  train = None
  if args.server:
    #run derecho
    train = subprocess.Popen([DERECHO_NAME,
      NUM_NODE,
      NUM_PARAM,
      ITEMSIZE,
      MODEL_SEM_NAME,
      GRAD_SEM_NAME,
      MODEL_SHM_NAME,
      GRAD_SHM_NAME],
      stdout = subprocess.PIPE)
  else:
    unlink(posix_ipc.unlink_semaphore, MODEL_SEM_NAME)
    unlink(posix_ipc.unlink_semaphore, GRAD_SEM_NAME)
    unlink(posix_ipc.unlink_shared_memory, MODEL_SHM_NAME)
    unlink(posix_ipc.unlink_shared_memory, GRAD_SHM_NAME)

    model_sem = posix_ipc.Semaphore(
        MODEL_SEM_NAME,
        flags = posix_ipc.O_CREAT | posix_ipc.O_EXCL,
        mode = 0o666, 
        initial_value = 1)

    grad_sem = posix_ipc.Semaphore(
        GRAD_SEM_NAME,
        flags = posix_ipc.O_CREAT | posix_ipc.O_EXCL,
        mode = 0o666, 
        initial_value = 1)
    # start training
    train = subprocess.Popen([" ".join([TRAINING_NN_NAME,
      "--epochs 100 ",
      "--derecho-name " + DERECHO_NAME,
      "--num-nodes " + NUM_NODE,
      "--my-rank " + MY_RANK,
      "--model-sem "+MODEL_SEM_NAME,
      "--grad-sem "+GRAD_SEM_NAME,
      "--model-shm "+MODEL_SHM_NAME,
      "--grad-shm "+GRAD_SHM_NAME])],
      shell=True)

  train.wait()


if __name__ == "__main__":
  print(read_derecho_cfg())
  #main()

