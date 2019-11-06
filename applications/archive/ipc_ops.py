from functools import reduce
import mmap
import numpy as np
import posix_ipc
import torch

def init_sem(sem_name):
    try:
        posix_ipc.unlink_semaphore(sem_name) # destroy any semaphore that might have persisted somehow
    except:
        pass
    sem = posix_ipc.Semaphore(sem_name, posix_ipc.O_CREAT, 0o600, 0)
    return sem

def init_shm(shm_name):
    shm = posix_ipc.SharedMemory(shm_name, mode=0o600)
    return shm

def create_tensor_shm(shape, dtype, mapfile, offset=0):
  """
  This function will return a pytorch tensor at offset in mapfile.
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

def move_model_shm(model, mapfile, offset=0):
  """
  Move given model to shared memory. If no gradient available, create a new one.
  @Return the new offset for shared memory.
  """
  for param in model.parameters():
      tensor = param.data.numpy()
      param.data = create_tensor_shm(tensor.shape, tensor.dtype, mapfile, offset)
      offset += param.data.numpy().nbytes
  return offset

def move_gradients_shm(model, mapfile, offset=0):
  """
  This function is very similar to the above one except this one moves gradients
  instead of model parameters. The reason to separate this two methods is because
  gradients and parameters may not map to the same shared memory(Different SST).
  @Return the new offset for shared memory.
  """
  for param in model.parameters():
      tensor = param.data.numpy()
      param.grad = create_tensor_shm(tensor.shape, tensor.dtype, mapfile, offset)
      offset += param.grad.numpy().nbytes
      return offset

def wait_shm_init(name):
  """
  Wait for a shared memory with given @name available, then map the whole shared
  memory into current process's address space by mmap.
  Parameters:
    @name: a string identifier for posix shared memory.
  @returns a tuple mapfile and size of the mapped file.
  """
  mem_initialized = False
  shm = None
  while not mem_initialized or (shm != None and shm.size == 0):
    if shm is not None:
      shm.close_fd()
    try:
      shm = init_shm(name)
      mem_initialized = True
    except:
      pass
  mapfile = mmap.mmap(shm.fd, shm.size)
  size = shm.size
  shm.close_fd()
  return (mapfile, size)

def share_gradients(src_model, models):
  """
  Make models in @models have the same gradient memory location with @src_model
  """
  params = list(src_model.parameters())
  for model in models:
    _params = list(model.parameters())
    assert(len(params) == len(_params))
    for i in range(len(params)):
      _params[i].grad = params[i].grad
