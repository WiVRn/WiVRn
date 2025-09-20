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

#extension GL_GOOGLE_include_directive : require

#include "common.glsl.inc"
#include "pbr.glsl.inc"

// Fragment output
layout(location = 0) out vec4 out_color;
layout(early_fragment_tests) in;

void main()
{
    vec4 albedo = material.base_color_factor * texture(base_color_texture, texcoord[0]);
    vec3 emissive_color = (material.base_emissive_factor * texture(emissive_texture, texcoord[0])).rgb;

    vec3 normal_unit;

    if (use_normal_maps)
    {
        vec3 sampled_normal = normalize(vec3(material.normal_scale, material.normal_scale, 1) * (texture(normal_map_texture, texcoord[0]).xyz * 2 - 1));

        // glTF spec §3.7.2.1
        vec3 bitangent = cross(normal, tangent.xyz) * tangent.w;
        mat3 TBN = mat3(tangent.xyz, bitangent, normal); // View space to tangent space?

        // Normal in view space
        normal_unit = TBN * sampled_normal;
    }
    else
        normal_unit = normalize(normal);

    vec3 light_dir = normalize(light_pos.xyz - frag_pos.xyz * light_pos.w);
    vec3 view_dir = normalize(-frag_pos.xyz);

    if (use_pbr)
    {
        float ao = 1 + material.occlusion_strength * (texture(occlusion_texture, texcoord[0]).r - 1);
        vec4 mr = texture(metallic_roughness_texture, texcoord[0]);

        vec3 half_vec = normalize(light_dir + view_dir);

        float metallic = material.metallic_factor * mr.b;
        float roughness = material.roughness_factor * mr.g;

        vec3 light_colour = scene.light_color.rgb;
        float attenuation = 1;
        vec3 radiance =
            per_light_radiance(albedo.rgb, metallic, roughness, light_colour, light_dir, normal_unit, view_dir, half_vec, attenuation) +
                vec3(scene.ambient_color) * albedo.rgb * ao;

        vec3 tmp = radiance + emissive_color;

        out_color = vec4(tmp / (tmp + 1), albedo.a);
    }
    else
    {
        out_color = vec4(emissive_color + albedo.rgb * (max(0, dot(normal_unit, light_dir)) * scene.light_color.rgb + scene.ambient_color.rgb), albedo.a);
    }
}
