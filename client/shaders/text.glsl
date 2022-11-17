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

layout (std140, push_constant) uniform UBO
{
    mat4 model_view_projection;
} instance;

vec2 uv[6] = vec2[](
    vec2(0.0, 0.0),
    vec2(1.0, 0.0),
    vec2(0.0, 1.0),

    vec2(1.0, 0.0),
    vec2(0.0, 1.0),
    vec2(1.0, 1.0)
);


layout (location = 0) out vec2 outUV;

void main()
{
	vec4 position = vec4(uv[gl_VertexIndex], 0.0, 1.0);
	outUV = vec2(uv[gl_VertexIndex].x, 1.0 - uv[gl_VertexIndex].y);

	gl_Position = instance.model_view_projection * position;
}
#endif


#ifdef FRAG_SHADER
layout(set = 0, binding = 0) uniform sampler2D texSampler;
layout(location = 0) in vec2 texcoord0;

layout(location = 0) out vec4 outColor;

void main()
{
	outColor = vec4(texture(texSampler, texcoord0).rrr, 1.0);
}
#endif
