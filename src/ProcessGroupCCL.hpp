/*
 * Copyright (c) 2020-2021, Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Intel Corporation nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once


#include <exception>
#include <memory>
#include <mutex>
#include <vector>

#include <torch/version.h>
#include <c10d/ProcessGroup.hpp>
#include <c10d/Store.hpp>
#include <c10d/Types.hpp>
#include <c10d/Utils.hpp>

namespace torch_ccl {
struct CCLCommCollector;

static inline void format_tensors_param(std::vector<c10::IValue>& param, const at::Tensor& tensor) {
  param.emplace_back(tensor);
}

template <typename T>
static inline void format_tensors_param(std::vector<c10::IValue>& param, const std::vector<T>& vec) {
  for (const auto& elem : vec) {
    format_tensors_param(param, elem);
  }
}
}

namespace c10d {

// WorkCCL is the state associated with a CCL operarion.
//
// ProcessGroupCCL implements CCL bindings for c10d.
//
// All functions on this class are expected to be called in the same
// order across processes in the group.
//
// All collective functions provided by this class are scheduled
// for asynchronous execution by CCL.
constexpr const char* CCL_BACKEND_NAME = "ccl";
class ProcessGroupCCL : public ProcessGroup
{

public:
  class AsyncWorkCCL : public ProcessGroup::Work {
  public:
    AsyncWorkCCL(std::vector<std::vector<at::Tensor>> outputTensors,
                 int rank = -1,
                 c10d::OpType opType = OpType::UNKNOWN,
                 const char* profilingTitle = nullptr,
                 const c10::optional<std::vector<at::Tensor>>& inputTensors = c10::nullopt);

    virtual void run() = 0;

    c10::intrusive_ptr<c10::ivalue::Future> getFuture() override;

    std::vector<at::Tensor> result() override;

    void finishAsyncWorkCCL();

    void finishAsyncWorkCCLError(std::exception_ptr eptr);

  protected:
    friend class ProcessGroupCCL;

  public:
    std::string debugName;
    const std::vector<std::vector<at::Tensor>> outputTensors_;
    // The future returned by getFuture.
    c10::intrusive_ptr<at::ivalue::Future> future_;
  };

  explicit ProcessGroupCCL(const c10::intrusive_ptr<Store>& store,
                           int rank,
                           int size,
                           std::chrono::milliseconds);
  virtual ~ProcessGroupCCL();

#if TORCH_VERSION_MINOR >= 11
  const std::string getBackendName() const override {
    return std::string(CCL_BACKEND_NAME);
  }
#endif

  c10::intrusive_ptr<ProcessGroup::Work> broadcast(
      std::vector<at::Tensor>& data,
      const BroadcastOptions& opts = BroadcastOptions()) override;

  c10::intrusive_ptr<ProcessGroup::Work> allreduce(
      std::vector<at::Tensor>& tensors,
      const AllreduceOptions& opts = AllreduceOptions()) override;

  c10::intrusive_ptr<ProcessGroup::Work> allreduce_coalesced(
      std::vector<at::Tensor>& tensors,
      const AllreduceCoalescedOptions& opts =
          AllreduceCoalescedOptions()) override;

  c10::intrusive_ptr<ProcessGroup::Work> reduce(
      std::vector<at::Tensor>& tensors,
      const ReduceOptions& opts = ReduceOptions()) override;

  c10::intrusive_ptr<ProcessGroup::Work> allgather(
      std::vector<std::vector<at::Tensor>>& outputTensors,
      std::vector<at::Tensor>& inputTensors,
      const AllgatherOptions& opts = AllgatherOptions()) override;

  c10::intrusive_ptr<ProcessGroup::Work> _allgather_base(
      at::Tensor& outputBuffer,
      at::Tensor& inputBuffer,
      const AllgatherOptions& opts = AllgatherOptions()) override;

  c10::intrusive_ptr<ProcessGroup::Work> allgather_coalesced(
      std::vector<std::vector<at::Tensor>>& outputTensorLists,
      std::vector<at::Tensor>& inputTensors,
      const AllgatherOptions& opts = AllgatherOptions()) override;

  c10::intrusive_ptr<ProcessGroup::Work> gather(
      std::vector<std::vector<at::Tensor>>& outputTensors,
      std::vector<at::Tensor>& inputTensors,
      const GatherOptions& opts = GatherOptions()) override;

  c10::intrusive_ptr<ProcessGroup::Work> scatter(
      std::vector<at::Tensor>& outputTensors,
      std::vector<std::vector<at::Tensor>>& inputTensors,
      const ScatterOptions& opts = ScatterOptions()) override;

  c10::intrusive_ptr<ProcessGroup::Work> reduce_scatter(
      std::vector<at::Tensor>& outputTensors,
      std::vector<std::vector<at::Tensor>>& inputTensors,
      const ReduceScatterOptions& opts = ReduceScatterOptions()) override;

  c10::intrusive_ptr<ProcessGroup::Work> alltoall_base(
      at::Tensor& outputTensor,
      at::Tensor& inputTensor,
      std::vector<int64_t>& outputSplitSizes,
      std::vector<int64_t>& inputSplitSizes,
      const AllToAllOptions& opts = AllToAllOptions()) override;

  c10::intrusive_ptr<ProcessGroup::Work> alltoall(
      std::vector<at::Tensor>& outputTensors,
      std::vector<at::Tensor>& inputTensors,
      const AllToAllOptions& opts = AllToAllOptions()) override;

  c10::intrusive_ptr<ProcessGroup::Work> send(
      std::vector<at::Tensor>& tensors,
      int dstRank,
      int tag) override;

  c10::intrusive_ptr<ProcessGroup::Work> recv(
      std::vector<at::Tensor>& tensors,
      int srcRank,
      int tag) override;

  c10::intrusive_ptr<ProcessGroup::Work> recvAnysource(
      std::vector<at::Tensor>& tensor,
      int tag) override;

  c10::intrusive_ptr<ProcessGroup::Work> barrier(
      const BarrierOptions& opts = BarrierOptions()) override;

  // create a new ProcessGroupCCL and initialize CCL if not initialized
  static c10::intrusive_ptr<ProcessGroup> createProcessGroupCCL(
      const c10::intrusive_ptr<Store>& store,
      int rank = -1,
      int size = -1,
      std::chrono::milliseconds op_time_out = kNoTimeout);
  static const int64_t OP_TIMEOUT_MILLIS;
 public:

  static void cclInitOnce();
  static void cclFini();

  // Store that is used to exchange information between processes.
  c10::intrusive_ptr<Store> store_;

  std::chrono::milliseconds timeout;

  std::unique_ptr<torch_ccl::CCLCommCollector> ccl_member_;

  static std::mutex globalMutex;
};

} // namespace c10d
