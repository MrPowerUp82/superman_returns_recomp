// Capture the exact final raster clip from the original vertex shader.
// No inferred camera matrix, skinning approximation or projection inversion.
struct Input {float4 position : SV_Position;};
struct Output {float4 position : SV_Position;float4 clip : TEXCOORD15;};
[maxvertexcount(3)]
void ExtractClip(triangle Input input[3],inout PointStream<Output> stream) {
  [unroll] for(uint i=0;i<3;++i) {
    Output output;output.position=input[i].position;output.clip=input[i].position;
    stream.Append(output);
  }
}
