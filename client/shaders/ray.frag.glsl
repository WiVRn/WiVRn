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

const int nb_views = 2;
const int nb_clipping = 4;

layout(set = 0, binding = 1) uniform mesh_ubo
{
    mat4 model;
    mat4 modelview[nb_views];
    mat4 modelviewproj[nb_views];
    vec4 clipping_plane[nb_clipping];

    // Extra data
    float emissive_factor;
} mesh;

layout(set = 0, binding = 3) uniform sampler2D base_color_texture;
layout(set = 0, binding = 6) uniform sampler2D emissive_texture;

struct texture_info
{
	int texcoord;
	float rotation;
	vec2 offset;
	vec2 scale;
};

layout(std140, set = 0, binding = 8) uniform material_ubo
{
    vec4 base_color_factor;
    vec4 base_emissive_factor;
    float metallic_factor;
    float roughness_factor;
    float occlusion_strength;
    float normal_scale;
    float alpha_cutoff;

    texture_info base_color;
    texture_info metallic_roughness;
    texture_info occlusion;
    texture_info emissive;
    texture_info normal;
} material;


layout(location = 4) in vec2 texcoord_base_color;
layout(location = 7) in vec2 texcoord_emissive;
layout(location = 9) in vec4 vertex_color;

const bool use_pbr = false;

// Fragment output
layout(location = 0) out vec4 out_color;
layout(early_fragment_tests) in;

void main()
{
    vec4 albedo = vertex_color * material.base_color_factor * texture(base_color_texture, texcoord_base_color);
    vec3 emissive_color = mesh.emissive_factor * texture(emissive_texture, texcoord_emissive).rgb;

    out_color = vec4(emissive_color, albedo.a);

    if (use_pbr)
        out_color = vec4(out_color.rgb / (out_color.rgb + 1), out_color.a);
}
