#pragma once
#include "Falcor.h"
#include <memory>

struct PipelineCounts
{
    bool available = false;
    uint64_t inputPrimitives = 0, vertexInvocations = 0, pixelInvocations = 0, rasterPrimitives = 0;
};

// D3D12 queries around a separate, uninstrumented draw, outside GPU timing.
// read() requires the caller to have completed the containing command list.
class PipelineStatistics
{
public:
    explicit PipelineStatistics(Falcor::Device* device);
    ~PipelineStatistics();
    bool begin(Falcor::RenderContext* context);
    void endResolve(Falcor::RenderContext* context);
    PipelineCounts read() const;
private:
    struct Data;
    std::unique_ptr<Data> mData;
};
