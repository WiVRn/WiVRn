// Copyright 2022, Guillaume Meunier
// Copyright 2022, Patrick Nicolas
// SPDX-License-Identifier: BSL-1.0

#version 450

layout (location = 0) out vec2 outUV;

void main() {
    outUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2); // [(0, 0), (2, 0), (0, 2)]
    gl_Position = vec4(outUV * 2.0f + -1.0f, 0.0f, 1.0f); // [(-1, -1), (3, -1), (-1, 3)]
}
