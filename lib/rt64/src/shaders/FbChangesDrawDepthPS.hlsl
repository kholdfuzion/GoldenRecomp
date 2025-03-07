//
// RT64
//

struct FbChangesDrawCommonCB {
    uint2 Resolution;
};

[[vk::push_constant]] ConstantBuffer<FbChangesDrawCommonCB> gConstants : register(b0);
Texture2D<float> gDepth : register(t2);
Texture2D<uint> gBoolean : register(t3);

float4 PSMain(in float4 pos : SV_Position, in float2 uv : TEXCOORD0, out float resultDepth : SV_DEPTH) : SV_TARGET {
    uint pixelChanged = gBoolean.Load(uint3(uv * gConstants.Resolution, 0));
    if (pixelChanged == 0) {
        discard;
    }

    resultDepth = gDepth.Load(uint3(uv * gConstants.Resolution, 0));
    return 0.0f;
}