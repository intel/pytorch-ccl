# torch-ccl

This repository holds PyTorch bindings maintained by Intel for the Intel® oneAPI Collective Communications Library (oneCCL).


# Introduction

[PyTorch](https://github.com/pytorch/pytorch) is an open-source machine learning framework.

[Intel® oneCCL](https://github.com/oneapi-src/oneCCL) (collective commnications library) is a library for efficient distributed deep learning training implementing such collectives like allreduce, allgather, alltoall. For more information on oneCCL, please refer to the [oneCCL documentation](https://spec.oneapi.com/versions/latest/elements/oneCCL/source/index.html) and [oneCCL specification](https://spec.oneapi.com/versions/latest/elements/oneCCL/source/index.html).

`torch-ccl` module implements PyTorch C10D ProcessGroup API and can be dynamically loaded as external ProcessGroup and only works on Linux platform now.

# Pytorch API Align
We recommend Anaconda as Python package management system. The following is the corresponding branchs (tags) of torch-ccl and supported Pytorch.

   | ``torch`` | ``torch-ccl`` |  
   | :-----:| :---: |  
   |  ``master`` |  ``master``  |
   | [v1.10.0](https://github.com/pytorch/pytorch/tree/v1.10.0) |  [ccl_torch1.10](https://github.com/intel/torch-ccl/tree/ccl_torch1.10)   |
   | [v1.9.0](https://github.com/pytorch/pytorch/tree/v1.9.0) |  [ccl_torch1.9](https://github.com/intel/torch-ccl/tree/ccl_torch1.9)   |
   | [v1.8.1](https://github.com/pytorch/pytorch/tree/v1.8.1) |  [ccl_torch1.8](https://github.com/intel/torch-ccl/tree/ccl_torch1.8)   | 
   | [v1.7.1](https://github.com/pytorch/pytorch/tree/v1.7.1) |  [ccl_torch1.7](https://github.com/intel/torch-ccl/tree/ccl_torch1.7)   | 
   | [v1.6.0](https://github.com/pytorch/pytorch/tree/v1.6.0) |  [ccl_torch1.6](https://github.com/intel/torch-ccl/tree/ccl_torch1.6)   | 
   | [v1.5-rc3](https://github.com/pytorch/pytorch/tree/v1.5.0-rc3) |   [beta09](https://github.com/intel/torch-ccl/tree/beta09)   |

The usage details can be found in the README of corresponding branch. The following part is about the usage of v1.9 tag. if you want to use other version of torch-ccl please checkout to that branch(tag). For pytorch-1.5.0-rc3, the [#PR28068](https://github.com/pytorch/pytorch/pull/28068) and [#PR32361](https://github.com/pytorch/pytorch/pull/32361) are need to dynamicall register external ProcessGroup and enable ``alltoall`` collective communication primitive. The patch file about these two PRs is in ``patches`` directory and you can use it directly. 

# Requirements

Python 3.6 or later and a C++17 compiler

PyTorch v1.10.0

# Installation

To install `torch-ccl`:

1. clone the `torch-ccl`.

```bash
   git clone https://github.com/intel/torch-ccl.git && cd torch-ccl 
   git submodule sync 
   git submodule update --init --recursive
```
2. Install torch-ccl

```bash
   python setup.py install
```


# Usage

example.py

```python

import torch.nn.parallel
import torch.distributed as dist
import torch_ccl

...

os.environ['MASTER_ADDR'] = '127.0.0.1'
os.environ['MASTER_PORT'] = '29500'
os.environ['RANK'] = str(os.environ.get('PMI_RANK', 0))
os.environ['WORLD_SIZE'] = str(os.environ.get('PMI_SIZE', 1))

backend = 'ccl'
dist.init_process_group(backend, ...)
my_rank = dist.get_rank()
my_size = dist.get_world_size()
print("my rank = %d  my size = %d" % (my_rank, my_size))

...

model = torch.nn.parallel.DistributedDataParallel(model, ...)

...
```
(torch_ccl is installed along with the MPI toolset.)
```

$ source <torch_ccl_path>/env/setvars.sh

eg:
  $ torch_ccl_path=$(python -c "import torch; import torch_ccl; import os;  print(os.path.abspath(os.path.dirname(torch_ccl.__file__)))")
  $ source $torch_ccl_path/env/setvars.sh

$ mpirun -n <N> -ppn <PPN> -f <hostfile> python example.py
```


# Performance Debugging

For debugging performance of communication primitives PyTorch's [Autograd profiler](https://pytorch.org/docs/stable/autograd.html#profiler)
can be used to inspect time spent inside oneCCL calls.

Example:

profiling.py

```python

import torch.nn.parallel
import torch.distributed as dist
import torch_ccl
import os

os.environ['MASTER_ADDR'] = '127.0.0.1'
os.environ['MASTER_PORT'] = '29500'
os.environ['RANK'] = str(os.environ.get('PMI_RANK', 0))
os.environ['WORLD_SIZE'] = str(os.environ.get('PMI_SIZE', 1))

backend = 'ccl'
dist.init_process_group(backend)
my_rank = dist.get_rank()
my_size = dist.get_world_size()
print("my rank = %d  my size = %d" % (my_rank, my_size))

x = torch.ones([2, 2])
y = torch.ones([4, 4])
with torch.autograd.profiler.profile(record_shapes=True) as prof:
    for _ in range(10):
        dist.all_reduce(x)
        dist.all_reduce(y)
dist.barrier()
print(prof.key_averages(group_by_input_shape=True).table(sort_by="self_cpu_time_total"))

```

```
$ mpirun -n 2 -l python profiling.py
```

```
[0] my rank = 0  my size = 2
[0] -----------------------------------  ------------  ------------  ------------  ------------  ------------  ------------  --------------------  
[0]                                Name    Self CPU %      Self CPU   CPU total %     CPU total  CPU time avg    # of Calls          Input Shapes  
[0] -----------------------------------  ------------  ------------  ------------  ------------  ------------  ------------  --------------------  
[0]                torch_ccl::allreduce        91.41%     297.900ms        91.41%     297.900ms      29.790ms            10              [[2, 2]]  
[0]     torch_ccl::wait::cpu::allreduce         8.24%      26.845ms         8.24%      26.845ms       2.684ms            10      [[2, 2], [2, 2]]  
[0]     torch_ccl::wait::cpu::allreduce         0.30%     973.651us         0.30%     973.651us      97.365us            10      [[4, 4], [4, 4]]  
[0]                torch_ccl::allreduce         0.06%     190.254us         0.06%     190.254us      19.025us            10              [[4, 4]]  
[0] -----------------------------------  ------------  ------------  ------------  ------------  ------------  ------------  --------------------  
[0] Self CPU time total: 325.909ms
[0] 
[1] my rank = 1  my size = 2
[1] -----------------------------------  ------------  ------------  ------------  ------------  ------------  ------------  --------------------  
[1]                                Name    Self CPU %      Self CPU   CPU total %     CPU total  CPU time avg    # of Calls          Input Shapes  
[1] -----------------------------------  ------------  ------------  ------------  ------------  ------------  ------------  --------------------  
[1]                torch_ccl::allreduce        96.03%     318.551ms        96.03%     318.551ms      31.855ms            10              [[2, 2]]  
[1]     torch_ccl::wait::cpu::allreduce         3.62%      12.019ms         3.62%      12.019ms       1.202ms            10      [[2, 2], [2, 2]]  
[1]                torch_ccl::allreduce         0.33%       1.082ms         0.33%       1.082ms     108.157us            10              [[4, 4]]  
[1]     torch_ccl::wait::cpu::allreduce         0.02%      56.505us         0.02%      56.505us       5.651us            10      [[4, 4], [4, 4]]  
[1] -----------------------------------  ------------  ------------  ------------  ------------  ------------  ------------  --------------------  
[1] Self CPU time total: 331.708ms
[1] 

```


# License
[BSD License](https://github.com/intel/torch-ccl/blob/master/LICENSE)
