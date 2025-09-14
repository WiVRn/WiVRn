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

#extension GL_EXT_multiview : enable

layout (constant_id = 0) const int nb_texcoords = 2;
layout (constant_id = 1) const bool dithering = true;
layout (constant_id = 2) const bool alpha_cutout = false;
layout (constant_id = 3) const bool skinning = false;

const int nb_views = 2;
const int nb_clipping = 4;

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
        vec4 clipping_plane[nb_clipping];
} mesh;

layout(set = 0, binding = 2) uniform joints_ubo
{
        mat4 joint_matrices[32];
} joints;

#ifdef FRAG_SHADER
layout(set = 0, binding = 3) uniform sampler2D base_color;
layout(set = 0, binding = 4) uniform sampler2D metallic_roughness;
layout(set = 0, binding = 5) uniform sampler2D occlusion;
layout(set = 0, binding = 6) uniform sampler2D emissive;
layout(set = 0, binding = 7) uniform sampler2D normal_map;
layout(set = 0, binding = 8) uniform material_ubo
{
        vec4 base_color_factor;
        vec4 base_emissive_factor;
        float metallic_factor;
        float roughness_factor;
        float occlusion_strength;
        float normal_scale;
        float alpha_cutoff;

        int base_color_texcoord;
        int metallic_roughness_texcoord;
        int occlusion_texcoord;
        int emissive_texcoord;
        int normal_texcoord;
} material;
#endif

// Vertex input
#ifdef VERT_SHADER
layout(location = 0) in vec4 in_position;
layout(location = 1) in vec4 in_color;
#endif

// Vertex-to-fragment
#ifdef VERT_SHADER
#define VERT_TO_FRAG out
#else
#define VERT_TO_FRAG in
#endif

layout(location = 0) VERT_TO_FRAG vec4 color;

// Shader code
#ifdef VERT_SHADER
out gl_PerVertex
{
        vec4 gl_Position;
};

void main()
{
        gl_Position = mesh.modelviewproj[gl_ViewIndex] * in_position;
        color = in_color;
}
#endif

#ifdef FRAG_SHADER
layout(location = 0) out vec4 out_color;
layout(early_fragment_tests) in;
void main()
{
        out_color = color;
}
#endif
