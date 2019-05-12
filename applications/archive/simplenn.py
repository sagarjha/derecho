""" Neural Network.
A 2-Hidden Layers Fully Connected Neural Network (a.k.a Multilayer Perceptron)
implementation with TensorFlow. This example is using the MNIST database
of handwritten digits (http://yann.lecun.com/exdb/mnist/).
Links:
    [MNIST Dataset](http://yann.lecun.com/exdb/mnist/).
Author: Aymeric Damien
Project: https://github.com/aymericdamien/TensorFlow-Examples/
"""
import torch
import torch.nn as nn
import torch.nn.functional as F
import torchvision
from torch.autograd import Variable
import  torchvision.transforms as transforms
import subprocess
import io
import sysv_ipc
import array
import struct
import numpy
import sys
import os


def read_derecho_cfg():
    pwd = os.getcwd()
    cfg_filepath = os.path.join(pwd, 'derecho.cfg')
    print("cfg_filepath {}".format(cfg_filepath))
    assert os.path.exists(cfg_filepath)
    with open(cfg_filepath, 'r') as f:
        lines = f.readlines()

    for line in lines:
        words = line.strip().split()
        if '#' in words or len(line) < 3:
            continue
        else:
            if words[0] == 'semaphore_key_cpp':
                semaphore_key_cpp = words[2]
            elif words[0] == 'semaphore_key_py' :
                semaphore_key_py = words[2]
            elif words[0] == 'shared_memory_key_to_cpp':
                shared_memory_key_to_cpp = words[2]
            elif words[0] == 'shared_memory_key_to_py' :
                shared_memory_key_to_py = words[2]

    return int(semaphore_key_cpp), int(semaphore_key_py), int(shared_memory_key_to_cpp), int(shared_memory_key_to_py)

# Derecho parameter
assert len(sys.argv) == 3
derecho_program = sys.argv[1]
num_nodes = int(sys.argv[2])
semaphore_key_cpp, semaphore_key_py, shared_memory_key_to_cpp, shared_memory_key_to_py = read_derecho_cfg()
permission = 0o666
type_size = 8 # double

# MNIST dataset (images and labels)
input_size = 784
num_classes = 10
train_dataset = torchvision.datasets.MNIST(root='../data',
                                            train=True,
                                            transform=transforms.Compose([
                                                transforms.ToTensor(),
                                            ]),
                                            download=True)

test_dataset = torchvision.datasets.MNIST(root='../data',
                                            train=False,
                                            transform=transforms.Compose([
                                                transforms.ToTensor(),
                                            ])
                                            )

train_loader_pool = []
data_per_worker = len(train_dataset) // num_nodes
split_data = torch.utils.data.random_split(train_dataset, [data_per_worker for i in range(num_nodes)])
for sub_data in split_data:
    train_loader_i = torch.utils.data.DataLoader(dataset=sub_data,
                                                batch_size=128,
                                                shuffle=True)
    train_loader_pool.append(train_loader_i)



# shared memory
print("shared memory init")
SIZE = input_size * num_classes
size = SIZE
byte_size = size * type_size
# byte_size = byte_size - (byte_size % 4096) + 4096
print(byte_size)
semcpp = sysv_ipc.Semaphore(semaphore_key_cpp, flags = sysv_ipc.IPC_CREAT, mode = permission, initial_value = 0)
sempy = sysv_ipc.Semaphore(semaphore_key_py, flags = sysv_ipc.IPC_CREAT, mode = permission, initial_value = 0)

derecho = subprocess.Popen([derecho_program, str(num_nodes), str(SIZE), str(semaphore_key_cpp), str(semaphore_key_py), str(shared_memory_key_to_cpp), str(shared_memory_key_to_py)], stdin=subprocess.PIPE, stdout = subprocess.PIPE)
#stdin_wrapper = io.TextIOWrapper(derecho.stdin, 'utf-8')
#stdin_wrapper.write("127.0.0.1\n")
#stdin_wrapper.write("37683\n")
#stdin_wrapper.write("127.0.0.1\n")
#stdin_wrapper.write("37684\n")
#stdin_wrapper.flush()

