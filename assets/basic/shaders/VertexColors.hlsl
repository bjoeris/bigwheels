// Copyright 2022 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.


struct TransformData
{
    float4x4 M;
};

ConstantBuffer<TransformData> Transform : register(b0);

struct VSOutput {
	float4 Position : SV_POSITION;
	float3 Color    : COLOR;
};

VSOutput vsmain(float4 Position : POSITION, float3 Color : COLOR)
{
	VSOutput result;
	result.Position = mul(Transform.M, Position);
	result.Color = Color;
	return result;
}

uint pcg_hash(uint input)
{
    uint state = input * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float4 psmain(VSOutput input) : SV_TARGET
{
	uint x = uint(input.Position.x + 0.5);
	uint y = uint(input.Position.y + 0.5);
	uint rand = pcg_hash(y * 10000 + x);
	float3 noise = float3(
		float(rand & 0xFF) * (1.0 / 255.0),
		float((rand<<8) & 0xFF) * (1.0 / 255.0),
		float((rand<<16) & 0xFF) * (1.0 / 255.0));

 	return float4(input.Color * 0.3 + noise * 0.7, 1);
}
