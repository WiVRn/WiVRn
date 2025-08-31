// Copyright (c) 2025 Hans-Kristian Arntzen
// SPDX-License-Identifier: MIT

#extension GL_EXT_control_flow_attributes : require

layout(location = 0) in vec2 vUV;
layout(location = 0, component = 2) in float vIntCoord;

layout(location = 0) out mediump float oY;
#if OUTPUT_PLANES == 2
layout(location = 1) out mediump vec2 oCbCr;
#elif OUTPUT_PLANES == 3
layout(location = 1) out mediump float oCb;
layout(location = 2) out mediump float oCr;
#endif

layout(set = 0, binding = 0) uniform mediump texture2D uYEven;
layout(set = 0, binding = 1) uniform mediump texture2D uYOdd;
layout(set = 0, binding = 2) uniform mediump sampler uSampler;
#if INPUT_PLANES == 3
layout(set = 0, binding = 3) uniform mediump texture2D uCbEven;
layout(set = 0, binding = 4) uniform mediump texture2D uCbOdd;
layout(set = 0, binding = 5) uniform mediump texture2D uCrEven;
layout(set = 0, binding = 6) uniform mediump texture2D uCrOdd;
#elif INPUT_PLANES == 2
layout(set = 0, binding = 3) uniform mediump texture2D uCbCrEven;
layout(set = 0, binding = 4) uniform mediump texture2D uCbCrOdd;
#endif

// Direct and naive implementing of the CDF 9/7 synthesis filters.
// Optimized for the mobile GPUs which don't have any
// competent compute/shared memory performance whatsoever,
// i.e. anything not AMD/NV/Intel.

layout(constant_id = 0) const bool VERTICAL = false;
layout(constant_id = 1) const bool FINAL_Y = false;
layout(constant_id = 2) const bool FINAL_CBCR = false;
layout(constant_id = 3) const int EDGE_CONDITION = 0;
const ivec2 OFFSET_M2 = VERTICAL ? ivec2(0, 0) : ivec2(0, 0);
const ivec2 OFFSET_M1 = VERTICAL ? ivec2(0, 1) : ivec2(1, 0);
const ivec2 OFFSET_C  = VERTICAL ? ivec2(0, 2) : ivec2(2, 0);
const ivec2 OFFSET_P1 = VERTICAL ? ivec2(0, 3) : ivec2(3, 0);
const ivec2 OFFSET_P2 = VERTICAL ? ivec2(0, 4) : ivec2(4, 0);

const float SYNTHESIS_LP_0 = 1.11508705;
const float SYNTHESIS_LP_1 = 0.591271763114;
const float SYNTHESIS_LP_2 = -0.057543526229;
const float SYNTHESIS_LP_3 = -0.091271763114;

const float SYNTHESIS_HP_0 = 0.602949018236;
const float SYNTHESIS_HP_1 = -0.266864118443;
const float SYNTHESIS_HP_2 = -0.078223266529;
const float SYNTHESIS_HP_3 = 0.016864118443;
const float SYNTHESIS_HP_4 = 0.026748757411;

layout(push_constant) uniform Registers
{
	vec2 uv_offset;
	vec2 half_texel_offset;
	float res_scale;
	int aligned_transform_size;
};

float[10] sample_component_gather(mediump texture2D tex_even, mediump texture2D tex_odd)
{
	float components[10];
	vec2 gather_uv = vUV + half_texel_offset;
	vec2 even0, even1, odd0, odd1;

	if (VERTICAL)
	{
		even0 = textureGatherOffset(sampler2D(tex_even, uSampler), gather_uv, OFFSET_M1).wx;
		even1 = textureGatherOffset(sampler2D(tex_even, uSampler), gather_uv, OFFSET_P1).wx;
		odd0 = textureGatherOffset(sampler2D(tex_odd, uSampler), gather_uv, OFFSET_M2).wx;
		odd1 = textureGatherOffset(sampler2D(tex_odd, uSampler), gather_uv, OFFSET_C).wx;
	}
	else
	{
		even0 = textureGatherOffset(sampler2D(tex_even, uSampler), gather_uv, OFFSET_M1).wz;
		even1 = textureGatherOffset(sampler2D(tex_even, uSampler), gather_uv, OFFSET_P1).wz;
		odd0 = textureGatherOffset(sampler2D(tex_odd, uSampler), gather_uv, OFFSET_M2).wz;
		odd1 = textureGatherOffset(sampler2D(tex_odd, uSampler), gather_uv, OFFSET_C).wz;
	}

	components[0] = 0.0;
	components[1] = odd0.x;
	components[2] = even0.x;
	components[3] = odd0.y;
	components[4] = even0.y;
	components[5] = odd1.x;
	components[6] = even1.x;
	components[7] = odd1.y;
	components[8] = even1.y;
	components[9] = textureLodOffset(sampler2D(tex_odd, uSampler), vUV, 0.0, OFFSET_P2).x;

	return components;
}

