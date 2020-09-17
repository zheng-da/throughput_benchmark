import torch as th
import numpy as np
import time

# The number of GPUs we'll copy data to simultaneously.
num_gpus = 1
# The size of the array that we copy to GPUs.
arr_size = 100000

arr = th.randn(arr_size, 10)
arr = arr.pin_memory()
devs = []
for i in range(num_gpus):
    devs.append(th.device('cuda:' + str(i)))

for i in range(10):
    start = time.time()
    res = []
    for dev in devs:
        #arr1 = arr.to(dev)
        arr1 = arr.to(dev, non_blocking=True)
        res.append(arr1)
    th.cuda.synchronize()
    seconds = time.time() - start
    print('to {} GPUs: {:.3f}'.format(len(devs), np.prod(arr.shape) * 4 / seconds / 1000000000 * len(devs)))
