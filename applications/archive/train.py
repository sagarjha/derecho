import torch
import torchvision
import torch.nn as nn
import  torchvision.transforms as transforms
import numpy as np
import posix_ipc
import sysv_ipc
import argparse
import mmap
import subprocess
from functools import reduce


class Net(nn.Module):
  """
  We probably want to use this as model class later.
  """
  def __init__(self):
    pass

# worker class
class Worker:

  # init training and load dataset
  def __init__(self, model_optimizer_pairs, criterion, my_rank, num_workers, buf_num):
    """
    Loading dataset and init model.
    Parameters:
      @model_optimizer_pairs: a list of tuples that contains (model, optimizer)
    """
    self.rank = my_rank
    self.num_workers = num_workers
    self.model_optimizer_pairs = model_optimizer_pairs
    self.criterion = criterion
    self.train_loader = self.load_dataset()
    self.config = self.load_config()
    self.buf_num = buf_num


  def load_dataset(self):
    """
    For different dataset, one can subclass Worker and implement
    their own load_dataset method.
    This method should return a train_loader.
    """
    train_dataset = torchvision.datasets.MNIST(
        root='../data',
        train=True,
        transform=transforms.Compose([
          transforms.ToTensor()
          ]),
        download=True)
    #test_dataset = torchvision.datasets.MNIST(
    #    root='../data',
    #    train=False,
    #    transform=transforms.Compose([
    #      transforms.ToTensor()
    #      ])
    #    )
    data_split_size = len(train_dataset) // self.num_workers
    local_data = torch.utils.data.random_split(train_dataset, [data_split_size for i in range(self.num_workers)])

    return torch.utils.data.DataLoader(
        dataset=local_data[self.rank-1],
        batch_size=128,
        shuffle=True)


  def lauch_derecho(self):
    subprocess.Popen([derecho_program, str(derecho_numnode)],
                     stdin=subprocess.PIPE, stdout=subprocess.PIPE)


  def load_config(self):
    """
    Return a dict that contains configuration paramters
    """
    pass

  def init_sem(self, model, grad):
    self.sem_model = posix_ipc.Semaphore(model)
    self.sem_grad = posix_ipc.Semaphore(grad)


  def latest_model_optimizer_pair(self):
    """
    Return the least model updated by server and its optimizer.
    Depends on different implementation, this method should be overrided.
    Currently, we can use a shared memory to tell which model to use.
    """
    buf_num = self.buf_num.item()
    assert(buf_num >= 0 buf_num < 3)
    return self.model_optimizer_pairs[buf_num]


  def train_iteration(self, data, targets):
    """
    Train the model, compute the grads and push grads to server.
    """

    # resize data for prototyping test
    data = data.view(-1, 28*28)

    # acquire lock for model update. This will be removed in the future after
    # Three-way buffer finished.
    #self.sem_model.acquire()

    # acquire lock for grad update.
    self.sem_grad.acquire()

    # get leaast model and optimizer
    model, optimizer = self.latest_model_optimizer_pair()

    # forward pass
    optimizer.zero_grad()
    outputs = model(data)
    loss = self.criterion(outputs, targets)

    # backward, calculating grads.
    loss.backward()

    # we will remove this line later because grad will be updated by server
    # there should be a lock waiting for RMDA pushing grads to the server
    # completed.
    #optimizer.step()

    self.sem_grad.release()

    # release lock for model update. This will be removed in the future after
    # Three-way buffer finished.
    #self.sem_model.release()

    return (outputs, loss.item())


  def train(self, epochs=100, log_interval=20):
    """
    Train model in mini-batches for @epochs.
    """
    for epoch in range(1, epochs+1):
      for idx, (data, targets) in enumerate(self.train_loader):
        outputs, loss = self.train_iteration(data, targets)
        if idx % log_interval == 0:
          _, predicted = torch.max(outputs.data, 1)
          correct = (predicted == targets).sum()
          print("epoch:", epoch, "correct:", correct.item()/len(data), "loss=", loss)