vec2[10] sample_component_gather2(mediump texture2D tex_even, mediump texture2D tex_odd)
{
	vec2 components[10];

	// Little point in using gather here, at least for now.
	components[0] = vec2(0.0);
	components[1] = textureLodOffset(sampler2D(tex_odd, uSampler), vUV, 0.0, OFFSET_M2).xy;
	components[2] = textureLodOffset(sampler2D(tex_even, uSampler), vUV, 0.0, OFFSET_M1).xy;
	components[3] = textureLodOffset(sampler2D(tex_odd, uSampler), vUV, 0.0, OFFSET_M1).xy;
	components[4] = textureLodOffset(sampler2D(tex_even, uSampler), vUV, 0.0, OFFSET_C).xy;
	components[5] = textureLodOffset(sampler2D(tex_odd, uSampler), vUV, 0.0, OFFSET_C).xy;
	components[6] = textureLodOffset(sampler2D(tex_even, uSampler), vUV, 0.0, OFFSET_P1).xy;
	components[7] = textureLodOffset(sampler2D(tex_odd, uSampler), vUV, 0.0, OFFSET_P1).xy;
	components[8] = textureLodOffset(sampler2D(tex_even, uSampler), vUV, 0.0, OFFSET_P2).xy;
	components[9] = textureLodOffset(sampler2D(tex_odd, uSampler), vUV, 0.0, OFFSET_P2).xy;

	return components;
}

void main()
{
	bool is_odd = (int(vIntCoord) & 1) != 0;

	float Y[10] = sample_component_gather(uYEven, uYOdd);
#if INPUT_PLANES == 2
	vec2 CbCr[10] = sample_component_gather2(uCbCrEven, uCbCrOdd);
#elif INPUT_PLANES == 3
	float Cb[10] = sample_component_gather(uCbEven, uCbOdd);
	float Cr[10] = sample_component_gather(uCrEven, uCrOdd);
	vec2 CbCr[10];
	[[unroll]]
	for (int i = 0; i < 10; i++)
		CbCr[i] = vec2(Cb[i], Cr[i]);
#endif

	if (EDGE_CONDITION < 0)
	{
		// The mirroring rules are particular.
		// For odd inputs we can rely on the mirrored sampling to get intended behavior.
		if (vIntCoord < 1.0)
		{
			// Y4 is the pivot.
			Y[2] = Y[6];
#if INPUT_SAMPLES > 1
			CbCr[2] = CbCr[6];
#endif
		}
	}
	else if (EDGE_CONDITION > 0)
	{
		if (vIntCoord + 2.0 > aligned_transform_size)
		{
			// We're on the last two pixels.
			// Y5 is the pivot. LP inputs behave as expected when using mirroring.
			Y[7] = Y[3];
			Y[9] = Y[1];
#if INPUT_SAMPLES > 1
			CbCr[7] = CbCr[3];
			CbCr[9] = CbCr[1];
#endif
		}
		else if (vIntCoord + 4.0 >= aligned_transform_size)
		{
			// Y7 is the pivot.
			Y[9] = Y[5];
#if INPUT_SAMPLES > 1
			CbCr[9] = CbCr[5];
#endif
		}
	}

#if INPUT_PLANES > 1
#define AccumT vec3
#define GenInput(comp) vec3(Y[comp], CbCr[comp])
#else
#define AccumT float
#define GenInput(comp) Y[comp]
#endif

	AccumT C0, C1, C2, C3, C4;
	float W0, W1, W2, W3, W4;

	// Not ideal, but gotta do what we gotta do.
	// GPU will have to take both paths here,
	// but at least we avoid dynamic load-store which is RIP perf on these chips ...
	if (is_odd)
	{
		C0 = GenInput(5);
		C1 = GenInput(4) + GenInput(6);
		C2 = GenInput(3) + GenInput(7);
		C3 = GenInput(2) + GenInput(8);
		C4 = GenInput(1) + GenInput(9);

		W0 = SYNTHESIS_HP_0;
		W1 = SYNTHESIS_LP_1;
		W2 = SYNTHESIS_HP_2;
		W3 = SYNTHESIS_LP_3;
		W4 = SYNTHESIS_HP_4;
	}
	else
	{
		C0 = GenInput(4);
		C1 = GenInput(3) + GenInput(5);
		C2 = GenInput(2) + GenInput(6);
		C3 = GenInput(1) + GenInput(7);
		C4 = AccumT(0.0);

		W0 = SYNTHESIS_LP_0;
		W1 = SYNTHESIS_HP_1;
		W2 = SYNTHESIS_LP_2;
		W3 = SYNTHESIS_HP_3;
		W4 = 0.0;
	}

	AccumT result = C0 * W0 + C1 * W1 + C2 * W2 + C3 * W3 + C4 * W4;

#if OUTPUT_PLANES == 3
	oY = result.x;
	oCb = result.y;
	oCr = result.z;
#elif OUTPUT_PLANES == 2
	oY = result.x;
	oCbCr = result.yz;
#else
	oY = result;
#endif

	if (FINAL_Y)
		oY += 0.5;

	if (FINAL_CBCR)
	{
#if OUTPUT_PLANES == 3
		oCb += 0.5;
		oCr += 0.5;
#elif OUTPUT_PLANES == 2
		oCbCr += 0.5;
#endif
	}
}
