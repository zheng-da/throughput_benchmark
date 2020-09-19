import torch as th
import numpy as np
import time

src = th.randn(1000000000)
dst = th.randn(1000000000)
for i in range(10):
    start = time.time()
    dst.copy_(src)
    print('sequential copy throughput: {} GB/s'.format(np.prod(src.shape) * 4 / (time.time() - start) / 1024 / 1024 / 1024))

src = th.randn(10000000, 100)
for i in range(10):
    idx = np.random.randint(0, len(src), size=len(src)//10)
    start = time.time()
    dst = src[idx]
    print('random slice throughput: {} GB/s'.format(np.prod(dst.shape) * 4 / (time.time() - start) / 1024 / 1024 / 1024))
