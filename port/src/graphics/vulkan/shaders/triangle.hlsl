struct VertexIn { [[vk::location(0)]] float2 position : POSITION; [[vk::location(1)]] float3 color : COLOR; };
struct VertexOut { float4 position : SV_Position; [[vk::location(0)]] float3 color : COLOR; };
VertexOut VSMain(VertexIn input) { VertexOut result;result.position=float4(input.position,0,1);result.color=input.color;return result; }
float4 PSMain(VertexOut input) : SV_Target { return float4(input.color,1); }
