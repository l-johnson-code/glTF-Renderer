struct VSIn {
    uint id: SV_VertexID;
};

struct VSOut {
    float4 pos: SV_POSITION;
    float2 uv: TEXCOORD;
};

VSOut main(VSIn input)
{
    // Create a fullscreen triangle.
    VSOut output;
    float right = (input.id & 1);
    float top = ((input.id >> 1) & 1);
    output.pos.x = -1. + 4. * right;
    output.pos.y = -1. + 4. * top;
    output.pos.z = 0.;
    output.pos.w = 1.;
    output.uv.x = 2. * right;
    output.uv.y = 1. - 2. * top;
    return output;
}