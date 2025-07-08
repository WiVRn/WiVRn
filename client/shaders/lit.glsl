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

layout (constant_id = 0) const int nb_texcoords = 2;
layout (constant_id = 1) const bool dithering = true;
layout (constant_id = 2) const bool alpha_cutout = false;
layout (constant_id = 3) const bool skinning = false;

const int nb_clipping = 4;

const float fog_min_dist = 20.0;
const float fog_max_dist = 35.0;
const vec4 fog_color = vec4(0.0, 0.25, 0.5, 1.0);

layout(set = 0, binding = 0) uniform scene_ssbo
{
	mat4 view;
	mat4 proj;
	vec4 light_position;
	vec4 ambient_color;
	vec4 light_color;
} scene;

layout(set = 0, binding = 1) uniform mesh_ssbo
{
	mat4 model;
	mat4 modelview;
	mat4 modelviewproj;
	vec4 clipping_plane[nb_clipping];
} mesh;

layout(set = 0, binding = 2) uniform joints_ssbo
{
	mat4 joint_matrices[32];
} joints;

#ifdef FRAG_SHADER
layout(set = 1, binding = 0) uniform sampler2D base_color;
layout(set = 1, binding = 1) uniform sampler2D metallic_roughness;
layout(set = 1, binding = 2) uniform sampler2D occlusion;
layout(set = 1, binding = 3) uniform sampler2D emissive;
layout(set = 1, binding = 4) uniform sampler2D normal_map;
layout(set = 1, binding = 5) uniform material_ubo
{
	vec4 base_color_factor;
	vec4 base_emissive_factor;
	float metallic_factor;
	float roughness_factor;
	float occlusion_strength;
	float normal_scale;

	int base_color_texcoord;
	int metallic_roughness_texcoord;
	int occlusion_texcoord;
	int emissive_texcoord;
	int normal_texcoord;
} material;
#endif

// Vertex input
#ifdef VERT_SHADER
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec3 in_tangent;
layout(location = 3) in vec2 in_texcoord[2];
layout(location = 5) in vec4 in_color;
layout(location = 6) in vec4 in_joints;
layout(location = 7) in vec4 in_weights;
#endif

// Vertex-to-fragment
#ifdef VERT_SHADER
#define VERT_TO_FRAG out
#else
#define VERT_TO_FRAG in
#endif

layout(location = 0) VERT_TO_FRAG vec3 normal;
layout(location = 1) VERT_TO_FRAG vec4 frag_pos;
layout(location = 2) VERT_TO_FRAG vec4 light_pos;
layout(location = 3) VERT_TO_FRAG vec2 texcoord[nb_texcoords];

// Fragment output
#ifdef FRAG_SHADER
layout(location = 0) out vec4 out_color;
#endif

// Shader code
#ifdef VERT_SHADER

out gl_PerVertex
{
	vec4 gl_Position;
	float gl_ClipDistance[nb_clipping];
};

void main()
{
	// TODO: use base_color_texcoord et al instead of always using texcoord 0
	for(int i = 0; i < nb_texcoords; i++)
		texcoord[i] = in_texcoord[i];

	if (skinning)
	{
		mat4 skinMatrix =
			in_weights.x * joints.joint_matrices[int(in_joints.x)] +
			in_weights.y * joints.joint_matrices[int(in_joints.y)] +
			in_weights.z * joints.joint_matrices[int(in_joints.z)] +
			in_weights.w * joints.joint_matrices[int(in_joints.w)];

		normal = vec3(mesh.modelview * skinMatrix * vec4(in_normal, 0.0));
		gl_Position = mesh.modelviewproj * skinMatrix * vec4(in_position, 1.0);
	}
	else
	{
		normal = vec3(mesh.modelview * vec4(in_normal, 0.0));
		gl_Position = mesh.modelviewproj * vec4(in_position, 1.0);
	}
	frag_pos = mesh.modelview * vec4(in_position, 1.0);
	light_pos = scene.view * scene.light_position;

	for(int i = 0; i < nb_clipping; i++)
	{
		gl_ClipDistance[i] = dot(mesh.clipping_plane[i], mesh.model * vec4(in_position, 1.0));
	}

}

#endif

#ifdef FRAG_SHADER

const float dither_pattern[4][4] = {
	{ 0.0f, 0.5f, 0.125f, 0.625f},
	{ 0.75f, 0.25f, 0.875f, 0.375f},
	{ 0.1875f, 0.6875f, 0.0625f, 0.5625},
	{ 0.9375f, 0.4375f, 0.8125f, 0.3125}
};

vec4 one_over_d_linear_to_srgb(vec4 color)
{
	/* Linear to sRGB is:
	 *
	 * if (x <= 0.00031308)
	 *     return 12.92 * x;
	 * else
	 *     return 1.055*pow(x, 1.0 / 2.4) - 0.055;
	 *
	 * Its derivative is:
	 * if (x <= 0.00031308)
	 *     return 12.92;
	 * else
	 *     return (1.055 / 2.4) * pow(x, -1.4 / 2.4);
	 *
	 */

	// Alpha is not converted to sRGB
	vec3 tmp1 = vec3(1.0 / 12.92, 1.0 / 12.92, 1.0 / 12.92);
	vec3 tmp2 = (2.4 / 1.055) * vec3(pow(color.r, 1.4 / 2.4), pow(color.g, 1.4 / 2.4), pow(color.b, 1.4 / 2.4));

	return vec4(mix(tmp1, tmp2, vec3(greaterThan(color.rgb, vec3(0.00031308, 0.00031308, 0.00031308)))), 1.0);
}

void main()
{
	vec3 light_dir = normalize(light_pos.xyz - frag_pos.xyz * light_pos.w);
	vec3 view_dir = normalize(frag_pos.xyz);
	vec3 normal_unit = normalize(normal);
	vec3 reflect_dir = reflect(-light_dir, normal_unit);

	// Ambient lighting
	vec3 ambient = vec3(scene.ambient_color);

	// Diffuse lighting
	vec3 diffuse = max(dot(normal_unit, light_dir), 0.0) * vec3(scene.light_color);

	// Specular lighting
// 	vec3 half_dir = normalize(light_dir + view_dir);
// 	float spec_angle = max(dot(half_dir, normal_unit), 0.0);
// 	vec3 specular = pow(spec_angle, instance.specular_power) * instance.specular_strength * instance.light_color;

	vec3 light = ambient + diffuse /*+ specular*/;

	vec4 bc = material.base_color_factor * texture(base_color, fract(texcoord[0])) * vec4(light, 1.0);

	vec4 ec = material.base_emissive_factor * texture(emissive, fract(texcoord[0]));

	float fog = clamp((length(frag_pos) - fog_min_dist) / (fog_max_dist - fog_min_dist), 0.0, 1.0);

	bc = mix(bc + ec, fog_color, fog);

	if (dithering)
	{
		ivec2 tmp = ivec2(gl_FragCoord.xy) % 4;
		float dither_thd = 1.0f - dither_pattern[tmp.x][tmp.y];

		out_color = bc + one_over_d_linear_to_srgb(bc) * vec4(dither_thd, dither_thd, dither_thd, dither_thd) / 255.0f;
	}
	else
		out_color = bc;
}
#endif