def getmessage(p) :
    print("reading")
    while True :
        m = p.stdout.readline().decode("utf-8").strip()
        if m.isdigit() :
            print("reading finishes: " + m)
            return m

message = getmessage(derecho)
node_id = int(message)
if node_id == 0 :
    print("server", SIZE)
    semcpp.release(delta=100)
    while True :
        continue
offset2cpp = int(getmessage(derecho))
offset2py = int(getmessage(derecho))
print("offsets: " + str(offset2cpp) + " " + str(offset2py))
#mem2cpp = sysv_ipc.SharedMemory(shared_memory_key_to_cpp, 0, mode = permission, size = byte_size + offset2cpp)
mem2py = sysv_ipc.SharedMemory(shared_memory_key_to_py, 0, mode = permission, size = byte_size * 2 + offset2py)

#newparas = array.array("d", mem2py.read(byte_size, offset2py));
#print("python setup: test :" + str(newparas[0]))
semcpp.release()
sempy.acquire()
'''options = dict(dtype=numpy.dtype, reshape=numpy.reshape, seed=None)

train = tf.DataSet(mnist.train.images[:int(len(mnist.train.images)/derecho_numnode)],
           mnist.train.images[:int(len(mnist.train.labels)/derecho_numnode)],
           **options)

test = tf.DataSet(mnist.test.images[:int(len(mnist.test.images)/derecho_numnode)],
           mnist.test.images[:int(len(mnist.test.labels)/derecho_numnode)],
           **options)

validation = tf.DataSet(mnist.validation.images[:int(len(mnist.validation.images)/derecho_numnode)],
           mnist.validation.images[:int(len(mnist.validation.labels)/derecho_numnode)],
           **options)
mnist = tf.base.Datasets(train=train, validation=validation, test=test)'''

class worker:
    def __init__(self, rank, train_loader, input_size, num_classes):
        self.rank = rank
        self.train_loader = train_loader

        self.iter = iter(self.train_loader)

        self.model = nn.Linear(input_size, num_classes, bias=False)
        self.model.weight.data.fill_(0.)
#        self.model.bias.data.fill_(0.)
        self.criterion = nn.CrossEntropyLoss()
        self.optimizer = torch.optim.SGD(self.model.parameters(), lr=0.001, weight_decay=1e-4)

    def train_iteration(self, images, labels):
        #images, labels = next(self.iter)
        images = images.view(-1, 28*28)

        # Forward pass
        self.model.zero_grad()
        outputs = self.model(images)
        loss = self.criterion(outputs, labels)

        # Backward and optimize
        self.optimizer.zero_grad()
        loss.backward()
        self.optimizer.step()
        _, predicted = torch.max(outputs.data, 1)
        correct = (predicted == labels).sum()
        #correct = 0
        return correct

    def get_parameters(self):
        param_list = self.model.weight.data.tolist()
        #list.append(self.model.bias.data.numpy().tolist()[0])
        whole_list = []
        for pl in param_list:
            whole_list += pl
        return whole_list

    def update_parameters(self, params):
        self.model.weight.data = torch.FloatTensor(params)
        #self.model.bias.data = torch.FloatTensor([params[-1]])


w = worker(node_id, train_loader_pool[node_id], input_size, num_classes)
epochs = 100
step = 0
correct = 0
print("start training")


for epoch in range(0, epochs):
    for images, labels in w.train_loader:
        step += 1
        correct = w.train_iteration(images, labels)
        if True or step % 20 == 0:
            print(correct)


        tmp = w.get_parameters()
        print(len(tmp))
        mem2py.write(struct.pack("%sd" % len(tmp), *tmp), offset2cpp)
        semcpp.release()
        print("releasing the lock")
        sempy.acquire()
        print("acquired the lock")


        newparas = array.array("d", mem2py.read(byte_size, offset2py))
        newparas = numpy.frombuffer(newparas).tolist()
        print(len(newparas))
        updated_list = []
        for i in range(num_classes):
            updated_list.append(newparas[i*input_size:(i+1)*input_size])

        print(len(updated_list))
        w.update_parameters(updated_list)
        #w.update_parameters(tmp)