# helper functions
def shareModel(model, shm_tensors, shm_grads):
  """
  [Update: Use moveModelParametersToSharedMemory instead.]
  subsitute model's tensors to shared tensors.
  model's tensors and shared tensors should have the same shape and datatype.
  """
  paramters = model.parameters()
  assert(len(model.paramters()) == len(shm_tensors) and len(shm_tensors) == len(shm_grads))
  for i in range(len(paramters)):
    assert(paramters[i].data.dtype == shm_tensor[i].dtype)
    parameters[i].data = shm_tensor[i]
    parameters[i].grad = shm_grads[i]


def createTensorInSharedMemory(shape, dtype, mapfile, offset=0):
  """
  This fuction will return a pytorch tensor at offest in mapfile.
  PRECONDITION: there is enough space in mapfile for requested array.
  Params:
    shape: shape of the array as a tuple
    dtype: data type
    mapfile: a mmap.mmap object return from mmap() call. This should
              be the shared memory region.
    offset: the offset to put this array in the mapfile. Default is 0.
  """
  # calculate array size
  size = reduce(lambda x, a: a * x, list(shape)) * np.dtype(dtype).itemsize
  # convert mapfile to a buffer
  buf = memoryview(mapfile)[offset:size+offset]
  # create a np ndarray on shared buffer
  arr = np.ndarray(shape=shape, dtype=dtype, buffer=buf)
  # convert np array to a pytorch tensor and return it
  return torch.from_numpy(arr)


def createTensorInSharedMemoryByTensor(tensor, mapfile, offset=0):
  tensor = tensor.numpy()
  return createTensorInSharedMemory(tensor.shape, tensor.dtype, mapfile, offset)


def moveModelParametersToSharedMemory(model, mapfile, offset=0):
  """
  Move given model to shared memory. If no grad availale, create a new one.
  @Return the new offset for shared memory.
  """
  for param in model.parameters():
    param.data = createTensorInSharedMemoryByTensor(param.data, mapfile, offset)
    offset += param.data.numpy().nbytes
  return offset


def moveGradientsToSharedMemory(model, mapfile, offset=0):
  """
  This function is very similar to the above one except this one moves gradients
  instead of model parameters. The reason to separate this two methods is because
  gradients and parameters may not map to the same shared memory(Different SST).
  @Return the new offset for shared memory.
  """
  for param in model.parameters():
    param.grad = createTensorInSharedMemoryByTensor(param.data, mapfile, offset)
    offset += param.grad.numpy().nbytes
  return offset

def shareModelGradients(src_model, models):
  """
  Make models in @models have the same gradient memory locatoin with @src_model
  """
  params = list(src_model.parameters())
  for model in models:
    _params = list(model.parameters())
    assert(len(params) == len(_params))
    for i in range(len(params)):
      _params[i].grad = params[i].grad


def launchDerecho(name, num_nodes, num_params, itemsize, model_sem, grad_sem, model_shm,
    grad_shm):
  subprocess.Popen([
    name,
    num_nodes,
    num_params,
  itemsize,
    model_sem,
    grad_sem,
    model_shm,
    grad_shm])

  bufs = []
  buf_names = [model_shm * 3]

  for idx, n in enumerate(buf_names):
    buf_names[idx] = n + "_BUF_" + str(idx)

  # we first map SST table
  mapfile, size = waitingSharedMemory(model_shm)

  # map all buffers
  for name in buf_names:
    bufs.append(waitingSharedMemory(name))

  return mapfile, size//2, bufs


def waitingSharedMemory(name):
  """
  Waiting a shared memory with given @name avaliable, then map the whole shared
  memory into current process's address space by mmap.
  Parameters:
    @name: a string identifier for posix shared memory.
  @returns a tuple mapfile and size of the mapped file.
  """
  mem_initilized = False
  shm = None
  while not mem_initilized or (shm != None and shm.size == 0):
    if shm is not None:
      shm.close_fd()
    try:
      shm = posix_ipc.SharedMemory(name, mode=0o666)
      mem_initilized = True
    except:
      pass
  mapfile = mmap.mmap(shm.fd, shm.size)
  size = shm.size
  shm.close_fd()
  return (mapfile, size)


