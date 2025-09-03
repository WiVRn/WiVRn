// Copyright (c) 2025 Hans-Kristian Arntzen
// SPDX-License-Identifier: MIT
#extension GL_KHR_shader_subgroup_basic : require
#extension GL_KHR_shader_subgroup_ballot : require
#extension GL_KHR_shader_subgroup_vote : require
#extension GL_KHR_shader_subgroup_arithmetic : require
#extension GL_KHR_shader_subgroup_shuffle_relative : require

#if STORAGE_MODE == 0
#extension GL_EXT_shader_8bit_storage : require
#extension GL_EXT_shader_16bit_storage : require
#else
#extension GL_EXT_samplerless_texture_functions : require
#endif

layout(local_size_x = 128) in;

layout(set = 0, binding = 0) writeonly uniform image2DArray uDequantImg;

layout(set = 0, binding = 1) readonly buffer PayloadOffsets
{
    uint data[];
} payload_offsets;

#if STORAGE_MODE == 0
layout(set = 0, binding = 2) readonly buffer Payloads
{
    uint data[];
} payload_data_u32;

layout(set = 0, binding = 2) readonly buffer Payloads16
{
    uint16_t data[];
} payload_data_u16;

layout(set = 0, binding = 2) readonly buffer Payloads8
{
    uint8_t data[];
} payload_data_u8;
#elif STORAGE_MODE == 1
layout(set = 0, binding = 2) uniform usamplerBuffer PayloadU32;
layout(set = 0, binding = 3) uniform mediump usamplerBuffer PayloadU16;
layout(set = 0, binding = 4) uniform mediump usamplerBuffer PayloadU8;
#else
layout(set = 0, binding = 2) uniform utexture2D PayloadU32;
layout(set = 0, binding = 3) uniform mediump utexture2D PayloadU16;
layout(set = 0, binding = 4) uniform mediump utexture2D PayloadU8;
#endif

#include "dwt_swizzle.inc"
#include "dwt_quant_scale.inc"
#include "constants.inc"

layout(push_constant) uniform Registers
{
    ivec2 resolution;
    int output_layer;
    int block_offset_32x32;
    int block_stride_32x32;
} registers;

#if STORAGE_MODE == 1
uint read_payload_u8(int coord)
{
	return texelFetch(PayloadU8, coord).x;
}

uint read_payload_u16(int coord)
{
	return texelFetch(PayloadU16, coord).x;
}

uint read_payload_u32(int coord)
{
	return texelFetch(PayloadU32, coord).x;
}
#elif STORAGE_MODE == 2
uint read_payload_u8(uint coord)
{
	uint x = bitfieldExtract(coord, 0, 12);
	uint y = bitfieldExtract(coord, 12, 20);
	return texelFetch(PayloadU8, ivec2(x, y), 0).x;
}

uint read_payload_u16(uint coord)
{
	uint x = bitfieldExtract(coord, 0, 11);
	uint y = bitfieldExtract(coord, 11, 21);
	return texelFetch(PayloadU16, ivec2(x, y), 0).x;
}

uint read_payload_u32(uint coord)
{
	uint x = bitfieldExtract(coord, 0, 10);
	uint y = bitfieldExtract(coord, 10, 22);
	return texelFetch(PayloadU32, ivec2(x, y), 0).x;
}
#endif

mat2x4 decode_payload(uint code_word, uint q_bits, uint offset, uint block_index)
{
    bool empty_block = code_word == 0;
    if (empty_block)
        return mat2x4(vec4(0.0), vec4(0.0));

    int bit_offset = 2 * int(block_index);

    // First, we need to compute the offset that our 4x2 block starts on.
    uint lsbs = code_word & 0x5555u;
    uint msbs = code_word & 0xaaaau;
    uint msbs_shift = msbs >> 1;
    msbs |= msbs_shift;

    uint byte_offset =
        bitCount(bitfieldExtract(lsbs, 0, bit_offset)) +
        bitCount(bitfieldExtract(msbs, 0, bit_offset)) +
        q_bits * block_index + offset;

#if STORAGE_MODE == 0
    // Eagerly load the data to keep latency down.
    // Also forces the descriptor to be loaded early.
    uint payload = uint(payload_data_u8.data[byte_offset]);
#else
	uint payload = read_payload_u8(int(byte_offset));
#endif

    uint local_control_word = bitfieldExtract(code_word, bit_offset, 2);
    int decoded_abs[8] = int[8](0, 0, 0, 0, 0, 0, 0, 0);
    int plane_iterations = int(q_bits + local_control_word);

    for (int q = plane_iterations - 1; q >= 0; q--)
    {
        for (int b = 0; b < 8; b++)
        {
            int decoded = int(bitfieldExtract(payload, b, 1));
            decoded_abs[b] = bitfieldInsert(decoded_abs[b], decoded, q, 1);
        }
        byte_offset++;
#if STORAGE_MODE == 0
        payload = uint(payload_data_u8.data[byte_offset]);
#else
        payload = read_payload_u8(int(byte_offset));
#endif
    }

    mat2x4 m;

    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < 2; j++)
        {
            float v = float(decoded_abs[i * 2 + j]);
            if (v != 0.0)
                v += 0.5;
            m[j][i] = v;
        }
    }

    return m;
}

