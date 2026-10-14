#include "PipelineStatistics.h"
#include "Core/API/LowLevelContextData.h"
#include "Core/API/NativeHandleTraits.h"
#if FALCOR_HAS_D3D12
#include <wrl/client.h>
#endif

struct PipelineStatistics::Data
{
#if FALCOR_HAS_D3D12
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> heap;
    Microsoft::WRL::ComPtr<ID3D12Resource> readback;
    ID3D12GraphicsCommandList* commandList = nullptr;
#endif
};

PipelineStatistics::PipelineStatistics(Falcor::Device* device) : mData(std::make_unique<Data>())
{
#if FALCOR_HAS_D3D12
    if (device->getNativeHandle().getType() != Falcor::NativeHandleType::ID3D12Device) return;
    auto native = device->getNativeHandle().as<ID3D12Device*>();
    D3D12_QUERY_HEAP_DESC query{};
    query.Type = D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS;
    query.Count = 1;
    if (FAILED(native->CreateQueryHeap(&query, IID_PPV_ARGS(&mData->heap)))) return;
    D3D12_HEAP_PROPERTIES properties{};
    properties.Type = D3D12_HEAP_TYPE_READBACK;
    properties.CreationNodeMask = properties.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC resource{};
    resource.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource.Width = sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS);
    resource.Height = resource.DepthOrArraySize = resource.MipLevels = resource.SampleDesc.Count = 1;
    resource.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    native->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &resource, D3D12_RESOURCE_STATE_COPY_DEST,
                                    nullptr, IID_PPV_ARGS(&mData->readback));
#else
    (void)device;
#endif
}
PipelineStatistics::~PipelineStatistics() = default;

bool PipelineStatistics::begin(Falcor::RenderContext* context)
{
#if FALCOR_HAS_D3D12
    if (!mData->readback) return false;
    auto low = context->getLowLevelData();
    low->getResourceCommandEncoder();
    low->closeEncoders();
    mData->commandList = low->getCommandBufferNativeHandle().as<ID3D12GraphicsCommandList*>();
    mData->commandList->BeginQuery(mData->heap.Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0);
    return true;
#else
    (void)context;
    return false;
#endif
}
void PipelineStatistics::endResolve(Falcor::RenderContext* context)
{
#if FALCOR_HAS_D3D12
    auto low = context->getLowLevelData();
    low->closeEncoders();
    auto native = low->getCommandBufferNativeHandle().as<ID3D12GraphicsCommandList*>();
    FALCOR_CHECK(native == mData->commandList, "Pipeline query must stay on one command list.");
    native->EndQuery(mData->heap.Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0);
    native->ResolveQueryData(mData->heap.Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0, 1, mData->readback.Get(), 0);
#else
    (void)context;
#endif
}
PipelineCounts PipelineStatistics::read() const
{
    PipelineCounts result;
#if FALCOR_HAS_D3D12
    if (!mData->readback) return result;
    D3D12_RANGE range{0, sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS)};
    void* mapped = nullptr;
    FALCOR_CHECK(SUCCEEDED(mData->readback->Map(0, &range, &mapped)), "Pipeline query readback failed.");
    const auto data = *static_cast<const D3D12_QUERY_DATA_PIPELINE_STATISTICS*>(mapped);
    const D3D12_RANGE written{0, 0};
    mData->readback->Unmap(0, &written);
    result = {true, data.IAPrimitives, data.VSInvocations, data.PSInvocations, data.CPrimitives};
#endif
    return result;
}
