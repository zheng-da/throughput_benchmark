import torch as th
import numpy as np
import time

# The number of GPUs we'll copy data to simultaneously.
num_gpus = 8
# The size of the array that we copy to GPUs.
arr_size = 1000000

arr = th.randn(arr_size, 10)
arr = arr.pin_memory()
devs = []
gpu_arrs = []
pinned_arrs = []
for i in range(num_gpus):
    dev = th.device('cuda:' + str(i))
    gpu_arrs.append(th.randn(arr_size, 10, device=dev))
    pinned_arrs.append(th.zeros(arr_size, 10, pin_memory=True))
    pinned_arrs[-1][:] = 0
    devs.append(dev)
cpu_dev = th.device('cpu')

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

for i in range(10):
    start = time.time()
    res = []
    for dev in devs:
        #arr1 = arr.to(dev)
        arr1 = arr.to(dev, non_blocking=True)
        res.append(arr1)
    for i, arr in enumerate(gpu_arrs):
        pinned_arrs[i].copy_(arr, non_blocking=True)
        #res.append(arr.to(cpu_dev, non_blocking=True))
    th.cuda.synchronize()
    seconds = time.time() - start
    print('from/to {} GPUs: {:.3f}'.format(len(devs), np.prod(arr.shape) * 4 * 2 / seconds / 1000000000 * len(devs)))
