layout(local_size_x = 64) in;

layout(set = 0, binding = 0) uniform mediump sampler2D uTexture;
layout(set = 0, binding = 1) writeonly uniform mediump image2DArray uOutput;

layout(constant_id = 0) const bool DCShift = false;

layout(push_constant) uniform Registers
{
	ivec2 resolution;
	vec2 inv_resolution;
	ivec2 aligned_resolution;
};

uint local_index;

#include "dwt_common.inc"

vec2 generate_mirror_uv(ivec2 coord)
{
	coord -= ivec2(lessThan(coord, ivec2(0)));
	coord += 1;
	ivec2 end_mirrored_clamp = (2 * aligned_resolution) - resolution;
	ivec2 past_wrapped_coord = coord + 2 * (resolution - aligned_resolution) + 1;
	coord = mix(min(coord, resolution), past_wrapped_coord, greaterThanEqual(coord, end_mirrored_clamp));

	return vec2(coord) * inv_resolution;
}

void load_image_with_apron()
{
	ivec2 base_coord = ivec2(gl_WorkGroupID.xy) * ivec2(BLOCK_SIZE, BLOCK_SIZE) - APRON;
	ivec2 local_coord0 = 2 * unswizzle8x8(local_index);
	ivec2 coord0 = base_coord + local_coord0;

	VEC4 texels0 = VEC4(textureGather(uTexture, generate_mirror_uv(coord0))).wzxy;
	VEC4 texels1 = VEC4(textureGather(uTexture, generate_mirror_uv(coord0 + ivec2(16, 0)))).wzxy;
	VEC4 texels2 = VEC4(textureGather(uTexture, generate_mirror_uv(coord0 + ivec2(0, 16)))).wzxy;
	VEC4 texels3 = VEC4(textureGather(uTexture, generate_mirror_uv(coord0 + ivec2(16, 16)))).wzxy;
	if (DCShift)
	{
		texels0 -= FLOAT(0.5);
		texels1 -= FLOAT(0.5);
		texels2 -= FLOAT(0.5);
		texels3 -= FLOAT(0.5);
	}

	int local_coord0_y_half = local_coord0.y >> 1;

	// Pack two lines together in one vec2. This allows packed FP16 math easily by processing two lines in parallel.
	store_shared(local_coord0_y_half + 0, local_coord0.x + 0, texels0.xz);
	store_shared(local_coord0_y_half + 0, local_coord0.x + 1, texels0.yw);
	store_shared(local_coord0_y_half + 0, local_coord0.x + 16, texels1.xz);
	store_shared(local_coord0_y_half + 0, local_coord0.x + 17, texels1.yw);
	store_shared(local_coord0_y_half + 8, local_coord0.x + 0, texels2.xz);
	store_shared(local_coord0_y_half + 8, local_coord0.x + 1, texels2.yw);
	store_shared(local_coord0_y_half + 8, local_coord0.x + 16, texels3.xz);
	store_shared(local_coord0_y_half + 8, local_coord0.x + 17, texels3.yw);

	// Load the top-right apron
	{
		ivec2 local_coord = ivec2(BLOCK_SIZE + 2 * (local_index % 4u), 2 * (local_index / 4u));
		VEC4 texels = VEC4(textureGather(uTexture, generate_mirror_uv(base_coord + local_coord))).wzxy;
		if (DCShift)
		{
			texels -= FLOAT(0.5);
		}
		store_shared(local_coord.y >> 1, local_coord.x + 0, texels.xz);
		store_shared(local_coord.y >> 1, local_coord.x + 1, texels.yw);
	}

	// Load the bottom-left apron
	{
		ivec2 local_coord = ivec2(2 * (local_index % 16u), BLOCK_SIZE + 2 * (local_index / 16u));
		VEC4 texels = VEC4(textureGather(uTexture, generate_mirror_uv(base_coord + local_coord))).wzxy;
		if (DCShift)
		{
			texels -= FLOAT(0.5);
		}
		store_shared(local_coord.y >> 1, local_coord.x + 0, texels.xz);
		store_shared(local_coord.y >> 1, local_coord.x + 1, texels.yw);
	}

	if (local_index < 16)
	{
		// Load the bottom-right apron
		ivec2 local_coord = ivec2(BLOCK_SIZE + 2 * (local_index % 4u), BLOCK_SIZE + 2 * (local_index / 4u));
		VEC4 texels = VEC4(textureGather(uTexture, generate_mirror_uv(base_coord + local_coord))).wzxy;
		if (DCShift)
		{
			texels -= FLOAT(0.5);
		}
		store_shared(local_coord.y >> 1, local_coord.x + 0, texels.xz);
		store_shared(local_coord.y >> 1, local_coord.x + 1, texels.yw);
	}
}

