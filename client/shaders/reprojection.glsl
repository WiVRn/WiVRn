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

#extension GL_EXT_fragment_shading_rate: require
#ifdef VERT_SHADER

layout (location = 0) in vec2 vPosition;
layout (location = 1) in vec2 vUV;
layout (location = 2) in int vShadingRate;

layout(location = 0) out vec2 outUV;

void main()
{
	gl_Position = vec4(vPosition, 0.0, 1.0);
	outUV = vUV;
	gl_PrimitiveShadingRateEXT = vShadingRate;
}
#endif

#ifdef FRAG_SHADER

layout(set = 0, binding = 0) uniform sampler2D texSampler;

layout(location = 0) in vec2 inUV;

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = texture(texSampler, inUV).rgba;
}

#endif
