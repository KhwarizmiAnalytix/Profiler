/*
 * XSigma: High-Performance Computational Library
 *
 * SPDX-License-Identifier: GPL-3.0-or-later OR Commercial
 *
 * This file is part of XSigma and is licensed under a dual-license model:
 *
 *   - Open-source License (GPLv3):
 *       Free for personal, academic, and research use under the terms of
 *       the GNU General Public License v3.0 or later.
 *
 *   - Commercial License:
 *       A commercial license is required for proprietary, closed-source,
 *       or SaaS usage. Contact us to obtain a commercial agreement.
 *
 * Contact: licensing@xsigma.co.uk
 * Website: https://www.xsigma.co.uk
 */

/* Copyright 2019 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "native/exporters/xplane/xplane_schema.h"

#include <atomic>
#include <cstdint>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

#include "common/flat_hash.h"
#include "common/profiler_macros.h"

namespace profiler
{
namespace
{
template <class Collection>
bool InsertOrUpdate(Collection* const collection, const typename Collection::value_type& vt)
{
    std::pair<typename Collection::iterator, bool> const ret = collection->insert(vt);
    if (!ret.second)
    {
        // update
        ret.first->second = vt.second;
        return false;
    }
    return true;
}

// Same as above, except that the key and value are passed separately.
template <class Collection>
bool InsertOrUpdate(
    Collection* const                                   collection,
    const typename Collection::value_type::first_type&  key,
    const typename Collection::value_type::second_type& value)
{
    return InsertOrUpdate(collection, typename Collection::value_type(key, value));
}

template <typename M, typename ReverseM>
bool ReverseMap(const M& m, ReverseM* reverse)
{
    bool all_unique = true;
    for (const auto& kv : m)
    {
        if (!InsertOrUpdate(reverse, kv.second, kv.first))
        {
            all_unique = false;
        }
    }
    return all_unique;
}

template <typename ReverseM, typename M>
ReverseM ReverseMap(const M& m)
{
    typename std::remove_const<ReverseM>::type reverse;
    ReverseMap(m, &reverse);
    return reverse;
}
// Returns a pointer to the const value associated with the given key if it
// exists, or NULL otherwise.
template <class Collection>
const typename Collection::value_type::second_type* FindOrNull(
    const Collection& collection, const typename Collection::value_type::first_type& key)
{
    auto const it = collection.find(key);
    if (it == collection.end())
    {
        return nullptr;
    }
    return &it->second;
}

// Same as above but returns a pointer to the non-const value.
template <class Collection>
typename Collection::value_type::second_type* FindOrNull(
    Collection&                                        collection,  // NOLINT
    const typename Collection::value_type::first_type& key)
{
    typename Collection::iterator it = collection.find(key);
    if (it == collection.end())
    {
        return nullptr;
    }
    return &it->second;
}

PROFILER_UNUSED constexpr int kNumHostEventTypes =
    HostEventType::kLastHostEventType - HostEventType::kFirstHostEventType + 1;

PROFILER_UNUSED constexpr int kNumStatTypes =
    StatType::kLastStatType - StatType::kFirstStatType + 1;

PROFILER_UNUSED constexpr int kNumMegaScaleStatTypes =
    MegaScaleStatType::kLastMegaScaleStatType - MegaScaleStatType::kFirstMegaScaleStatType + 1;

PROFILER_UNUSED constexpr int kNumLineIdTypes =
    LineIdType::kLastLineIdType - LineIdType::kFirstLineIdType + 1;

using HostEventTypeMap        = flat_hash_map<std::string_view, HostEventType>;
using HostEventTypeStrMap     = flat_hash_map<HostEventType, std::string_view>;
using StatTypeMap             = flat_hash_map<std::string_view, StatType>;
using StatTypeStrMap          = flat_hash_map<StatType, std::string_view>;
using MegaScaleStatTypeMap    = flat_hash_map<std::string_view, MegaScaleStatType>;
using MegaScaleStatTypeStrMap = flat_hash_map<MegaScaleStatType, std::string_view>;
using LineIdTypeMap           = flat_hash_map<std::string_view, LineIdType>;
using LineIdTypeStrMap        = flat_hash_map<LineIdType, std::string_view>;

const HostEventTypeMap& GetHostEventTypeMap()
{
    static auto* host_event_type_map = new HostEventTypeMap({
        {"UnknownHostEventType", kUnknownHostEventType},
        {"TraceContext", kTraceContext},
        {"SessionRun", kSessionRun},
        {"FunctionRun", kFunctionRun},
        {"RunGraph", kRunGraph},
        {"RunGraphDone", kRunGraphDone},
        {"TfOpRun", kTfOpRun},
        {"EagerExecute", kEagerKernelExecute},
        {"ExecutorState::Process", kExecutorStateProcess},
        {"ExecutorDoneCallback", kExecutorDoneCallback},
        {"MemoryAllocation", kMemoryAllocation},
        {"MemoryDeallocation", kMemoryDeallocation},
        // Performance counter related.
        {"RemotePerfCounter", kRemotePerf},
        // tf data captured function events.
        {"InstantiatedCapturedFunction::Run", kTfDataCapturedFunctionRun},
        {"InstantiatedCapturedFunction::RunWithBorrowedArgs",
         kTfDataCapturedFunctionRunWithBorrowedArgs},
        {"InstantiatedCapturedFunction::RunInstantiated", kTfDataCapturedFunctionRunInstantiated},
        {"InstantiatedCapturedFunction::RunAsync", kTfDataCapturedFunctionRunAsync},
        // Loop ops.
        {"ParallelForOp", kParallelForOp},
        {"ForeverOp", kForeverOp},
        {"WhileOp-EvalCond", kWhileOpEvalCond},
        {"WhileOp-StartBody", kWhileOpStartBody},
        {"ForOp", kForOp},
        // tf.data related.
        {"IteratorGetNextOp::DoCompute", kIteratorGetNextOp},
        {"IteratorGetNextAsOptionalOp::DoCompute", kIteratorGetNextAsOptionalOp},
        {"Iterator", kIterator},
        {"Iterator::Prefetch::Generator", kDeviceInputPipelineSecondIterator},
        {"PrefetchProduce", kPrefetchProduce},
        {"PrefetchConsume", kPrefetchConsume},
        {"ParallelInterleaveProduce", kParallelInterleaveProduce},
        {"ParallelInterleaveConsume", kParallelInterleaveConsume},
        {"ParallelInterleaveInitializeInput", kParallelInterleaveInitializedInput},
        {"ParallelMapProduce", kParallelMapProduce},
        {"ParallelMapConsume", kParallelMapConsume},
        {"MapAndBatchProduce", kMapAndBatchProduce},
        {"MapAndBatchConsume", kMapAndBatchConsume},
        {"ParseExampleProduce", kParseExampleProduce},
        {"ParseExampleConsume", kParseExampleConsume},
        {"ParallelBatchProduce", kParallelBatchProduce},
        {"ParallelBatchConsume", kParallelBatchConsume},
        // Batching related.
        {"BatchingSessionRun", kBatchingSessionRun},
        {"ProcessBatch", kProcessBatch},
        {"BrainSessionRun", kBrainSessionRun},
        {"ConcatInputTensors", kConcatInputTensors},
        {"MergeInputTensors", kMergeInputTensors},
        {"ScheduleWithoutSplit", kScheduleWithoutSplit},
        {"ScheduleWithSplit", kScheduleWithSplit},
        {"ScheduleWithEagerSplit", kScheduleWithEagerSplit},
        {"ASBSQueue::Schedule", kASBSQueueSchedule},
        // TFRT related.
        {"TfrtModelRun", kTfrtModelRun},
        // Serving related.
        {"ServingModelRun", kServingModelRun},
        // GPU related.
        {"KernelLaunch", kKernelLaunch},
        {"KernelExecute", kKernelExecute},
        // TPU related.
        {"EnqueueRequestLocked", kEnqueueRequestLocked},
        {"RunProgramRequest", kRunProgramRequest},
        {"HostCallbackRequest", kHostCallbackRequest},
        {"TransferH2DRequest", kTransferH2DRequest},
        {"TransferPreprocessedH2DRequest", kTransferPreprocessedH2DRequest},
        {"TransferD2HRequest", kTransferD2HRequest},
        {"OnDeviceSendRequest", kOnDeviceSendRequest},
        {"OnDeviceRecvRequest", kOnDeviceRecvRequest},
        {"OnDeviceSendRecvLocalRequest", kOnDeviceSendRecvLocalRequest},
        {"CustomWait", kCustomWait},
        {"OnDeviceSendRequestMulti", kOnDeviceSendRequestMulti},
        {"OnDeviceRecvRequestMulti", kOnDeviceRecvRequestMulti},
        {"PjrtAsyncWait", kPjrtAsyncWait},
        {"DoEnqueueProgram", kDoEnqueueProgram},
        {"DoEnqueueContinuationProgram", kDoEnqueueContinuationProgram},
        {"WriteHbm", kWriteHbm},
        {"ReadHbm", kReadHbm},
        {"TpuExecuteOp", kTpuExecuteOp},
        {"CompleteCallbacks", kCompleteCallbacks},
        {"TPUPartitionedCallOp-InitializeVarOnTPU", kTpuPartitionedCallOpInitializeVarOnTpu},
        {"TPUPartitionedCallOp-ExecuteRemote", kTpuPartitionedCallOpExecuteRemote},
        {"TPUPartitionedCallOp-ExecuteLocal", kTpuPartitionedCallOpExecuteLocal},
        {"Linearize", kLinearize},
        {"Delinearize", kDelinearize},
        {"TransferBufferFromDevice-FastPath", kTransferBufferFromDeviceFastPath},
        {"tpu::System::TransferToDevice=>IssueEvent", kTransferToDeviceIssueEvent},
        {"tpu::System::TransferToDevice=>IssueEvent=>Done", kTransferToDeviceDone},
        {"tpu::System::TransferFromDevice=>IssueEvent", kTransferFromDeviceIssueEvent},
        {"tpu::System::TransferFromDevice=>IssueEvent=>Done", kTransferFromDeviceDone},
        {"tpu::System::Execute", kTpuSystemExecute},
    });
    // PROFILER_CHECK_DEBUG(host_event_type_map->size() == kNumHostEventTypes);
    return *host_event_type_map;
}

const StatTypeMap& GetStatTypeMap()
{
    static auto* stat_type_map = new StatTypeMap(
        {{"UnknownStatType", kUnknownStatType},
         // TraceMe arguments.
         {"id", kStepId},
         {"device_ordinal", kDeviceOrdinal},
         {"chip_ordinal", kChipOrdinal},
         {"node_ordinal", kNodeOrdinal},
         {"model_id", kModelId},
         {"queue_addr", kQueueAddr},
         {"queue_id", kQueueId},
         {"request_id", kRequestId},
         {"run_id", kRunId},
         {"replica_id", kReplicaId},
         {"graph_type", kGraphType},
         {"step_num", kStepNum},
         {"iter_num", kIterNum},
         {"index_on_host", kIndexOnHost},
         {"allocator_name", kAllocatorName},
         {"bytes_reserved", kBytesReserved},
         {"bytes_allocated", kBytesAllocated},
         {"bytes_available", kBytesAvailable},
         {"fragmentation", kFragmentation},
         {"peak_bytes_in_use", kPeakBytesInUse},
         {"requested_bytes", kRequestedBytes},
         {"allocation_bytes", kAllocationBytes},
         {"addr", kAddress},
         {"region_type", kRegionType},
         {"data_type", kDataType},
         {"shape", kTensorShapes},
         {"layout", kTensorLayout},
         {"kpi_name", kKpiName},
         {"kpi_value", kKpiValue},
         {"element_id", kElementId},
         {"parent_id", kParentId},
         {"core_type", kCoreType},
         // XPlane semantics related.
         {"_pt", kProducerType},
         {"_ct", kConsumerType},
         {"_p", kProducerId},
         {"_c", kConsumerId},
         {"_r", kIsRoot},
         {"_a", kIsAsync},
         // device_option trace arguments.
         {"device_id", kDeviceId},
         {"device_type_string", kDeviceTypeString},
         {"context_id", kContextId},
         {"correlation_id", kCorrelationId},
         {"memcpy_details", kMemcpyDetails},
         {"memalloc_details", kMemallocDetails},
         {"MemFree_details", kMemFreeDetails},
         {"Memset_details", kMemsetDetails},
         {"MemoryResidency_details", kMemoryResidencyDetails},
         {"kernel_details", kKernelDetails},
         {"nvtx_range", kNVTXRange},
         {"stream", kStream},
         // Stats added when processing traces.
         {"group_id", kGroupId},
         {"flow", kFlow},
         {"step_name", kStepName},
         {"tf_op", kTfOp},
         {"hlo_op", kHloOp},
         {"deduplicated_name", kDeduplicatedName},
         {"hlo_category", kHloCategory},
         {"hlo_module", kHloModule},
         {"program_id", kProgramId},
         {"equation", kEquation},
         {"is_eager", kIsEager},
         {"is_func", kIsFunc},
         {"tf_function_call", kTfFunctionCall},
         {"tracing_count", kTfFunctionTracingCount},
         {"flops", kFlops},
         {"model_flops", kModelFlops},
         {"bytes_accessed", kBytesAccessed},
         {"memory_access_breakdown", kMemoryAccessBreakdown},
         {"source", kSourceInfo},
         {"model_name", kModelName},
         {"model_version", kModelVersion},
         {"bytes_transferred", kBytesTransferred},
         {"queue", kDmaQueue},
         {"dcn_collective_info", kDcnCollectiveInfo},
         // Performance counter related.
         {"Raw Value", kRawValue},
         {"Scaled Value", kScaledValue},
         {"Thread Id", kThreadId},
         {"matrix_unit_utilization_percent", kMatrixUnitUtilizationPercent},
         // XLA metadata map related.
         {"Hlo Proto", kHloProto},
         {"EdgeTPU Model information", kEdgeTpuModelInfo},
         {"EdgeTPU Model Profile information", kEdgeTpuModelProfileInfo},
         {"EdgeTPU MLIR", kEdgeTpuMlir},
         // device_option capability related.
         {"clock_rate", kDevCapClockRateKHz},
         {"core_count", kDevCapCoreCount},
         {"memory_bandwidth", kDevCapMemoryBandwidth},
         {"memory_size", kDevCapMemorySize},
         {"compute_cap_major", kDevCapComputeCapMajor},
         {"compute_cap_minor", kDevCapComputeCapMinor},
         {"peak_teraflops_per_second", kDevCapPeakTeraflopsPerSecond},
         {"peak_hbm_bw_gigabytes_per_second", kDevCapPeakHbmBwGigabytesPerSecond},
         {"peak_sram_rd_bw_gigabytes_per_second", kDevCapPeakSramRdBwGigabytesPerSecond},
         {"peak_sram_wr_bw_gigabytes_per_second", kDevCapPeakSramWrBwGigabytesPerSecond},
         {"device_vendor", kDevVendor},
         // Batching related.
         {"batch_size_after_padding", kBatchSizeAfterPadding},
         {"padding_amount", kPaddingAmount},
         {"batching_input_task_size", kBatchingInputTaskSize},
         // GPU related metrics.
         {"theoretical_occupancy_pct", kTheoreticalOccupancyPct},
         {"occupancy_min_grid_size", kOccupancyMinGridSize},
         {"occupancy_suggested_block_size", kOccupancySuggestedBlockSize},
         // Aggregated Stat
         {"self_duration_ps", kSelfDurationPs},
         {"min_duration_ps", kMinDurationPs},
         {"total_profile_duration_ps", kTotalProfileDurationPs},
         {"max_iteration_num", kMaxIterationNum},
         {"device_type", kDeviceType},
         {"uses_megacore", kUsesMegaCore},
         {"symbol_id", kSymbolId},
         {"hlo_category", kHloCategory},
         {"tf_op_name", kTfOpName},
         {"dma_stall_duration_ps", kDmaStallDurationPs},
         {"key", kKey},
         {"payload_size_bytes", kPayloadSizeBytes},
         {"duration_us", kDuration},
         {"buffer_size", kBufferSize},
         {"transfers", kTransfers},
         // Dcn message Stats
         {"dcn_label", kDcnLabel},
         {"dcn_source_slice_id", kDcnSourceSliceId},
         {"dcn_source_per_slice_device_id", kDcnSourcePerSliceDeviceId},
         {"dcn_destination_slice_id", kDcnDestinationSliceId},
         {"dcn_destination_per_slice_device_id", kDcnDestinationPerSliceDeviceId},
         {"dcn_chunk", kDcnChunk},
         {"dcn_loop_index", kDcnLoopIndex},
         {"dropped_traces", kDroppedTraces},
         {"cuda_graph_id", kCudaGraphId},
         {"cuda_graph_exec_id", kCudaGraphExecId},
         {"cuda_graph_orig_id", kCudaGraphOrigId},
         {"step_idle_time_ps", kStepIdleTimePs},
         {"gpu_device_name", kGpuDeviceName},
         {"source_stack", kSourceStack},
         {"device_offset_ps", kDeviceOffsetPs},
         {"device_duration_ps", kDeviceDurationPs}});
    // PROFILER_CHECK_DEBUG(stat_type_map->size() == kNumStatTypes);
    return *stat_type_map;
}

const MegaScaleStatTypeMap& GetMegaScaleStatTypeMap()
{
    static auto* stat_type_map = new MegaScaleStatTypeMap({
        {"graph_key", kMegaScaleGraphKey},
        {"local_device_id", kMegaScaleLocalDeviceId},
        {"num_actions", kMegaScaleNumActions},
        {"collective_type", kMegaScaleCollectiveType},
        {"input_size", kMegaScaleInputSize},
        {"slack_us", kMegaScaleSlackUs},
        {"action_type", kMegaScaleActionType},
        {"start_end_type", kMegaScaleStartEndType},
        {"action_index", kMegaScaleActionIndex},
        {"action_duration_ns", kMegaScaleActionDurationNs},
        {"action_inputs", kMegaScaleActionInputs},
        {"transfer_source", kMegaScaleTransferSource},
        {"transfer_destinations", kMegaScaleTransferDestinations},
        {"buffer_sizes", kMegaScaleBufferSizes},
        {"compute_operation", kMegaScaleComputeOperation},
        {"chunk", kMegaScaleChunk},
        {"launch_id", kMegaScaleLaunchId},
        {"loop_iteration", kMegaScaleLoopIteration},
        {"transmission_budget_us", kMegaScaleTransmissionBudgetUs},
        {"delay_budget_us", kMegaScaleDelayBudgetUs},
        {"graph_protos", kMegaScaleGraphProtos},
        {"network_transport_latency_us", kMegaScaleNetworkTransportLatency},
    });
    // PROFILER_CHECK_DEBUG(stat_type_map->size() == kNumMegaScaleStatTypes);
    return *stat_type_map;
}

const LineIdTypeMap& GetLineIdTypeMap()
{
    static auto* line_id_type_map = new LineIdTypeMap({
        {"UnknownLineIdType", kUnknownLineIdType},
        {"DcnHostTraffic", kDcnHostTraffic},
        {"DcnCollectiveTraffic", kDcnCollectiveTraffic},
    });
    // PROFILER_CHECK_DEBUG(line_id_type_map->size() == kNumLineIdTypes);
    return *line_id_type_map;
}

const HostEventTypeStrMap& GetHostEventTypeStrMap()
{
    static auto* host_event_type_str_map =
        new HostEventTypeStrMap(ReverseMap<HostEventTypeStrMap>(GetHostEventTypeMap()));
    return *host_event_type_str_map;
}

const StatTypeStrMap& GetStatTypeStrMap()
{
    static auto* stat_type_str_map =
        new StatTypeStrMap(ReverseMap<StatTypeStrMap>(GetStatTypeMap()));
    return *stat_type_str_map;
}

const MegaScaleStatTypeStrMap& GetMegaScaleStatTypeStrMap()
{
    static auto* stat_type_str_map =
        new MegaScaleStatTypeStrMap(ReverseMap<MegaScaleStatTypeStrMap>(GetMegaScaleStatTypeMap()));
    return *stat_type_str_map;
}

[[maybe_unused]] const LineIdTypeStrMap& GetLineIdTypeStrMap()
{
    static auto* line_id_type_str_map =
        new LineIdTypeStrMap(ReverseMap<LineIdTypeStrMap>(GetLineIdTypeMap()));
    return *line_id_type_str_map;
}

using TaskEnvStatTypeMap    = flat_hash_map<std::string_view, TaskEnvStatType>;
using TaskEnvStatTypeStrMap = flat_hash_map<TaskEnvStatType, std::string_view>;

PROFILER_UNUSED constexpr int kNumTaskEnvStatTypes =
    TaskEnvStatType::kLastTaskEnvStatType - TaskEnvStatType::kFirstTaskEnvStatType + 1;

const TaskEnvStatTypeMap& GetTaskEnvStatTypeMap()
{
    static auto* task_env_stat_type_map = new TaskEnvStatTypeMap({
        {"profile_start_time", kEnvProfileStartTime},
        {"profile_stop_time", kEnvProfileStopTime},
    });
    // PROFILER_CHECK_DEBUG(task_env_stat_type_map->size() == kNumTaskEnvStatTypes);
    return *task_env_stat_type_map;
}

const TaskEnvStatTypeStrMap& GetTaskEnvStatTypeStrMap()
{
    static auto* task_env_stat_type_str_map =
        new TaskEnvStatTypeStrMap(ReverseMap<TaskEnvStatTypeStrMap>(GetTaskEnvStatTypeMap()));
    return *task_env_stat_type_str_map;
}

}  // namespace

std::string_view GetHostEventTypeStr(HostEventType event_type)
{
    return GetHostEventTypeStrMap().at(event_type);
}

std::optional<int64_t> FindHostEventType(std::string_view event_name)
{
    if (const auto* event_type = FindOrNull(GetHostEventTypeMap(), event_name))
    {
        return *event_type;
    }
    return std::nullopt;
}

std::optional<int64_t> FindTfOpEventType(PROFILER_UNUSED std::string_view event_name)
{
// TF op names.
#if 0
    Category category = ParseTfOpFullname(event_name).category;
    switch (category)
    {
    case Category::kTensorFlow:
        return HostEventType::kTfOpRun;
    case Category::kTfData:
        return HostEventType::kIterator;
    default:
        return std::nullopt;
    }
#else
    return std::nullopt;
#endif
}

std::string_view GetStatTypeStr(StatType stat_type)
{
    return GetStatTypeStrMap().at(stat_type);
}

std::optional<int64_t> FindStatType(std::string_view stat_name)
{
    if (const auto* stat_type = FindOrNull(GetStatTypeMap(), stat_name))
    {
        return *stat_type;
    }
    return std::nullopt;
}

std::string_view GetMegaScaleStatTypeStr(MegaScaleStatType stat_type)
{
    return GetMegaScaleStatTypeStrMap().at(stat_type);
}

std::optional<int64_t> FindMegaScaleStatType(std::string_view stat_name)
{
    if (const auto* stat_type = FindOrNull(GetMegaScaleStatTypeMap(), stat_name))
    {
        return *stat_type;
    }
    return std::nullopt;
}

std::string_view GetTaskEnvStatTypeStr(TaskEnvStatType stat_type)
{
    return GetTaskEnvStatTypeStrMap().at(stat_type);
}

std::optional<int64_t> FindTaskEnvStatType(std::string_view stat_name)
{
    if (const auto* stat_type = FindOrNull(GetTaskEnvStatTypeMap(), stat_name))
    {
        return *stat_type;
    }
    return std::nullopt;
}

//static std::string_view GetLineIdTypeStr(LineIdType line_id_type)
//{
//    return GetLineIdTypeStrMap().at(line_id_type);
//}

bool IsInternalEvent(std::optional<int64_t> event_type)
{
    // TODO(b/162102421): Introduce a prefix for internal event names.
    if (!event_type.has_value())
    {
        return false;
    }
    switch (*event_type)
    {
    case HostEventType::kMemoryAllocation:
    case HostEventType::kMemoryDeallocation:
    case HostEventType::kPrefetchProduce:
    case HostEventType::kPrefetchConsume:
    case HostEventType::kParallelInterleaveProduce:
    case HostEventType::kParallelInterleaveConsume:
    case HostEventType::kParallelInterleaveInitializedInput:
    case HostEventType::kParallelMapProduce:
    case HostEventType::kParallelMapConsume:
    case HostEventType::kMapAndBatchProduce:
    case HostEventType::kMapAndBatchConsume:
    case HostEventType::kParseExampleProduce:
    case HostEventType::kParseExampleConsume:
        return true;
    default:
        return false;
    }
}

bool IsInternalStat(std::optional<int64_t> stat_type)
{
    // TODO(b/162102421): Introduce a prefix for internal stat names.
    if (!stat_type.has_value())
    {
        return false;
    }
    switch (*stat_type)
    {
    case StatType::kKernelDetails:
    case StatType::kProducerType:
    case StatType::kProducerId:
    case StatType::kConsumerType:
    case StatType::kConsumerId:
    case StatType::kIsRoot:
    case StatType::kFlops:
    case StatType::kBytesAccessed:
    case StatType::kProgramId:
    case StatType::kSymbolId:
        return true;
    default:
        return false;
    }
}

/*static*/ std::atomic<uint64_t> XFlow::next_flow_id_(0);

