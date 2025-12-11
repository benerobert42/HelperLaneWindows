// Poly.slang
struct VSIn
{
    float2 pos : POSITION;    // expected in [0..1] UV space
};

struct V2P
{
    float4 pos : SV_Position;
};

struct GridParams
{
    uint cols;
    uint rows;
    float2 cellSize;
    float2 origin;
    float scale;
};

GridParams grid;
RWTexture2D<uint> gHelperLaneCounter;

V2P vsMain(VSIn v, uint instanceId : SV_InstanceID)
{
    uint col = instanceId % grid.cols;
    uint row = instanceId / grid.cols;
    float2 cellOrigin = float2(col, row) * grid.cellSize + grid.origin;
    float2 local = v.pos * grid.scale;
    float2 worldPos = cellOrigin + local;
    float2 ndc = worldPos * 2.0 - 1.0;
    V2P o;
    o.pos = float4(ndc, 0.0, 1.0);
    return o;
}

float4 psMain(V2P input) : SV_Target
{
    uint2 pixelPos = (uint2)input.pos.xy;
    
    // Count helper lanes in the current quad using quad operations
    uint isHelperLane = IsHelperLane() ? 1u : 0u;
    uint quadHelperCount = 
        QuadReadLaneAt(isHelperLane, 0) +
        QuadReadLaneAt(isHelperLane, 1) +
        QuadReadLaneAt(isHelperLane, 2) +
        QuadReadLaneAt(isHelperLane, 3);
    
    // Determine if we're the top-left pixel of the quad (lane 0)
    // In a quad, lane 0 is the pixel with even x and even y coordinates
    bool isLane0 = ((pixelPos.x & 1) == 0) && ((pixelPos.y & 1) == 0);
    
    // Only the top-left pixel (lane 0) increments the counter to avoid race conditions
    if (isLane0 && quadHelperCount > 0)
    {
        InterlockedAdd(gHelperLaneCounter[pixelPos], quadHelperCount);
    }
    
    // Read the total accumulated count
    // This includes counts from all quads/instances that rendered to this pixel
    uint totalCount = gHelperLaneCounter[pixelPos];
    
    // Visualize based on total count
    if (totalCount == 0)      return float4(0, 1, 0, 1);  // green
    else if (totalCount == 1) return float4(1, 1, 0, 1);  // yellow
    else if (totalCount == 2) return float4(1, 0.5, 0.1, 1); // orange
    else                      return float4(1, 0, 0, 1);  // red
}
