import posix_ipc
import subprocess

PREFIX = "RDMA_FOR_ML_"
MODEL_SEM_NAME = PREFIX+"MODEL_SEM"
GRAD_SEM_NAME = PREFIX+"GRAD_SEM"

def unlink(func, name):
  try:
    func(name)
    print("unlinked: "+name)
  except:
    pass

unlink(posix_ipc.unlink_semaphore, MODEL_SEM_NAME)
unlink(posix_ipc.unlink_semaphore, GRAD_SEM_NAME)

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


MODEL_SHM_NAME = PREFIX+"MODEL_SHM"
GRAD_SHM_NAME = PREFIX+"GRAD_SHM"
unlink(posix_ipc.unlink_shared_memory, MODEL_SHM_NAME)
unlink(posix_ipc.unlink_shared_memory, GRAD_SHM_NAME)

TRAINING_NN_NAME = "python ./train.py"
DERECHO_NAME = "./rdma_for_ml2_async"

NUM_NODE = '2'
NUM_PARAM = str(784*10)

#model_sem.acquire()

# run derecho
#subprocess.Popen([DERECHO_NAME,
#  NUM_NODE,
#  NUM_PARAM,
#  MODEL_SEM_NAME,
#  GRAD_SEM_NAME,
#  MODEL_SHM_NAME,
#  GRAD_SHM_NAME],
#  stdout = subprocess.PIPE)
#
#mem_initilized = False
#while not mem_initilized:
#  try:
#    shm = posix.SharedMemory(MODEL_SHM_NAME)
#    mem_initilized = True
#  except:
#    pass

# start training
train = subprocess.Popen([" ".join([TRAINING_NN_NAME,
  "--epochs 100 ",
  "--derecho-name " + DERECHO_NAME,
  "--num-nodes " + NUM_NODE,
  "--model-sem "+MODEL_SEM_NAME,
  "--grad-sem "+GRAD_SEM_NAME,
  "--model-shm "+MODEL_SHM_NAME,
  "--grad-shm "+GRAD_SHM_NAME])],
  shell=True)
  #stdout = subprocess.PIPE)

train.wait()

#model_sem.release()