// String constants for XProf TraceMes.
//const std::string_view kMegaScaleDcnReceive        = "MegaScale: Communication Transport Receive";
//const std::string_view kMegaScaleDcnSend           = "MegaScale: Communication Transport Send";
//const std::string_view kMegaScaleDcnSendFinished   = "MegaScale: Send Finished";
//const std::string_view kMegaScaleDcnMemAllocate    = "MegaScale: Memory Allocate";
//const std::string_view kMegaScaleDcnMemCopy        = "MegaScale: Memory Copy";
//const std::string_view kMegaScaleTopologyDiscovery = "MegaScale: Communication Topology Discovery.";
//const std::string_view kMegaScaleBarrier           = "MegaScale: Barrier.";
//const std::string_view kMegaScaleHostCommand       = "MegaScale: HostCommandHandle";
//const std::string_view kMegaScaleD2HTransferStart  = "MegaScale: device_option to Host Action";
//const std::string_view kMegaScaleD2HTransferFinished =
//    "MegaScale: device_option to Host Transfer Finished";
//const std::string_view kMegaScaleH2DTransferStart = "MegaScale: Host to device_option Action";
//const std::string_view kMegaScaleH2DTransferFinished =
//    "MegaScale: Host to device_option Transfer Finished";
//const std::string_view kMegaScaleReductionStart        = "MegaScale: Reduction";
//const std::string_view kMegaScaleReductionFinished     = "MegaScale: Reduction Finished";
//const std::string_view kMegaScaleCompressionStart      = "MegaScale: Compression";
//const std::string_view kMegaScaleCompressionFinished   = "MegaScale: Compression Finished";
const std::string_view kMegaScaleDcnReceive        = "MegaScale: DCN Receive";
const std::string_view kMegaScaleDcnSend           = "MegaScale: DCN Send";
const std::string_view kMegaScaleDcnSendFinished   = "MegaScale: DCN Send Finished";
const std::string_view kMegaScaleDcnMemAllocate    = "MegaScale: DCN Memory Allocate";
const std::string_view kMegaScaleDcnMemCopy        = "MegaScale: DCN Memory Copy";
const std::string_view kMegaScaleTopologyDiscovery = "MegaScale: Topology Discovery";
const std::string_view kMegaScaleBarrier           = "MegaScale: Barrier";
const std::string_view kMegaScaleHostCommand       = "MegaScale: Host Command";
const std::string_view kMegaScaleD2HTransferStart  = "MegaScale: device_option to Host Transfer";
const std::string_view kMegaScaleD2HTransferFinished =
    "MegaScale: device_option to Host Transfer Finished";
