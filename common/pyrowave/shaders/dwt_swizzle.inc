// Copyright (c) 2025 Hans-Kristian Arntzen
// SPDX-License-Identifier: MIT
#ifndef DWT_SWIZZLE_H_
#define DWT_SWIZZLE_H_

ivec2 unswizzle4x8(uint index)
{
	uint y = bitfieldExtract(index, 0, 1);
	uint x = bitfieldExtract(index, 1, 2);
	y |= bitfieldExtract(index, 3, 2) << 1;
	return ivec2(x, y);
}

ivec2 unswizzle8x8(uint index)
{
	uint y = bitfieldExtract(index, 0, 1);
	uint x = bitfieldExtract(index, 1, 2);
	y |= bitfieldExtract(index, 3, 2) << 1;
	x |= bitfieldExtract(index, 5, 1) << 2;
	return ivec2(x, y);
}

#endif