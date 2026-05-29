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
const int nb_windows = 4;

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

layout(set = 0, binding = 6) uniform sampler2D emissive_texture;



#ifdef VERT_SHADER
// Vertex input
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_texcoord;

#if SKINNED
layout(location = 2) in vec4 in_joints;
layout(location = 3) in vec4 in_weights;
#endif

#endif

#ifdef FRAG_SHADER
#define VS_TO_FS in
#else
#define VS_TO_FS out
#endif

// Vertex-to-fragment
layout(location = 0) VS_TO_FS vec2 texcoord;
#ifndef HTC_WORKAROUND
layout(location = 1) VS_TO_FS vec4 position_wrt_window[nb_windows];
layout(location = 1 + nb_windows) VS_TO_FS vec4 base_wrt_window[nb_windows];
#endif // HTC_WORKAROUND

#ifdef VERT_SHADER
out gl_PerVertex
{
    vec4 gl_Position;
#ifdef HTC_WORKAROUND
    float gl_ClipDistance[nb_windows];
#endif
};

void main()
{
    texcoord = in_texcoord;

#if SKINNED
    mat4 skinMatrix =
        in_weights.x * joints.joint_matrices[int(in_joints.x)] +
            in_weights.y * joints.joint_matrices[int(in_joints.y)] +
            in_weights.z * joints.joint_matrices[int(in_joints.z)] +
            in_weights.w * joints.joint_matrices[int(in_joints.w)];

    gl_Position = mesh.modelviewproj[gl_ViewIndex] * skinMatrix * vec4(in_position, 1.0);
    vec4 world_position = mesh.model * skinMatrix * vec4(in_position, 1.0);
    vec4 base_position = mesh.model * skinMatrix * vec4(in_position.xy, 0.0, 1.0);
#else // SKINNED
    gl_Position = mesh.modelviewproj[gl_ViewIndex] * vec4(in_position, 1.0);
    vec4 world_position = mesh.model * vec4(in_position, 1.0);
    vec4 base_position = mesh.model * vec4(in_position.xy, 0.0, 1.0);
#endif // SKINNED

    for(uint i = 0; i < nb_windows; i++)
    {
#ifdef HTC_WORKAROUND
        vec4 tmp = mesh.world_to_window[i] * world_position;
        gl_ClipDistance[i] = tmp.z;
#else // HTC_WORKAROUND
        position_wrt_window[i] = mesh.world_to_window[i] * world_position;
        base_wrt_window[i] = mesh.world_to_window[i] * base_position;
#endif // HTC_WORKAROUND
    }
}
#endif // VERT_SHADER

#ifdef FRAG_SHADER
// Fragment output
layout(location = 0) out vec4 out_color;

void main()
{
#ifdef HTC_WORKAROUND
    out_color = vec4(mesh.emissive_factor * texture(emissive_texture, texcoord).rgb, 1.0 / 255.0);
#else // HTC_WORKAROUND
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

    out_color = vec4(mesh.emissive_factor * texture(emissive_texture, texcoord).rgb, 0);
#endif // HTC_WORKAROUND

}
#endif // FRAG_SHADER