const std::string_view kMegaScaleH2DTransferStart = "MegaScale: Host to device_option Transfer";
const std::string_view kMegaScaleH2DTransferFinished =
    "MegaScale: Host to device_option Transfer Finished";
const std::string_view kMegaScaleReductionStart        = "MegaScale: Reduction";
const std::string_view kMegaScaleReductionFinished     = "MegaScale: Reduction Finished";
const std::string_view kMegaScaleCompressionStart      = "MegaScale: Compression";
const std::string_view kMegaScaleCompressionFinished   = "MegaScale: Compression Finished";
const std::string_view kMegaScaleDecompressionStart    = "MegaScale: Decompression";
const std::string_view kMegaScaleDecompressionFinished = "MegaScale: Decompression Finished";
const char             kXProfMetadataKey[]             = "key";
const char             kXProfMetadataFlow[]            = "flow";
const char             kXProfMetadataTransfers[]       = "transfers";
const char             kXProfMetadataBufferSize[]      = "buffer_size";

// String constants for threadpool_listener.
const std::string_view kThreadpoolListenerRecord      = "ThreadpoolListener::Record";
const std::string_view kThreadpoolListenerStartRegion = "ThreadpoolListener::StartRegion";
const std::string_view kThreadpoolListenerStopRegion  = "ThreadpoolListener::StopRegion";
const std::string_view kThreadpoolListenerRegion      = "ThreadpoolListener::Region";
}  // namespace profiler
