// kate: hl glsl

const int nb_views = 2;
const int nb_clipping = 4;
const int nb_texcoords = 2;

const bool use_pbr = false;
const bool use_normal_maps = false;

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
	 * if (x <= 0.0031308)
	 *     return 12.92 * x;
	 * else
	 *     return 1.055*pow(x, 1.0 / 2.4) - 0.055;
	 *
	 * Its derivative is:
	 * if (x <= 0.0031308)
	 *     return 12.92;
	 * else
	 *     return (1.055 / 2.4) * pow(x, -1.4 / 2.4);
	 *
	 */

// 	vec3 tmp1 = vec3(1.0 / 12.92);
// 	vec3 tmp2 = (2.4 / 1.055) * pow(color.rgb, vec3(1.4 / 2.4));

	vec3 tmp1 = vec3(0.0773993808049);
	vec3 tmp2 = 2.27488151659 * pow(color.rgb, vec3(0.583333333333));

	// Alpha is not converted to sRGB
	return vec4(mix(tmp1, tmp2, vec3(greaterThan(color.rgb, vec3(0.0031308, 0.0031308, 0.0031308)))), 1.0);
}

vec4 dither(ivec2 frag_coord, vec4 colour)
{
	ivec2 tmp = frag_coord % 4;
	float dither_thd = 1.0f - dither_pattern[tmp.x][tmp.y];

	return colour + one_over_d_linear_to_srgb(colour) * vec4(dither_thd, dither_thd, dither_thd, 0) / 255.0f;
}

vec2 compute_texcoord(texture_info info, vec2 in_texcoord)
{
	mat3 translation = mat3(
		1, 0, 0,
		0, 1, 0,
		info.offset.x, info.offset.y, 1);
	mat3 rotation = mat3(
		cos(info.rotation), sin(info.rotation), 0,
		-sin(info.rotation), cos(info.rotation), 0,
		0, 0, 1);
	mat3 scale = mat3(
		info.scale.x, 0, 0,
		0, info.scale.y, 0,
		0, 0, 1);

	mat3 matrix = translation * rotation * scale;

	return (matrix * vec3(in_texcoord, 1)).xy;
}

#ifdef FRAG_SHADER
#define VS_TO_FS in
#else
#define VS_TO_FS out
#endif

// Vertex-to-fragment
layout(location = 0) VS_TO_FS vec3 normal;
layout(location = 1) VS_TO_FS vec4 tangent;
layout(location = 2) VS_TO_FS vec4 frag_pos;
layout(location = 3) VS_TO_FS vec4 light_pos;
layout(location = 4) VS_TO_FS vec2 texcoord_base_color;
layout(location = 5) VS_TO_FS vec2 texcoord_metallic_roughness;
layout(location = 6) VS_TO_FS vec2 texcoord_occlusion;
layout(location = 7) VS_TO_FS vec2 texcoord_emissive;
layout(location = 8) VS_TO_FS vec2 texcoord_normal;
