""" Neural Network.
A 2-Hidden Layers Fully Connected Neural Network (a.k.a Multilayer Perceptron)
implementation with TensorFlow. This example is using the MNIST database
of handwritten digits (http://yann.lecun.com/exdb/mnist/).
Links:
    [MNIST Dataset](http://yann.lecun.com/exdb/mnist/).
Author: Aymeric Damien
Project: https://github.com/aymericdamien/TensorFlow-Examples/
"""

from __future__ import print_function
import subprocess
import io
import sysv_ipc
import array
import struct
import sys
import os
import numpy

import tensorflow as tf

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
        if words[0] == 'semaphore_key':
            semaphore_key = words[2]
        elif words[0] == 'shared_memory_key':
            shared_memory_key = words[2]

return int(semaphore_key), int(shared_memory_key)

# Derecho parameter
assert len(sys.argv) == 3
derecho_program = sys.argv[1]
num_nodes = sys.argv[2]
semaphore_key, shared_memory_key = read_derecho_cfg()
permission = 0o666
type_size = 8 # double

# ML Parameters
learning_rate = 0.1
num_steps = 1000
batch_size = 128
display_frequency = 1

# Network Parameters
n_hidden_1 = 256 # 1st layer number of neurons
n_hidden_2 = 256 # 2nd layer number of neurons
num_input = 784 # MNIST data input (img shape: 28*28)
num_classes = 10 # MNIST total classes (0-9 digits)

# tf Graph input
X = tf.placeholder("float", [None, num_input])
Y = tf.placeholder("float", [None, num_classes])

# Store layers weight & bias
weights = {
'h1': tf.Variable(tf.random_normal([num_input, n_hidden_1])),
'h2': tf.Variable(tf.random_normal([n_hidden_1, n_hidden_2])),
'out': tf.Variable(tf.random_normal([n_hidden_2, num_classes]))
}
biases = {
'b1': tf.Variable(tf.random_normal([n_hidden_1])),
'b2': tf.Variable(tf.random_normal([n_hidden_2])),
'out': tf.Variable(tf.random_normal([num_classes]))
}

# Create model
def neural_net(x):
# Hidden fully connected layer with 256 neurons
layer_1 = tf.add(tf.matmul(x, weights['h1']), biases['b1'])
# Hidden fully connected layer with 256 neurons
layer_2 = tf.add(tf.matmul(layer_1, weights['h2']), biases['b2'])
# Output fully connected layer with a neuron for each class
out_layer = tf.matmul(layer_2, weights['out']) + biases['out']
return out_layer

# Construct model
logits = neural_net(X)
prediction = tf.nn.softmax(logits)

# Define loss and optimizer
loss_op = tf.reduce_mean(tf.nn.softmax_cross_entropy_with_logits(logits=logits, labels=Y))
optimizer = tf.train.AdamOptimizer(learning_rate=learning_rate)
train_op = optimizer.minimize(loss_op)

# Evaluate model
correct_pred = tf.equal(tf.argmax(prediction, 1), tf.argmax(Y, 1))
accuracy = tf.reduce_mean(tf.cast(correct_pred, tf.float32))

# Initialize the variables (i.e. assign their default value)
init = tf.global_variables_initializer()
V = tf.trainable_variables()
D = [v.get_shape() for v in V]
SIZE = sum([d.num_elements() for d in D])

# shared memory
size = SIZE
byte_size = size * type_size
sem = sysv_ipc.Semaphore(semaphore_key, flags = sysv_ipc.IPC_CREAT, mode = permission, initial_value = 0)
mem = sysv_ipc.SharedMemory(shared_memory_key, flags=sysv_ipc.IPC_CREAT, mode = permission, size = byte_size)

derecho = subprocess.Popen([derecho_program, str(num_nodes), str(SIZE), str(semaphore_key), str(shared_memory_key)], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
node_id = int(derecho.stdout.readline().decode("utf-8").strip())

if node_id == 0 :
print("server {}".format(SIZE))
while True :
    continue

# Import MNIST data
from tensorflow.examples.tutorials.mnist import input_data
mnist = input_data.read_data_sets("/tmp/data/", one_hot=True)

sem.acquire()
options = dict(dtype=dtype, reshape=reshape, seed=None)

train = DataSet(mnist.train.images[:int(len(mnist.train.images)/derecho_numnode)],
           mnist.train.images[:int(len(mnist.train.labels)/derecho_numnode)],
           **options)

test = DataSet(mnist.test.images[:int(len(mnist.test.images)/derecho_numnode)],
           mnist.test.images[:int(len(mnist.test.labels)/derecho_numnode)],
           **options)

validation = DataSet(mnist.validation.images[:int(len(mnist.validation.images)/derecho_numnode)],
           mnist.validation.images[:int(len(mnist.validation.labels)/derecho_numnode)],
           **options)
mnist = base.Datasets(train=train, validation=validation, test=test)

# Start training
with tf.Session() as sess:
    # Run the initializer
    sess.run(init)
    print("Total step: " + str(num_steps))

    for step in range(1, num_steps+1):
        batch_x, batch_y = mnist.train.next_batch(batch_size)
        # Run optimization op (backprop)
        sess.run(train_op, feed_dict={X: batch_x, Y: batch_y})
        if step % display_frequency == 0 or step == 1:
            # Calculate batch loss and accuracy
            loss, acc = sess.run([loss_op, accuracy], feed_dict={X: batch_x,
                                                                 Y: batch_y})
            print("Step " + str(step) + ", Minibatch Loss= " + \
                  "{:.4f}".format(loss) + ", Training Accuracy= " + \
                  "{:.3f}".format(acc))

            # Copying local memory to shared memory of rdma_for_ml2.
            V = tf.trainable_variables()
            D = [(v.name, v.value()) for v in V]
            arrays = [d[1].eval(session=sess) for d in D]
            sps = [arr.shape for arr in arrays]
            szs = [arr.size for arr in arrays]
            idxs = [szs[0]]
            for sz in szs[1:-1] :
                idxs.append(idxs[-1] + sz)
            # flatten
            lst = [arr.reshape([1, -1]) for arr in arrays]
            tmp = numpy.concatenate((lst), axis = 1).ravel().tolist()
            # write to the shared memory
            mem.write(struct.pack("%sd" % len(tmp), *tmp))
            sem.release()

            # [TO DO] condition variable to make sure that the cpp would be waiting
            sem.acquire()

            newparas = array.array("d", mem.read());
            print(newparas, len(newparas))
            newparas = numpy.frombuffer(newparas)

            rec = numpy.split(newparas, idxs)
            recs = [rec[i].reshape(sps[i]) for i in range(len(rec))]
            for i in range(len(V)) :
                V[i].load(recs[i], sess)

    print("Optimization Finished!")

    # Calculate accuracy for MNIST test images
    print("Testing Accuracy:", \
        sess.run(accuracy, feed_dict={X: mnist.test.images, Y: mnist.test.labels}))