shared uint shared_sign_offset;
shared uint shared_plane_byte_offsets[16];
shared uint shared_sign_scan[128 / 4];

const int MaxScaleExp = 4;

float decode_quant(uint quant_code)
{
    // Custom FP formulation for numbers in (0, 16) range.
    int e = MaxScaleExp - int(quant_code >> 3);
    int m = int(quant_code) & 0x7;
    float inv_quant = (1.0 / (8.0 * 1024.0 * 1024.0)) * float((8 + m) * (1 << (20 + e)));
    return inv_quant;
}

uint scan_subgroups(uint v)
{
    for (uint i = 1; i < gl_NumSubgroups; i *= 2)
    {
        uint up = subgroupShuffleUp(v, i);
        v += gl_SubgroupInvocationID >= i ? up : 0;
    }

    return v;
}

void scan_subgroups_fallback(uint local_index)
{
    barrier();

    // Slow LDS fallback for devices with wave size smaller than 16.
    bool active_lane = local_index < gl_NumSubgroups;

    uint v = 0;
    if (active_lane)
        v = shared_sign_scan[local_index];

    for (uint i = 1; i < gl_NumSubgroups; i *= 2)
    {
        uint up = 0;
        bool do_work = local_index >= i && active_lane;
        if (do_work)
            up = shared_sign_scan[local_index - i];

		// Resolve write-after-read hazard.
        barrier();

        if (do_work)
        {
            v += up;
            shared_sign_scan[local_index] = v;
        }

        barrier();
    }
}


