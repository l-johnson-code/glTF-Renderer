struct PsIn {
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

struct PsOut {
    float4 color : SV_Target0;
};

PsOut main(PsIn input)
{
    PsOut output;
    output.color = input.color;
    return output;
}