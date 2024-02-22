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

layout (constant_id = 0) const bool use_foveation_x = false;
layout (constant_id = 1) const bool use_foveation_y = false;
layout (constant_id = 2) const int nb_x = 1;
layout (constant_id = 3) const int nb_y = 1;

layout(set = 0, binding = 1) uniform UniformBufferObject
{
	mat4 reprojection;

	vec2 a;
	vec2 b;
	vec2 lambda;
	vec2 xc;
}
ubo;

vec2 reproject_and_unfoveate(vec2 uv)
{
	vec4 tmp = ubo.reprojection * vec4(uv, 1.0, 1.0);
	uv = vec2(tmp) / tmp.z; // between -1 and 1

	if (use_foveation_x && use_foveation_y)
	{
		uv = atan(ubo.lambda * (uv - ubo.xc)) / ubo.a - ubo.b;
	}
	else
	{
		if (use_foveation_x)
		{
			uv.x = (atan(ubo.lambda * (uv - ubo.xc)) / ubo.a - ubo.b).x;
		}
		if (use_foveation_y)
		{
			uv.y = (atan(ubo.lambda * (uv - ubo.xc)) / ubo.a - ubo.b).y;
		}
	}

	return uv * 0.5 + vec2(0.5, 0.5);
}

#ifdef VERT_SHADER

vec2 positions[6] = vec2[](
	vec2(0, 0), vec2(1, 0), vec2(0, 1),
	vec2(1, 0), vec2(0, 1), vec2(1, 1));

layout(location = 0) out vec2 outUV;

layout(location = 1) out vec4 test;



void main()
{
	vec2 quad_size = vec2(2, 2) / vec2(nb_x, nb_y);
	int cell_id = gl_VertexIndex / 6;

	vec2 top_left = quad_size * vec2(cell_id % nb_x, cell_id / nb_x) - 1.0;
	outUV = top_left + positions[gl_VertexIndex % 6] * quad_size;

	gl_Position = vec4(outUV, 0.0, 1.0);

	if (nb_x > 1 || nb_y > 1)
		outUV = reproject_and_unfoveate(outUV);
}
#endif

#ifdef FRAG_SHADER
layout(set = 0, binding = 0) uniform sampler2D texSampler;

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec4 test;

layout(location = 0) out vec4 outColor;

void main()
{
	vec2 uv;

	if (nb_x == 1 && nb_y == 1)
		uv = reproject_and_unfoveate(inUV);
	else
		uv = inUV;

	outColor = texture(texSampler, uv);

}
#endif

