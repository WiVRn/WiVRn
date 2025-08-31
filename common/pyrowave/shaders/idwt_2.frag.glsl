#version 450
// Copyright (c) 2025 Hans-Kristian Arntzen
// SPDX-License-Identifier: MIT

#extension GL_ARB_shading_language_include : require

#define OUTPUT_PLANES 3
#define INPUT_PLANES 2

#include "idwt.frag.h"
