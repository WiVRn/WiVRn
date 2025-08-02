layout(local_size_x = 64) in;
layout(constant_id = 0) const bool DCShift = false;

uint local_index;

#include "dwt_common.h"

layout(set = 0, binding = 0) uniform mediump sampler2DArray uTexture;
layout(set = 0, binding = 1) writeonly mediump uniform image2D uOutput;

layout(push_constant) uniform Registers
{
	ivec2 resolution;
	vec2 inv_resolution;
};

vec2 generate_mirror_uv(ivec2 coord, bool even_x, bool even_y)
{
	coord -= ivec2(band(bvec2(even_x, even_y), lessThan(coord, ivec2(0))));
	coord += 1;
	coord += ivec2(band(bvec2(!even_x, !even_y), greaterThanEqual(coord, resolution)));
	vec2 uv = vec2(coord) * inv_resolution;
	return uv.yx; // Transpose on load.
}

void write_shared_4x4(ivec2 coord, VEC4 texels0, VEC4 texels1, VEC4 texels2, VEC4 texels3)
{
	store_shared(coord.y + 0, 2 * coord.x + 0, VEC2(texels0.x, texels2.x));
	store_shared(coord.y + 0, 2 * coord.x + 1, VEC2(texels1.x, texels3.x));
	store_shared(coord.y + 0, 2 * coord.x + 2, VEC2(texels0.y, texels2.y));
	store_shared(coord.y + 0, 2 * coord.x + 3, VEC2(texels1.y, texels3.y));
	store_shared(coord.y + 1, 2 * coord.x + 0, VEC2(texels0.z, texels2.z));
	store_shared(coord.y + 1, 2 * coord.x + 1, VEC2(texels1.z, texels3.z));
	store_shared(coord.y + 1, 2 * coord.x + 2, VEC2(texels0.w, texels2.w));
	store_shared(coord.y + 1, 2 * coord.x + 3, VEC2(texels1.w, texels3.w));
}

void load_image_with_apron()
{
	ivec2 base_coord = ivec2(gl_WorkGroupID.xy) * ivec2(BLOCK_SIZE_HALF) - APRON_HALF;
	ivec2 local_coord0 = 2 * unswizzle8x8(local_index);
	ivec2 coord0 = base_coord + local_coord0;

	// Transpose on load.
	VEC4 texels0 = VEC4(textureGather(uTexture, vec3(generate_mirror_uv(coord0, true, true), 0.0), 0)).wxzy;
	VEC4 texels1 = VEC4(textureGather(uTexture, vec3(generate_mirror_uv(coord0, false, true), 2.0), 0)).wxzy;
	VEC4 texels2 = VEC4(textureGather(uTexture, vec3(generate_mirror_uv(coord0, true, false), 1.0), 0)).wxzy;
	VEC4 texels3 = VEC4(textureGather(uTexture, vec3(generate_mirror_uv(coord0, false, false), 3.0), 0)).wxzy;
	write_shared_4x4(local_coord0, texels0, texels1, texels2, texels3);

	ivec2 local_coord_horiz = ivec2(BLOCK_SIZE_HALF + 2 * (local_index % 2u), 2 * (local_index / 2u));
	if (local_coord_horiz.y < BLOCK_SIZE_HALF + 2 * APRON_HALF)
	{
		texels0 = VEC4(textureGather(uTexture, vec3(generate_mirror_uv(base_coord + local_coord_horiz, true, true), 0.0), 0)).wxzy;
		texels1 = VEC4(textureGather(uTexture, vec3(generate_mirror_uv(base_coord + local_coord_horiz, false, true), 2.0), 0)).wxzy;
		texels2 = VEC4(textureGather(uTexture, vec3(generate_mirror_uv(base_coord + local_coord_horiz, true, false), 1.0), 0)).wxzy;
		texels3 = VEC4(textureGather(uTexture, vec3(generate_mirror_uv(base_coord + local_coord_horiz, false, false), 3.0), 0)).wxzy;
		write_shared_4x4(local_coord_horiz, texels0, texels1, texels2, texels3);
	}

	ivec2 local_coord_vert = local_coord_horiz.yx;
	if (local_coord_vert.x < BLOCK_SIZE_HALF)
	{
		texels0 = VEC4(textureGather(uTexture, vec3(generate_mirror_uv(base_coord + local_coord_vert, true, true), 0.0), 0)).wxzy;
		texels1 = VEC4(textureGather(uTexture, vec3(generate_mirror_uv(base_coord + local_coord_vert, false, true), 2.0), 0)).wxzy;
		texels2 = VEC4(textureGather(uTexture, vec3(generate_mirror_uv(base_coord + local_coord_vert, true, false), 1.0), 0)).wxzy;
		texels3 = VEC4(textureGather(uTexture, vec3(generate_mirror_uv(base_coord + local_coord_vert, false, false), 3.0), 0)).wxzy;
		write_shared_4x4(local_coord_vert, texels0, texels1, texels2, texels3);
	}

	barrier();
}

