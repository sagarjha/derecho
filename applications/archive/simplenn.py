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
derecho_program = "../rdma_for_ml2"
derecho_numnode = 2

# shared semaphore and memory
semshk = 314158
smshk = 314259
permission = 0o666
size = None
type_size = 8 # double
byte_size = None

# Import MNIST data
from tensorflow.examples.tutorials.mnist import input_data
mnist = input_data.read_data_sets("/tmp/data/", one_hot=True)

import tensorflow as tf

# Parameters
learning_rate = 0.1
num_steps = 1000
batch_size = 128
display_step = 1

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
loss_op = tf.reduce_mean(tf.nn.softmax_cross_entropy_with_logits(
    logits=logits, labels=Y))
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
sem = sysv_ipc.Semaphore(semshk, flags = sysv_ipc.IPC_CREAT, mode = permission, initial_value = 0)
mem = sysv_ipc.SharedMemory(smshk, flags=sysv_ipc.IPC_CREAT, mode = permission, size = byte_size)

derecho = subprocess.Popen([derecho_program, str(derecho_numnode), str(SIZE), str(semshk), str(smshk)], stdin=subprocess.PIPE, stdout = subprocess.PIPE)
message = derecho.stdout.readline()
stdin_wrapper = io.TextIOWrapper(derecho.stdin, 'utf-8')
stdin_wrapper.write("127.0.0.1\n")
stdin_wrapper.write("37683\n")
stdin_wrapper.write("127.0.0.1\n")
stdin_wrapper.write("37684\n")
stdin_wrapper.flush()
message = derecho.stdout.readline()
if int(message.decode("utf-8").strip()) == 0 :
    print("server", SIZE)
    while True :
        continue

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
    print("Total Steps: " + str(num_steps))

    for step in range(1, num_steps+1):
        batch_x, batch_y = mnist.train.next_batch(batch_size)
        # Run optimization op (backprop)
        sess.run(train_op, feed_dict={X: batch_x, Y: batch_y})
        if step % display_step == 0 or step == 1:
            # Calculate batch loss and accuracy
            loss, acc = sess.run([loss_op, accuracy], feed_dict={X: batch_x,
                                                                 Y: batch_y})
            print("Step " + str(step) + ", Minibatch Loss= " + \
                  "{:.4f}".format(loss) + ", Training Accuracy= " + \
                  "{:.3f}".format(acc))

            V = tf.trainable_variables()
            D = [(v.name, v.value()) for v in V]
            arrays = [d[1].eval(session=sess) for d in D]

            #print(arrays)
            sps = [arr.shape for arr in arrays]
            szs = [arr.size for arr in arrays]

            #print(len(szs), szs)
            idxs = [szs[0]]
            for sz in szs[1:-1] :
                idxs.append(idxs[-1] + sz)
            #print(idxs)

            lst = [arr.reshape([1, -1]) for arr in arrays]
            import numpy
            tmp = numpy.concatenate((lst), axis = 1).ravel().tolist()
            mem.write(struct.pack("%sd" % len(tmp), *tmp))
            sem.release()
            print("releasing the lock")
            sem.acquire()
            print("acquired the lock")
            #for ele in tmp :
            #    #print("writing")
            #    stdin_wrapper.write(str(ele) + " ")
            #    stdin_wrapper.flush()
            # tmp = ' '.join(map(str, tmp)) + "\n"
            #stdin_wrapper.write(tmp)
            #stdin_wrapper.write("\n")
            #stdin_wrapper.flush()


            newparas = array.array("d", mem.read());
            #newparas = derecho.stdout.readline().decode("utf-8")
            print(newparas, len(newparas))
            print("roundgood")
            #newparas = numpy.fromstring(newparas)
            newparas = numpy.frombuffer(newparas)

            rec = numpy.split(newparas, idxs)
            recs = [rec[i].reshape(sps[i]) for i in range(len(rec))]
            for i in range(len(V)) :
                V[i].load(recs[i], sess)

    print("Optimization Finished!")

    # Calculate accuracy for MNIST test images
    print("Testing Accuracy:", \
        sess.run(accuracy, feed_dict={X: mnist.test.images, Y: mnist.test.labels}))