void forward_transform8x2()
{
	const int SIZE = 8;
	const int PADDED_SIZE = SIZE + 2 * APRON;
	const int PADDED_SIZE_HALF = PADDED_SIZE / 2;
	VEC2 values[PADDED_SIZE];

	ivec2 local_coord = ivec2(8 * (local_index % 4u), local_index / 4u);

	for (int i = 0; i < PADDED_SIZE; i++)
	{
		VEC2 v = load_shared(local_coord.y, local_coord.x + i);
		values[i] = v;
	}

	// CDF 9/7 lifting steps.
	// Arith go brrr.
	for (int i = 1; i < PADDED_SIZE - 1; i += 2)
		values[i] += ALPHA * (values[i - 1] + values[i + 1]);
	for (int i = 2; i < PADDED_SIZE - 2; i += 2)
		values[i] += BETA * (values[i - 1] + values[i + 1]);
	for (int i = 3; i < PADDED_SIZE - 3; i += 2)
		values[i] += GAMMA * (values[i - 1] + values[i + 1]);
	for (int i = 4; i < PADDED_SIZE - 4; i += 2)
		values[i] += DELTA * (values[i - 1] + values[i + 1]);

	// Avoid WAR hazard.
	barrier();

	for (int i = APRON_HALF; i < PADDED_SIZE_HALF - APRON_HALF; i++)
	{
		VEC2 a = values[2 * i + 0];
		VEC2 b = values[2 * i + 1];

		// Filter kernel rescale.
		a *= inv_K;
		b *= K;

		// Transpose the 2x2 block.
		VEC2 t0 = VEC2(a.x, b.x);
		VEC2 t1 = VEC2(a.y, b.y);

		// Transpose write
		int y_coord = (local_coord.x >> 1) + (i - APRON_HALF);
		store_shared(y_coord, 2 * local_coord.y + 0, t0);
		store_shared(y_coord, 2 * local_coord.y + 1, t1);
	}
}

void forward_transform4x2(bool active_lane, int y_offset)
{
	const int SIZE = 4;
	const int PADDED_SIZE = SIZE + 2 * APRON;
	const int PADDED_SIZE_HALF = PADDED_SIZE / 2;
	VEC2 values[PADDED_SIZE];

	ivec2 local_coord = ivec2(4 * (local_index % 8u), local_index / 8u + y_offset);

	if (active_lane)
	{
		for (int i = 0; i < PADDED_SIZE; i++)
		{
			VEC2 v = load_shared(local_coord.y, local_coord.x + i);
			values[i] = v;
		}

		// CDF 9/7 lifting steps.
		// Arith go brrr.
		for (int i = 1; i < PADDED_SIZE - 1; i += 2)
			values[i] += ALPHA * (values[i - 1] + values[i + 1]);
		for (int i = 2; i < PADDED_SIZE - 2; i += 2)
			values[i] += BETA * (values[i - 1] + values[i + 1]);
		for (int i = 3; i < PADDED_SIZE - 3; i += 2)
			values[i] += GAMMA * (values[i - 1] + values[i + 1]);
		for (int i = 4; i < PADDED_SIZE - 4; i += 2)
			values[i] += DELTA * (values[i - 1] + values[i + 1]);
	}

	// Avoid WAR hazard.
	barrier();

	if (active_lane)
	{
		for (int i = APRON_HALF; i < PADDED_SIZE_HALF - APRON_HALF; i++)
		{
			VEC2 a = values[2 * i + 0];
			VEC2 b = values[2 * i + 1];

			// Filter kernel rescale.
			a *= inv_K;
			b *= K;

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

	barrier();

	// Horizontal transform.
	forward_transform8x2();

	// Also need to transform the apron.
	forward_transform4x2(local_index < 32, BLOCK_SIZE_HALF);

	barrier();

	// Vertical transform.
	forward_transform8x2();

	barrier();

	ivec2 local_coord = unswizzle8x8(local_index);
	for (int y = local_coord.y; y < BLOCK_SIZE_HALF; y += 8)
	{
		for (int x = local_coord.x * 2; x < BLOCK_SIZE; x += 16)
		{
			VEC2 v0 = load_shared(y, x + 0);
			VEC2 v1 = load_shared(y, x + 1);

			int img_x = x >> 1;
			int img_y = y;

			ivec2 base_image_coord = ivec2(gl_WorkGroupID.xy) * (BLOCK_SIZE / 2) + ivec2(img_x, img_y);
			imageStore(uOutput, ivec3(base_image_coord, 0), v0.xxxx);
			imageStore(uOutput, ivec3(base_image_coord, 2), v0.yyyy);
			imageStore(uOutput, ivec3(base_image_coord, 1), v1.xxxx);
			imageStore(uOutput, ivec3(base_image_coord, 3), v1.yyyy);
		}
	}
}
