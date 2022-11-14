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

layout (binding = 0) uniform sampler2D image;

layout (location = 0) in vec2 pos;

layout (location = 0) out vec2 uv;

float gamma(float x)
{
	if (x <= 0.0031308)
		return 12.92 * x;
	else
		return 1.055 * pow(x, 1/2.4) - 0.055;
}

vec4 gamma4(vec4 x)
{
	return vec4(gamma(x.r), gamma(x.g), gamma(x.b), gamma(x.a));
}

void main() {
	vec4 rgba = gamma4(texture(image, pos));
	uv.x = 0.5 + rgba.r * -0.1146 + rgba.g * -0.3854 + rgba.b *  0.5;
	uv.y = 0.5 + rgba.r *  0.5    + rgba.g * -0.4542 + rgba.b * -0.0458;
}