void inverse_transform8x2()
{
	const int SIZE = 8;
	const int PADDED_SIZE = SIZE + 2 * APRON;
	const int PADDED_SIZE_HALF = PADDED_SIZE / 2;
	VEC2 values[PADDED_SIZE];

	ivec2 local_coord = ivec2(8 * (local_index % 4u), local_index / 4u);

	for (int i = 0; i < PADDED_SIZE; i += 2)
	{
		VEC2 v0 = load_shared(local_coord.y, local_coord.x + i + 0);
		VEC2 v1 = load_shared(local_coord.y, local_coord.x + i + 1);
		values[i + 0] = v0 * K;
		values[i + 1] = v1 * inv_K;
	}

	// CDF 9/7 lifting steps.
	// Arith go brrr.
	for (int i = 2; i < PADDED_SIZE - 1; i += 2)
		values[i] -= DELTA * (values[i - 1] + values[i + 1]);
	for (int i = 3; i < PADDED_SIZE - 2; i += 2)
		values[i] -= GAMMA * (values[i - 1] + values[i + 1]);
	for (int i = 4; i < PADDED_SIZE - 3; i += 2)
		values[i] -= BETA * (values[i - 1] + values[i + 1]);
	for (int i = 5; i < PADDED_SIZE - 4; i += 2)
		values[i] -= ALPHA * (values[i - 1] + values[i + 1]);

	// Avoid WAR hazard.
	barrier();

	for (int i = APRON_HALF; i < PADDED_SIZE_HALF - APRON_HALF; i++)
	{
		VEC2 a = values[2 * i + 0];
		VEC2 b = values[2 * i + 1];

		// Transpose the 2x2 block.
		VEC2 t0 = VEC2(a.x, b.x);
		VEC2 t1 = VEC2(a.y, b.y);

		// Transpose write
		int y_coord = (local_coord.x >> 1) + (i - APRON_HALF);
		store_shared(y_coord, 2 * local_coord.y + 0, t0);
		store_shared(y_coord, 2 * local_coord.y + 1, t1);
	}
}

void inverse_transform4x2(bool active_lane, int y_offset)
{
	const int SIZE = 4;
	const int PADDED_SIZE = SIZE + 2 * APRON;
	const int PADDED_SIZE_HALF = PADDED_SIZE / 2;
	VEC2 values[PADDED_SIZE];

	ivec2 local_coord = ivec2(4 * (local_index % 8u), local_index / 8u + y_offset);

	if (active_lane)
	{
		for (int i = 0; i < PADDED_SIZE; i += 2)
		{
			VEC2 v0 = load_shared(local_coord.y, local_coord.x + i + 0);
			VEC2 v1 = load_shared(local_coord.y, local_coord.x + i + 1);
			values[i + 0] = v0 * K;
			values[i + 1] = v1 * inv_K;
		}

		// CDF 9/7 lifting steps.
		// Arith go brrr.
		for (int i = 2; i < PADDED_SIZE - 1; i += 2)
			values[i] -= DELTA * (values[i - 1] + values[i + 1]);
		for (int i = 3; i < PADDED_SIZE - 2; i += 2)
			values[i] -= GAMMA * (values[i - 1] + values[i + 1]);
		for (int i = 4; i < PADDED_SIZE - 3; i += 2)
			values[i] -= BETA * (values[i - 1] + values[i + 1]);
		for (int i = 5; i < PADDED_SIZE - 4; i += 2)
			values[i] -= ALPHA * (values[i - 1] + values[i + 1]);
	}

	// Avoid WAR hazard.
	barrier();

	if (active_lane)
	{
		for (int i = APRON_HALF; i < PADDED_SIZE_HALF - APRON_HALF; i++)
		{
			VEC2 a = values[2 * i + 0];
			VEC2 b = values[2 * i + 1];

			// Transpose the 2x2 block.
			VEC2 t0 = VEC2(a.x, b.x);
			VEC2 t1 = VEC2(a.y, b.y);

			// Transpose write
			int y_coord = (local_coord.x >> 1) + (i - APRON_HALF);
			store_shared(y_coord, 2 * local_coord.y + 0, t0);
			store_shared(y_coord, 2 * local_coord.y + 1, t1);
		}
	}
}

void main()
{
	local_index = gl_SubgroupID * gl_SubgroupSize + gl_SubgroupInvocationID;

	load_image_with_apron();

	// Horizontal transform.
	inverse_transform8x2();

	// Also need to transform the apron.
	inverse_transform4x2(local_index < 32, BLOCK_SIZE_HALF);

	barrier();

	// Vertical transform.
	inverse_transform8x2();

	barrier();

	ivec2 local_coord = unswizzle8x8(local_index);

	for (int y = local_coord.y; y < BLOCK_SIZE_HALF; y += 8)
	{
		for (int x = local_coord.x; x < BLOCK_SIZE; x += 8)
		{
			VEC2 v = load_shared(y, x);
			if (DCShift)
				v += FLOAT(0.5);
			imageStore(uOutput, ivec2(2 * y + 0, x) + BLOCK_SIZE * ivec2(gl_WorkGroupID.yx), v.xxxx);
			imageStore(uOutput, ivec2(2 * y + 1, x) + BLOCK_SIZE * ivec2(gl_WorkGroupID.yx), v.yyyy);
		}
	}
}