void main()
{
    uint local_index = gl_SubgroupID * gl_SubgroupSize + gl_SubgroupInvocationID;

    int block_index_32x32 = int(registers.block_offset_32x32 +
        gl_WorkGroupID.y * registers.block_stride_32x32 +
        gl_WorkGroupID.x);

    uint block_local_index = bitfieldExtract(local_index, 0, 3);
    uint block_x = bitfieldExtract(local_index, 3, 2);
    uint block_y = bitfieldExtract(local_index, 5, 2);
    uint linear_block = block_y * 4 + block_x;

    // Each thread individually decodes 8 values.
    ivec2 local_coord = unswizzle8x8(block_local_index << 3);

    ivec2 coord = ivec2(gl_WorkGroupID.xy) * 32;
    coord += 8 * ivec2(block_x, block_y);
    coord += local_coord;

    uint offset_u32 = payload_offsets.data[block_index_32x32];

    if (offset_u32 == ~0u)
    {
        for (int j = 0; j < 2; j++)
            for (int i = 0; i < 4; i++)
                imageStore(uDequantImg, ivec3(coord + ivec2(i, j), registers.output_layer), vec4(0.0));
        return;
    }

#if STORAGE_MODE == 0
    uint ballot = payload_data_u32.data[offset_u32] & 0xffff;
    uint q_code = payload_data_u32.data[offset_u32 + 1] & 0xff;
#else
    uint ballot = read_payload_u16(2 * int(offset_u32));
    uint q_code = read_payload_u8(4 * int(offset_u32) + 4);
#endif

    if (local_index < 16)
    {
        uint control_word = 0;
        uint q_bits = 0;

        if (bitfieldExtract(ballot, int(local_index), 1) != 0)
        {
            uint local_code_offset = bitCount(bitfieldExtract(ballot, 0, int(local_index)));
#if STORAGE_MODE == 0
            control_word = uint(payload_data_u16.data[offset_u32 * 2 + 4 + local_code_offset]);
            q_bits = uint(payload_data_u8.data[offset_u32 * 4 + 8 + bitCount(ballot) * 2 + local_code_offset]) & 0xfu;
#else
            control_word = read_payload_u16(int(offset_u32 * 2 + 4 + local_code_offset));
            q_bits = read_payload_u8(int(offset_u32 * 4 + 8 + bitCount(ballot) * 2 + local_code_offset)) & 0xfu;
#endif
        }

        uint lsbs = control_word & 0x5555u;
        uint msbs = control_word & 0xaaaau;
        uint msbs_shift = msbs >> 1;
        msbs |= msbs_shift;
        uint byte_cost = bitCount(lsbs) + bitCount(msbs) + q_bits * 8;

        uint byte_scan = offset_u32 * 4 + 8 + 3 * bitCount(ballot) + subgroupInclusiveAdd(byte_cost);
        if (local_index == 15)
            shared_sign_offset = 8 * byte_scan;
        shared_plane_byte_offsets[local_index] = byte_scan - byte_cost;
    }

    barrier();

    mat2x4 v;
    int significant_count;

    if (bitfieldExtract(ballot, int(linear_block), 1) != 0)
    {
        uint local_code_offset = bitCount(bitfieldExtract(ballot, 0, int(linear_block)));

#if STORAGE_MODE == 0
        uint control_word = uint(payload_data_u16.data[offset_u32 * 2 + 4 + local_code_offset]);
        uint control_word2 = uint(payload_data_u8.data[offset_u32 * 4 + 8 + bitCount(ballot) * 2 + local_code_offset]);
#else
        uint control_word = read_payload_u16(int(offset_u32 * 2 + 4 + local_code_offset));
        uint control_word2 = read_payload_u8(int(offset_u32 * 4 + 8 + bitCount(ballot) * 2 + local_code_offset));
#endif

        v = decode_payload(control_word, control_word2 & 0xfu,
            shared_plane_byte_offsets[linear_block], block_local_index);

        significant_count = 0;
        for (int j = 0; j < 2; j++)
            for (int i = 0; i < 4; i++)
                significant_count += int(v[j][i] != 0.0);

        float q = decode_quant(q_code);
        float inv_scale = q * decode_quant_scale(bitfieldExtract(control_word2, QUANT_SCALE_OFFSET - 16, QUANT_SCALE_BITS));

        v *= inv_scale;
    }
    else
    {
        v = mat2x4(vec4(0.0), vec4(0.0));
        significant_count = 0;
    }

    // Figure out how many significant coefficients we have.
    int significant_scan = subgroupInclusiveAdd(significant_count);
    if (gl_SubgroupInvocationID == gl_SubgroupSize - 1)
        shared_sign_scan[gl_SubgroupID] = significant_scan;

    if (gl_NumSubgroups <= 8)
    {
        barrier();
		if (gl_SubgroupSize <= 32)
		{
			// Should be more robust since not all compilers properly understand the shuffle up pattern.
			// AMD is known to understand it well.
			if (local_index < gl_NumSubgroups)
				shared_sign_scan[local_index] = subgroupInclusiveAdd(shared_sign_scan[local_index]);
		}
		else
		{
			if (local_index < gl_NumSubgroups)
				shared_sign_scan[local_index] = scan_subgroups(shared_sign_scan[local_index]);
		}
        barrier();
    }
    else
    {
        scan_subgroups_fallback(local_index);
    }

    // Compute where we need to start reading sign bits from.
    uint sign_offset = shared_sign_offset + significant_scan - significant_count;
    if (gl_SubgroupID != 0)
        sign_offset += shared_sign_scan[gl_SubgroupID - 1];

    // Read out all sign bits we could possibly access per thread.
    // On AMD at least, this 64-bit load should be vectorizable.
#if STORAGE_MODE == 0
    uint sign_word = payload_data_u32.data[sign_offset / 32 + 0];
    uint sign_word_upper = payload_data_u32.data[sign_offset / 32 + 1];
#else
    uint sign_word = read_payload_u32(int(sign_offset / 32 + 0));
    uint sign_word_upper = read_payload_u32(int(sign_offset / 32 + 1));
#endif

    uint masked_sign_offset = sign_offset & 31u;
    if (masked_sign_offset != 0)
    {
        sign_word >>= masked_sign_offset;
        sign_word |= sign_word_upper << (32 - masked_sign_offset);
    }

    int sign_counter = 0;

    // Clock out the sign bits as needed.
    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < 2; j++)
        {
            if (v[j][i] != 0.0)
            {
                v[j][i] *= 1.0 - 2.0 * float(bitfieldExtract(sign_word, sign_counter, 1));
                sign_counter++;
            }
        }
    }

    // Write output.
    for (int j = 0; j < 2; j++)
        for (int i = 0; i < 4; i++)
            imageStore(uDequantImg, ivec3(coord + ivec2(i, j), registers.output_layer), vec4(v[j][i]));
}
