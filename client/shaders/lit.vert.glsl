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

#extension GL_EXT_multiview : require
#extension GL_GOOGLE_include_directive : require

#include "common.glsl.inc"

// Vertex input
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec4 in_tangent;
layout(location = 3) in vec2 in_texcoord[2];
layout(location = 5) in vec4 in_color;

out gl_PerVertex
{
    vec4 gl_Position;
    float gl_ClipDistance[nb_clipping];
};

void main()
{
    texcoord_base_color = compute_texcoord(material.base_color, in_texcoord[material.base_color.texcoord]);
    texcoord_metallic_roughness = compute_texcoord(material.metallic_roughness, in_texcoord[material.metallic_roughness.texcoord]);
    texcoord_occlusion = compute_texcoord(material.occlusion, in_texcoord[material.occlusion.texcoord]);
    texcoord_emissive = compute_texcoord(material.emissive, in_texcoord[material.emissive.texcoord]);
    texcoord_normal = compute_texcoord(material.normal, in_texcoord[material.normal.texcoord]);

    // TODO on CPU?
    mat3 mv = inverse(transpose(mat3(mesh.modelview[gl_ViewIndex])));

    tangent = vec4(normalize(mv * in_tangent.xyz), in_tangent.w);
    if (any(isnan(tangent))) // avoid NaNs if no tangent data is provided
        tangent = vec4(0, 0, 0, 0);

    normal = normalize(mv * in_normal);

    gl_Position = mesh.modelviewproj[gl_ViewIndex] * vec4(in_position, 1.0);

    frag_pos = mesh.modelview[gl_ViewIndex] * vec4(in_position, 1.0);
    light_pos = scene.view[gl_ViewIndex] * scene.light_position;

    for (int i = 0; i < nb_clipping; i++)
    {
        gl_ClipDistance[i] = dot(mesh.clipping_plane[i], mesh.model * vec4(in_position, 1.0));
    }
}