def localTestSetup():
  shm = posix_ipc.SharedMemory(
      SHM_NAME,
      flags= posix_ipc.O_CREAT | posix_ipc.O_EXCL,
      mode= 0o666,
      size=SHM_SIZE)
  mapfile = mmap.mmap(shm.fd, shm.size)
  shm.close_fd()
  return mapfile

#def initialize_threeway_buf(models, mapfile, rowlen):
#  assert(len(models) == 3)
#  offset_model = 0
#  offset_grad = rowlen
#  for model in models:
#    offset_model += moveModelParametersToSharedMemory(model, mapfile, offset_model)
#  model1, model2, model3 = models
#  moveGradientsToSharedMemory(model1, mapfile, offset_grad)
#  model1_p = list(model1.paramters())
#  model2_p = list(model2.paramters())
#  model3_p = list(model3.paramters())
#  for i in range(len(model1_p)):
#    model2_p[]

def main():
  parser=argparse.ArgumentParser(description='RDMA for ml')
  parser.add_argument('--epochs',
      type=int,
      default=100,
      metavar='N')
  parser.add_argument('--my-rank', 
      type=int,
      default=0, 
      help='local id(rank) for this node')
  parser.add_argument('--model-sem', 
      type=str, 
      default="MSEM", 
      help='name for model semaphores')
  parser.add_argument('--model-shm',
      type=str,
      default="MSHM",
      help='name for model shared memory')
  parser.add_argument('--grad-sem',
      type=str,
      default="GSEM",
      help='name for gradident semaphores')
  parser.add_argument('--grad-shm',
      type=str,
      default="GSHM",
      help='name for gradident shared memory')
  parser.add_argument('--derecho-name',
      type=str,
      default="rdma_for_ml2_async",
      help='name for derecho program')
  parser.add_argument('--num-nodes',
      type=int,
      default=2,
      help='number of nodes for training including server nodes')
  # more later

  args=parser.parse_args()

  # lock sem until shared memory intialization finished.
  model_sem = posix_ipc.Semaphore(args.model_sem)
  model_sem.acquire()

  model0 = nn.Linear(784, 10, bias=False)
  model1 = nn.Linear(784, 10, bias=False)
  model2 = nn.Linear(784, 10, bias=False)

  num_params = reduce(lambda a, x: a + x,
      map(lambda x: x.numel(), model.parameters()))
  itemsize = model.parameters().__next__().element_size()

  # prepare for training
  mapfile, rowlen, bufs = launchDerecho(args.derecho_name,
      str(args.num_nodes),
      str(num_params),
      str(itemsize),
      args.model_sem,
      args.grad_sem,
      args.model_shm,
      args.grad_shm)

  # mapfile = localTestSetup()
  moveModelParametersToSharedMemory(model0, bufs[0][0], 0)
  moveModelParametersToSharedMemory(model1, bufs[1][0], 0)
  moveModelParametersToSharedMemory(model2, bufs[2][0], 0)

  moveGradientsToSharedMemory(model0, mapfile, rowlen)
  shareModelGradients(model0, [model1, model2])
  buf_num = createTensorInSharedMemory((1,), np.uint32, mapfile, rowlen*2-4)

  model_sem.release()

  criterion = nn.CrossEntropyLoss()
  optimizer0 = torch.optim.SGD(model0.parameters(), lr=0.1, weight_decay=1e-4)
  optimizer1 = torch.optim.SGD(model1.parameters(), lr=0.1, weight_decay=1e-4)
  optimizer2 = torch.optim.SGD(model2.parameters(), lr=0.1, weight_decay=1e-4)

  worker = Worker(
      [(model0, optimizer0), (model1, optimizer1), (model2, optimizer2)], 
      criterion, 
      args.my_rank,
      args.num_nodes - 1,
      buf_num)
  worker.init_sem(args.model_sem, args.grad_sem)
  worker.train()


if __name__=='__main__':
  SHM_NAME = "RDMA_FOR_ML"
  SHM_SIZE = 4096 * 10
  main()
