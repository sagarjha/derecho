import torch
import torchvision
import torchvision.transforms as transforms

# worker class
class Worker:

  # initialize training and load dataset
  def __init__(self, model_optimizer_pairs, criterion, my_id, num_workers, buf_num, python_sem, cpp_sem):
    """
    Loading dataset and init model.
    Parameters:
      @model_optimizer_pairs: a list of tuples that contains (model, optimizer)
    """
    self.id = my_id
    self.num_workers = num_workers
    self.model_optimizer_pairs = model_optimizer_pairs
    self.criterion = criterion
    self.train_loader = self.load_dataset()
    self.python_sem = python_sem
    self.cpp_sem = cpp_sem
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
    data_split_size = len(train_dataset) // self.num_workers
    split_sizes = [data_split_size for i in range(self.num_workers)]
    split_sizes[-1] += (len(train_dataset) % self.num_workers)
    local_data = torch.utils.data.random_split(train_dataset, split_sizes)

    return torch.utils.data.DataLoader(
        dataset=local_data[self.id-1],
        batch_size=128,
        shuffle=True)
  
  def latest_model_optimizer_pair(self):
    """
    Return the least model updated by server and its optimizer.
    Depends on different implementation, this method should be overridden.
    Currently, we can use a shared memory to tell which model to use.
    """
    buf_num = self.buf_num.item()
    assert(buf_num >= 0 and buf_num < 3)
    return self.model_optimizer_pairs[buf_num]

  def train_iteration(self, data, targets):
    """
    Train the model, compute the gradients and push gradients to server.
    """

    # resize data for prototyping test
    data = data.view(-1, 28*28)

    # acquire lock for gradient update.
    self.cpp_sem.acquire()
    
    # get latest model and optimizer
    model, optimizer = self.latest_model_optimizer_pair()

    # forward pass
    optimizer.zero_grad()
    outputs = model(data)
    loss = self.criterion(outputs, targets)

    # backward, calculating gradients.
    loss.backward()
    
    self.python_sem.release()
    
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
