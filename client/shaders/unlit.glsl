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
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_texcoord0;

layout(set = 1, binding = 0) uniform UBO
{
	mat4 model_view;
	mat4 projection;
}
instance;

layout(location = 0) out vec4 normal;
layout(location = 1) out vec2 texcoord0;

void main()
{
	normal = instance.model_view * vec4(in_normal, 0.0);
	texcoord0 = texcoord0;
	gl_Position = instance.projection * instance.model_view * vec4(in_position, 1.0);
}

#endif

#ifdef FRAG_SHADER
layout(set = 0, binding = 0) uniform sampler2D base_color;

layout(set = 1, binding = 0) uniform UBO
{
	mat4 model_view;
	mat4 projection;
}
instance;

layout(location = 0) in vec4 normal;
layout(location = 1) in vec2 texcoord0;

void main()
{
}
#endif
