/*
 * WiVRn VR streaming
 * Copyright (C) 2022  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022  Patrick Nicolas <patricknicolas@laposte.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#version 450

#ifdef VERT_SHADER

vec2 positions[3] = vec2[](
        vec2(-1.0, -1.0),
        vec2(3.0, -1.0),
        vec2(-1.0, 3.0));

layout(location = 0) out vec2 outUV;

layout(set = 0, binding = 1) uniform UniformBufferObject
{
	mat4 reprojection;
}
ubo;

void main()
{
	//     outUV = vec2(ubo.reprojection * vec4(positions[gl_VertexIndex], 1.0, 1.0));
	outUV = positions[gl_VertexIndex];
	gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
}
#endif

#ifdef FRAG_SHADER
layout(set = 0, binding = 0) uniform sampler2D texSampler;

layout(set = 0, binding = 1) uniform UniformBufferObject
{
	mat4 reprojection;
}
ubo;

layout(location = 0) in vec2 inUV;

layout(location = 0) out vec4 outColor;

void main()
{
	vec4 tmp = ubo.reprojection * vec4(inUV, 1.0, 1.0);

	outColor = texture(texSampler, vec2(tmp) / tmp.z * 0.5 + vec2(0.5, 0.5));
}
#endif
