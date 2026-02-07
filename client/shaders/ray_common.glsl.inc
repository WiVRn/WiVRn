// kate: hl glsl

/*
 * WiVRn VR streaming
 * Copyright (C) 2026  Guillaume Meunier <guillaume.meunier@centraliens.net>
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

const int nb_views = 2;
const int nb_texcoords = 2;
const int nb_windows = 4;
const bool use_pbr = false;

layout(set = 0, binding = 0) uniform scene_ubo
{
    mat4 view[nb_views];
    vec4 light_position;
    vec4 ambient_color;
    vec4 light_color;
} scene;

layout(set = 0, binding = 1) uniform mesh_ubo
{
    mat4 model;
    mat4 modelview[nb_views];
    mat4 modelviewproj[nb_views];

    // Extra data
    mat4 world_to_window[nb_windows];
    float emissive_factor;
} mesh;

layout(set = 0, binding = 2) uniform joints_ubo
{
    mat4 joint_matrices[32];
} joints;

layout(set = 0, binding = 3) uniform sampler2D base_color_texture;
layout(set = 0, binding = 4) uniform sampler2D metallic_roughness_texture;
layout(set = 0, binding = 5) uniform sampler2D occlusion_texture;
layout(set = 0, binding = 6) uniform sampler2D emissive_texture;
layout(set = 0, binding = 7) uniform sampler2D normal_map_texture;

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

#ifdef VERT_SHADER
// Vertex input
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec4 in_tangent;
layout(location = 3) in vec2 in_texcoord[2];
layout(location = 5) in vec4 in_color;

#if SKINNED
layout(location = 6) in vec4 in_joints;
layout(location = 7) in vec4 in_weights;
#endif

#endif

#ifdef FRAG_SHADER
#define VS_TO_FS in
#else
#define VS_TO_FS out
#endif

// Vertex-to-fragment
layout(location = 0) VS_TO_FS vec2 texcoord;
layout(location = 1) VS_TO_FS vec4 position_wrt_window[nb_windows];
layout(location = 1 + nb_windows) VS_TO_FS vec4 base_wrt_window[nb_windows];

#ifdef VERT_SHADER
out gl_PerVertex
{
    vec4 gl_Position;
};

void main()
{
    texcoord = in_texcoord[0];

#if SKINNED
    mat4 skinMatrix =
        in_weights.x * joints.joint_matrices[int(in_joints.x)] +
            in_weights.y * joints.joint_matrices[int(in_joints.y)] +
            in_weights.z * joints.joint_matrices[int(in_joints.z)] +
            in_weights.w * joints.joint_matrices[int(in_joints.w)];

    gl_Position = mesh.modelviewproj[gl_ViewIndex] * skinMatrix * vec4(in_position, 1.0);
    vec4 world_position = mesh.model * skinMatrix * vec4(in_position, 1.0);
    vec4 base_position = mesh.model * skinMatrix * vec4(in_position.xy, 0.0, 1.0);
#else
    gl_Position = mesh.modelviewproj[gl_ViewIndex] * vec4(in_position, 1.0);
    vec4 world_position = mesh.model * vec4(in_position, 1.0);
    vec4 base_position = mesh.model * vec4(in_position.xy, 0.0, 1.0);
#endif

    for(uint i = 0; i < nb_windows; i++)
    {
        position_wrt_window[i] = mesh.world_to_window[i] * world_position;
        base_wrt_window[i] = mesh.world_to_window[i] * base_position;
    }
}
#endif

#ifdef FRAG_SHADER
// Fragment output
layout(location = 0) out vec4 out_color;

void main()
{
    for (uint i = 0; i < nb_windows; i++)
    {
        if (position_wrt_window[i].w < 0.5)
            continue;

        // TODO check NaN
        float lambda = -base_wrt_window[i].z / (position_wrt_window[i].z - base_wrt_window[i].z);

        if (lambda < 0 || lambda > 1)
            continue;

        vec2 intersection = base_wrt_window[i].xy + lambda * (position_wrt_window[i].xy - base_wrt_window[i].xy);
        if (abs(intersection.x) < 0.5 && abs(intersection.y) < 0.5)
            discard;
    }

    vec4 albedo = material.base_color_factor * texture(base_color_texture, texcoord);
    vec3 emissive_color = mesh.emissive_factor * texture(emissive_texture, texcoord).rgb;

    out_color = vec4(emissive_color, albedo.a);

    if (use_pbr)
        out_color = vec4(out_color.rgb / (out_color.rgb + 1), out_color.a);
}
#endif
