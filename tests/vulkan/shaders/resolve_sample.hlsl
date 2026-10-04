// Project-owned fixture reads the production per-draw descriptor arrays.
[[vk::binding(0,1)]] Texture2D<float4> Images[32];
[[vk::binding(0,2)]] SamplerState Samplers[32];
float4 PSMain():SV_Target0 {return Images[0].SampleLevel(Samplers[7],float2(.5,.5),0);}
