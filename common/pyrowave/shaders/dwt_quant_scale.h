// Copyright (c) 2025 Hans-Kristian Arntzen
// SPDX-License-Identifier: MIT
#ifndef DWT_QUANT_SCALE_H_
#define DWT_QUANT_SCALE_H_

float decode_quant_scale(uint code)
{
	// Minimum scale: 0.25
	// Maximum scale: ~2.2
	return float(code) / 8.0 + 0.25;
}

const uint ENCODE_QUANT_IDENTITY = 6;

uint encode_quant_scale(float scale)
{
	// Round the quant scale FP up so that the quantizer scale effectively rounds down.
	return uint(ceil((scale - 0.25) * 8.0));
}

#endif
