// MainShader.slang
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

uniform GridParams grid;
RWTexture2D<uint> PartialQuadCounterUAV;
RWTexture2D<uint> PartialNonHelperLaneCounterUAV;

// === Pass 1: Counting pass ===
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
    
    uint ActiveLaneCount = WaveActiveCountBits(true); // !!! needs to be outside all branching
    uint WaveLaneCount = WaveGetLaneCount();
    uint HelperLaneCount = WaveLaneCount - ActiveLaneCount;
    
    if (HelperLaneCount > 0) // if there is any inactive lane (= helper-lane)
    {
        if (WaveIsFirstLane())
        {
            InterlockedAdd(PartialQuadCounterUAV[pixelPos], 1); // count wave as partial in the first lane's coord
        }
        InterlockedAdd(PartialNonHelperLaneCounterUAV[pixelPos], 1); // count each active fragment
    }
    
    // Output transparent - visualization happens in second pass
    return float4(0, 0, 0, 0);
}

// === Pass 2: Visualization pass ===
uniform bool averageQuad;

Texture2D<uint> PartialQuadCounter;
Texture2D<uint> PartialNonHelperLaneCounter;

float4 vsVisualization(uint vId : SV_VertexID) : SV_POSITION
{
    if (vId == 0)      return float4(-1, -1, 0, 1);
    else if (vId == 1) return float4( 3, -1, 0, 1);
    else               return float4(-1,  3, 0, 1);
}

float4 psVisualization(float4 posH : SV_POSITION) : SV_TARGET
{
    uint2 pixelPos = (uint2)posH.xy;
    
    // how many times was this quad partially rendered (high cap for helper-lane count)
    uint Wave_PartialQuadCount = WaveActiveSum(PartialQuadCounter[pixelPos]);
    // how many times was this fragment still active in a partially rendered quad
    uint Fragment_PartialNonHelperLaneCount = PartialNonHelperLaneCounter[pixelPos];
    uint Fragment_HelperLaneCount = Wave_PartialQuadCount - Fragment_PartialNonHelperLaneCount;
    
    if (averageQuad)
    {
        // sum the helper lanes written in the wave area
        uint Wave_HelperLaneCount = WaveActiveSum(Fragment_HelperLaneCount);
        // average the sum for the area
        uint ActiveWaveCount = WaveActiveCountBits(true);
        Fragment_HelperLaneCount = (Wave_HelperLaneCount + ActiveWaveCount - 1) / ActiveWaveCount;
    }
    
    if (Fragment_HelperLaneCount == 0)      return float4(0, 1, 0, 1); // green
    else if (Fragment_HelperLaneCount == 1) return float4(1, 1, 0, 1); // yellow
    else if (Fragment_HelperLaneCount == 3) return float4(0, 0, 1, 1); // blue
    else                                    return float4(1, 0, 0, 1); // red
}
