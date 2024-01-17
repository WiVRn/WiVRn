#version 450

layout(binding = 0, rgba8) uniform readonly image2D rgb;
layout(binding = 1, r8) uniform writeonly image2D luminance;

#define SEMIPLANAR

#ifdef SEMIPLANAR
layout(binding = 2, rg8) uniform writeonly image2D chroma_uv;
#else
layout(binding = 2, r8) uniform writeonly image2D chroma_u;
layout(binding = 3, r8) uniform writeonly image2D chroma_v;
#endif

layout(push_constant) uniform PushConstants
{
	mat3 color_space;
}
pcs;

layout(constant_id = 0) const bool full_range = true;

layout(local_size_x = 16, local_size_y = 16) in;

vec3 rgb_to_ycbcr(vec3 color)
{
	vec3 yuv = transpose(pcs.color_space) * color;

	if (full_range)
	{
		return yuv + vec3(0, 0.5, 0.5);
	}
	else
	{
		// The matrix multiplication gives us Y in [0, 1] and Cb and Cr in [-0.5, 0.5].
		// This converts to "MPEG" or "Narrow" in the range [16, 235] and [16, 240].
		return vec3(
		        (219.0 * yuv.x + 16.0) / 256.0,
		        (224.0 * yuv.y + 128.0) / 256.0,
		        (224.0 * yuv.z + 128.0) / 256.0);
	}
}

void main()
{
	vec2 self_id = gl_GlobalInvocationID.xy;
	ivec2 coords = ivec2(self_id.x * 2, self_id.y * 2);
	ivec2 chroma_coords = coords / 2;

	int j, k;
	vec2 uvs[4];
	for (k = 0; k < 2; k += 1)
	{
		for (j = 0; j < 2; j += 1)
		{
			ivec2 texel_coords = coords + ivec2(j, k);
			vec4 texel = imageLoad(rgb, texel_coords);
			vec3 yuv = rgb_to_ycbcr(texel.rgb);

			imageStore(luminance, texel_coords, vec4(yuv.x));

			int i = k * 2 + j;
			uvs[i] = yuv.yz;
		}
	}

	vec2 uv = mix(mix(uvs[0], uvs[1], 0.5), mix(uvs[2], uvs[3], 0.5), 0.5);

#ifdef SEMIPLANAR
	imageStore(chroma_uv, chroma_coords, vec4(uv.x, uv.y, 0, 0));
#else
	imageStore(chroma_u, chroma_coords, vec4(u.x));
	imageStore(chroma_v, chroma_coords, vec4(v.y));
#endif
}
