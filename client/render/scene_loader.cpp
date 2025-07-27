/*
 * WiVRn VR streaming
 * Copyright (C) 2025  Guillaume Meunier <guillaume.meunier@centraliens.net>
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

#include "scene_loader.h"

#include "asset.h"
#include "gpu_buffer.h"
#include "image_loader.h"
#include "render/scene_components.h"
#include "utils/ranges.h"
#include <entt/entt.hpp>
#include <fastgltf/base64.hpp>
#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/util.hpp>
#include <glm/ext.hpp>
#include <ranges>
#include <spdlog/spdlog.h>

namespace
{
// BEGIN Utility functions
template <class T>
constexpr vk::Format vk_attribute_format = vk::Format::eUndefined;
template <>
constexpr vk::Format vk_attribute_format<float> = vk::Format::eR32Sfloat;
template <>
constexpr vk::Format vk_attribute_format<glm::vec2> = vk::Format::eR32G32Sfloat;
template <>
constexpr vk::Format vk_attribute_format<glm::vec3> = vk::Format::eR32G32B32Sfloat;
template <>
constexpr vk::Format vk_attribute_format<glm::vec4> = vk::Format::eR32G32B32A32Sfloat;
template <typename T, size_t N>
constexpr vk::Format vk_attribute_format<std::array<T, N>> = vk_attribute_format<T>;

template <class T>
constexpr size_t nb_attributes = 1;
template <typename T, size_t N>
constexpr size_t nb_attributes<std::array<T, N>> = N;

template <class T>
struct remove_array
{
	using type = T;
};
template <class T, size_t N>
struct remove_array<std::array<T, N>>
{
	using type = T;
};
template <class T>
using remove_array_t = remove_array<T>::type;

template <class T>
struct is_array : std::false_type
{
};

template <class T, size_t N>
struct is_array<std::array<T, N>> : std::true_type
{
};

template <class T>
constexpr bool is_array_v = is_array<T>::value;
// END

// BEGIN GLTF -> Vulkan conversion functions
std::pair<vk::Filter, vk::SamplerMipmapMode> convert(fastgltf::Filter filter)
{
	switch (filter)
	{
		case fastgltf::Filter::Nearest:
		case fastgltf::Filter::NearestMipMapNearest:
			return {vk::Filter::eNearest, vk::SamplerMipmapMode::eNearest};

		case fastgltf::Filter::Linear:
		case fastgltf::Filter::LinearMipMapNearest:
			return {vk::Filter::eLinear, vk::SamplerMipmapMode::eNearest};

		case fastgltf::Filter::NearestMipMapLinear:
			return {vk::Filter::eNearest, vk::SamplerMipmapMode::eLinear};

		case fastgltf::Filter::LinearMipMapLinear:
			return {vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear};
	}

	throw std::invalid_argument("filter");
}

vk::SamplerAddressMode convert(fastgltf::Wrap wrap)
{
	switch (wrap)
	{
		case fastgltf::Wrap::ClampToEdge:
			return vk::SamplerAddressMode::eClampToEdge;

		case fastgltf::Wrap::MirroredRepeat:
			return vk::SamplerAddressMode::eMirroredRepeat;

		case fastgltf::Wrap::Repeat:
			return vk::SamplerAddressMode::eRepeat;
	}

	throw std::invalid_argument("wrap");
}

renderer::sampler_info convert(fastgltf::Sampler & sampler)
{
	renderer::sampler_info info;

	info.mag_filter = convert(sampler.magFilter.value_or(fastgltf::Filter::Linear)).first;

	std::tie(info.min_filter, info.min_filter_mipmap) = convert(sampler.minFilter.value_or(fastgltf::Filter::LinearMipMapLinear));

	info.wrapS = convert(sampler.wrapS);
	info.wrapT = convert(sampler.wrapT);

	return info;
}

vk::PrimitiveTopology convert(fastgltf::PrimitiveType type)
{
	switch (type)
	{
		case fastgltf::PrimitiveType::Points:
			return vk::PrimitiveTopology::ePointList;

		case fastgltf::PrimitiveType::Lines:
			return vk::PrimitiveTopology::eLineList;

		case fastgltf::PrimitiveType::LineLoop:
			throw std::runtime_error("Unimplemented");

		case fastgltf::PrimitiveType::LineStrip:
			return vk::PrimitiveTopology::eLineStrip;

		case fastgltf::PrimitiveType::Triangles:
			return vk::PrimitiveTopology::eTriangleList;

		case fastgltf::PrimitiveType::TriangleStrip:
			return vk::PrimitiveTopology::eTriangleStrip;

		case fastgltf::PrimitiveType::TriangleFan:
			return vk::PrimitiveTopology::eTriangleFan;
	}

	throw std::invalid_argument("type");
}

glm::vec4 convert(const std::array<float, 4> & v)
{
	return {v[0], v[1], v[2], v[3]};
}

glm::vec3 convert(const std::array<float, 3> & v)
{
	return {v[0], v[1], v[2]};
}
// END

// BEGIN Vertex attributes loading
template <typename T>
        requires is_array_v<T>
void copy_vertex_attributes(
        const fastgltf::Asset & asset,
        const fastgltf::Primitive & primitive,
        std::string attribute_name,
        std::vector<renderer::vertex> & vertices,
        T renderer::vertex::*attribute)
{
	using U = T::value_type;
	constexpr size_t N = std::tuple_size_v<T>;

	for (size_t i = 0; i < N; i++)
	{
		auto it = primitive.findAttribute(attribute_name + std::to_string(i));

		if (it == primitive.attributes.end())
			continue;

		const fastgltf::Accessor & accessor = asset.accessors.at(it->second);

		if (vertices.size() < accessor.count)
			vertices.resize(accessor.count, {});

		fastgltf::iterateAccessorWithIndex<U>(asset, accessor, [&](U value, std::size_t idx) {
			(vertices[idx].*attribute)[i] = value;
		});
	}
}

template <typename T>
        requires(!is_array_v<T>)
void copy_vertex_attributes(
        const fastgltf::Asset & asset,
        const fastgltf::Primitive & primitive,
        std::string attribute_name,
        std::vector<renderer::vertex> & vertices,
        T renderer::vertex::*attribute)
{
	auto it = primitive.findAttribute(attribute_name);

	if (it == primitive.attributes.end())
		return;

	const fastgltf::Accessor & accessor = asset.accessors.at(it->second);

	if (vertices.size() < accessor.count)
		vertices.resize(accessor.count, {});

	fastgltf::iterateAccessorWithIndex<T>(asset, accessor, [&](T value, std::size_t idx) {
		vertices[idx].*attribute = value;
	});
}
// END

// BEGIN Image loading
constexpr bool starts_with(std::span<const std::byte> data, std::span<const uint8_t> prefix)
{
	return data.size() >= prefix.size() && !memcmp(data.data(), prefix.data(), prefix.size());
}

fastgltf::MimeType guess_mime_type(std::span<const std::byte> image_data)
{
	const uint8_t jpeg_magic[] = {0xFF, 0xD8, 0xFF};
	const uint8_t png_magic[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
	const uint8_t ktx1_magic[] = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31, 0x31, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
	const uint8_t ktx2_magic[] = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};

	if (starts_with(image_data, png_magic))
		return fastgltf::MimeType::PNG;
	else if (starts_with(image_data, jpeg_magic))
		return fastgltf::MimeType::JPEG;
	else if (starts_with(image_data, ktx1_magic))
		return fastgltf::MimeType::KTX2;
	else if (starts_with(image_data, ktx2_magic))
		return fastgltf::MimeType::KTX2;
	else
		return fastgltf::MimeType::None;
}

std::shared_ptr<vk::raii::ImageView> do_load_image(
        vk::raii::PhysicalDevice & physical_device,
        vk::raii::Device & device,
        thread_safe<vk::raii::Queue> & queue,
        vk::raii::CommandPool & cb_pool,
        std::span<const std::byte> image_data,
        bool srgb)
{
	switch (guess_mime_type(image_data))
	{
		case fastgltf::MimeType::JPEG:
		case fastgltf::MimeType::PNG:
		case fastgltf::MimeType::KTX2: {
			try
			{
				image_loader loader(physical_device, device, queue, cb_pool);
				loader.load(image_data, srgb);

				spdlog::debug("Loaded image {}x{}, format {}, {} mipmaps", loader.extent.width, loader.extent.height, vk::to_string(loader.format), loader.num_mipmaps);
				return loader.image_view;
			}
			catch (std::exception & e)
			{
				spdlog::info("Cannot load image: {}", e.what());
				return {};
			}
		}

		default:
			spdlog::error("Unsupported image MIME type");
			return {};
	}
}
// END

fastgltf::Asset load_gltf_asset(fastgltf::GltfDataBuffer & buffer, const std::filesystem::path & directory)
{
	fastgltf::Parser parser(fastgltf::Extensions::KHR_texture_basisu);

	auto gltf_options =
	        fastgltf::Options::DontRequireValidAssetMember |
	        fastgltf::Options::AllowDouble |
	        fastgltf::Options::LoadGLBBuffers |
	        fastgltf::Options::DecomposeNodeMatrices;

	auto expected_asset = parser.loadGltf(&buffer, directory, gltf_options);

	if (auto error = expected_asset.error(); error != fastgltf::Error::None)
		throw std::runtime_error(std::string(fastgltf::getErrorMessage(error)));

	return std::move(expected_asset.get());
}

class loader_context
{
	std::filesystem::path base_directory;
	fastgltf::Asset & gltf;
	vk::raii::PhysicalDevice physical_device;
	vk::raii::Device & device;
	thread_safe<vk::raii::Queue> & queue;
	vk::raii::CommandPool & cb_pool;

	std::vector<asset> loaded_assets;
	asset & load_from_asset(const std::filesystem::path & path)
	{
		return loaded_assets.emplace_back(path);
	}

public:
	loader_context(std::filesystem::path base_directory,
	               fastgltf::Asset & gltf,
	               vk::raii::PhysicalDevice physical_device,
	               vk::raii::Device & device,
	               thread_safe<vk::raii::Queue> & queue,
	               vk::raii::CommandPool & cb_pool) :
	        base_directory(base_directory),
	        gltf(gltf),
	        physical_device(physical_device),
	        device(device),
	        queue(queue),
	        cb_pool(cb_pool)
	{
	}

	fastgltf::span<const std::byte> load(const fastgltf::URI & uri)
	{
		auto path = base_directory.empty() ? std::filesystem::path(uri.path()) : base_directory / std::filesystem::path(uri.path());
		std::span<const std::byte> bytes;

		bytes = load_from_asset(path);

		return fastgltf::span<const std::byte>(bytes.data(), bytes.size());
	}

	fastgltf::span<const std::byte> load(const fastgltf::sources::URI & uri)
	{
		return load(uri.uri);
	}

	template <typename T, std::size_t Extent>
	fastgltf::span<T, fastgltf::dynamic_extent> subspan(fastgltf::span<T, Extent> span, size_t offset, size_t count = fastgltf::dynamic_extent)
	{
		assert(offset < span.size());
		assert(count == fastgltf::dynamic_extent || count <= span.size() - offset);

		if (count == fastgltf::dynamic_extent)
			count = span.size() - offset;

		return fastgltf::span<T>{span.data() + offset, count};
	}

	fastgltf::sources::ByteView visit_source(fastgltf::DataSource & source)
	{
		using return_type = fastgltf::sources::ByteView;

		return std::visit(fastgltf::visitor{
		                          [&](std::monostate) -> return_type {
			                          throw std::runtime_error("Invalid source");
		                          },
		                          [&](fastgltf::sources::Fallback) -> return_type {
			                          throw std::runtime_error("Invalid source");
		                          },
		                          [&](const fastgltf::sources::BufferView & buffer_view) -> return_type {
			                          fastgltf::BufferView & view = gltf.bufferViews.at(buffer_view.bufferViewIndex);

			                          fastgltf::Buffer & buffer = gltf.buffers.at(view.bufferIndex);

			                          auto buffer2 = visit_source(buffer.data);

			                          return {subspan(buffer2.bytes, view.byteOffset, view.byteLength), buffer_view.mimeType};
		                          },
		                          [&](const fastgltf::sources::URI & uri) -> return_type {
			                          if (!uri.uri.isLocalPath())
				                          throw std::runtime_error("Non local paths are not supported"); // TODO ?

			                          fastgltf::span<const std::byte> data = load(uri);

			                          // Don't use the MIME type from fastgltf, it's not initialized for URIs
			                          return {subspan(data, uri.fileByteOffset), fastgltf::MimeType::None};
		                          },
		                          [&](const fastgltf::sources::Vector & vector) -> return_type {
			                          fastgltf::span<const std::byte> data{reinterpret_cast<const std::byte *>(vector.bytes.data()), vector.bytes.size()};
			                          return {data, vector.mimeType};
		                          },
		                          [&](const fastgltf::sources::Array & array) -> return_type {
			                          fastgltf::span<const std::byte> data{reinterpret_cast<const std::byte *>(array.bytes.data()), array.bytes.size()};
			                          return {data, array.mimeType};
		                          },
		                          [&](const fastgltf::sources::CustomBuffer & custom_buffer) -> return_type {
			                          throw std::runtime_error("Unimplemented source CustomBuffer");
			                          // TODO ?
		                          },
		                          [&](fastgltf::sources::ByteView & byte_view) -> return_type {
			                          return {byte_view.bytes, byte_view.mimeType};
		                          }},
		                  source);
	}

	std::unordered_map<int, std::shared_ptr<vk::raii::ImageView>> images;
	std::shared_ptr<vk::raii::ImageView> load_image(int index, bool srgb)
	{
		auto it = images.find(index);
		if (it != images.end())
			return it->second;

		try
		{
			auto [image_data, mime_type] = visit_source(gltf.images[index].data);
			auto image = do_load_image(physical_device, device, queue, cb_pool, image_data, srgb);

			images.emplace(index, image);
			return image;
		}
		catch (...)
		{
			return nullptr;
		}
	}

	std::vector<std::shared_ptr<renderer::texture>> load_all_textures()
	{
		// Determine which texture is sRGB
		std::vector<uint8_t> srgb_array;
		srgb_array.resize(gltf.textures.size(), false);
		for (const fastgltf::Material & gltf_material: gltf.materials)
		{
			if (gltf_material.pbrData.baseColorTexture)
				srgb_array.at(gltf_material.pbrData.baseColorTexture->textureIndex) = true;

			if (gltf_material.emissiveTexture)
				srgb_array.at(gltf_material.emissiveTexture->textureIndex) = true;
		}

		std::vector<std::shared_ptr<renderer::texture>> textures;
		textures.reserve(gltf.textures.size());
		for (auto && [srgb, gltf_texture]: std::views::zip(srgb_array, gltf.textures))
		{
			auto & texture_ref = *textures.emplace_back(std::make_shared<renderer::texture>());

			if (gltf_texture.samplerIndex)
			{
				fastgltf::Sampler & sampler = gltf.samplers.at(*gltf_texture.samplerIndex);
				texture_ref.sampler = convert(sampler);
			}

			if (gltf_texture.basisuImageIndex)
			{
				texture_ref.image_view = load_image(*gltf_texture.basisuImageIndex, srgb);
				if (texture_ref.image_view)
					continue;
			}

			// if (gltf_texture.ddsImageIndex)
			// {
			// 	// TODO
			// }

			// if (gltf_texture.webpImageIndex)
			// {
			// 	// TODO
			// }

			if (gltf_texture.imageIndex)
			{
				texture_ref.image_view = load_image(*gltf_texture.imageIndex, srgb);
				if (texture_ref.image_view)
					continue;
			}

			throw std::runtime_error("Unsupported image type");
		}

		return textures;
	}

	void load_all_buffers()
	{
		for (fastgltf::Buffer & buffer: gltf.buffers)
		{
			if (std::holds_alternative<fastgltf::sources::URI>(buffer.data))
			{
				fastgltf::sources::URI uri = std::get<fastgltf::sources::URI>(buffer.data);

				// Don't use the MIME type from fastgltf, it's not initialized for URIs
				buffer.data = fastgltf::sources::ByteView(load(uri), fastgltf::MimeType::None);
			}
		}
	}

	std::vector<std::shared_ptr<renderer::material>> load_all_materials(std::vector<std::shared_ptr<renderer::texture>> & textures, gpu_buffer & staging_buffer, const renderer::material & default_material)
	{
		std::vector<std::shared_ptr<renderer::material>> materials;
		materials.reserve(gltf.materials.size());
		for (const fastgltf::Material & gltf_material: gltf.materials)
		{
			// Copy the default material, without references to its buffer or descriptor set
			auto & material_ref = *materials.emplace_back(std::make_shared<renderer::material>(default_material));
			material_ref.name = gltf_material.name;
			spdlog::info("Loading material \"{}\"", material_ref.name);
			material_ref.buffer.reset();
			material_ref.ds.reset();

			material_ref.double_sided = gltf_material.doubleSided;
			material_ref.blend_enable = gltf_material.alphaMode == fastgltf::AlphaMode::Blend; // TODO handle alpha cut off mode

			renderer::material::gpu_data & material_data = material_ref.staging;

			material_data.base_color_factor = convert(gltf_material.pbrData.baseColorFactor);
			material_data.base_emissive_factor = glm::vec4(convert(gltf_material.emissiveFactor), 0);
			material_data.metallic_factor = gltf_material.pbrData.metallicFactor;
			material_data.roughness_factor = gltf_material.pbrData.roughnessFactor;

			if (gltf_material.pbrData.baseColorTexture)
			{
				material_ref.base_color_texture = textures.at(gltf_material.pbrData.baseColorTexture->textureIndex);
				material_data.base_color_texcoord = gltf_material.pbrData.baseColorTexture->texCoordIndex;
			}

			if (gltf_material.pbrData.metallicRoughnessTexture)
			{
				material_ref.metallic_roughness_texture = textures.at(gltf_material.pbrData.metallicRoughnessTexture->textureIndex);
				material_data.metallic_roughness_texcoord = gltf_material.pbrData.metallicRoughnessTexture->texCoordIndex;
			}

			if (gltf_material.occlusionTexture)
			{
				material_ref.occlusion_texture = textures.at(gltf_material.occlusionTexture->textureIndex);
				material_data.occlusion_texcoord = gltf_material.occlusionTexture->texCoordIndex;
				material_data.occlusion_strength = gltf_material.occlusionTexture->strength;
			}

			if (gltf_material.emissiveTexture)
			{
				material_ref.emissive_texture = textures.at(gltf_material.emissiveTexture->textureIndex);
				material_data.emissive_texcoord = gltf_material.emissiveTexture->texCoordIndex;
			}

			if (gltf_material.normalTexture)
			{
				material_ref.normal_texture = textures.at(gltf_material.normalTexture->textureIndex);
				material_data.normal_texcoord = gltf_material.normalTexture->texCoordIndex;
				material_data.normal_scale = gltf_material.normalTexture->scale;
			}

			material_ref.offset = staging_buffer.add_uniform(material_data);
		}

		return materials;
	}

	std::vector<std::shared_ptr<renderer::mesh>> load_all_meshes(std::vector<std::shared_ptr<renderer::material>> & materials, gpu_buffer & staging_buffer)
	{
		std::vector<std::shared_ptr<renderer::mesh>> meshes;
		meshes.reserve(gltf.meshes.size());
		for (const fastgltf::Mesh & gltf_mesh: gltf.meshes)
		{
			std::shared_ptr<renderer::mesh> & mesh_ref = meshes.emplace_back();
			mesh_ref = std::make_shared<renderer::mesh>();

			mesh_ref->primitives.reserve(gltf_mesh.primitives.size());

			for (const fastgltf::Primitive & gltf_primitive: gltf_mesh.primitives)
			{
				auto & primitive_ref = mesh_ref->primitives.emplace_back();

				if (gltf_primitive.indicesAccessor)
				{
					fastgltf::Accessor & indices_accessor = gltf.accessors.at(*gltf_primitive.indicesAccessor);

					primitive_ref.indexed = true;
					primitive_ref.index_offset = staging_buffer.add_indices(indices_accessor);
					primitive_ref.index_count = indices_accessor.count;

					switch (indices_accessor.componentType)
					{
						case fastgltf::ComponentType::Byte:
						case fastgltf::ComponentType::UnsignedByte:
							primitive_ref.index_type = vk::IndexType::eUint8EXT;
							break;

						case fastgltf::ComponentType::Short:
						case fastgltf::ComponentType::UnsignedShort:
							primitive_ref.index_type = vk::IndexType::eUint16;
							break;

						case fastgltf::ComponentType::Int:
						case fastgltf::ComponentType::UnsignedInt:
							primitive_ref.index_type = vk::IndexType::eUint32;
							break;

						default:
							throw std::runtime_error("Invalid index type");
							break;
					}
				}
				else
				{
					primitive_ref.indexed = false;
				}

				std::vector<renderer::vertex> vertices;

				copy_vertex_attributes(gltf, gltf_primitive, "POSITION", vertices, &renderer::vertex::position);
				copy_vertex_attributes(gltf, gltf_primitive, "NORMAL", vertices, &renderer::vertex::normal);
				copy_vertex_attributes(gltf, gltf_primitive, "TANGENT", vertices, &renderer::vertex::tangent);
				copy_vertex_attributes(gltf, gltf_primitive, "TEXCOORD_", vertices, &renderer::vertex::texcoord);
				copy_vertex_attributes(gltf, gltf_primitive, "COLOR", vertices, &renderer::vertex::color);
				copy_vertex_attributes(gltf, gltf_primitive, "JOINTS_", vertices, &renderer::vertex::joints);
				copy_vertex_attributes(gltf, gltf_primitive, "WEIGHTS_", vertices, &renderer::vertex::weights);

				primitive_ref.vertex_offset = staging_buffer.add_vertices(vertices);
				primitive_ref.vertex_count = vertices.size();

				primitive_ref.cull_mode = vk::CullModeFlagBits::eBack;       // TBC
				primitive_ref.front_face = vk::FrontFace::eCounterClockwise; // TBC
				primitive_ref.topology = convert(gltf_primitive.type);

				if (gltf_primitive.materialIndex)
					primitive_ref.material_ = materials.at(*gltf_primitive.materialIndex);
			}
		}

		return meshes;
	}

	entt::registry load_all_nodes(const std::vector<std::shared_ptr<renderer::mesh>> & meshes)
	{
		entt::registry registry;

		std::vector<entt::entity> entities{gltf.nodes.size()};
		registry.create(entities.begin(), entities.end());

		// Create all node components first
		registry.insert<components::node>(entities.begin(), entities.end());

		for (const auto & [entity, gltf_node]: std::ranges::zip_view(entities, gltf.nodes))
		{
			components::node & node = registry.get<components::node>(entity);
			node.name = gltf_node.name;

			if (gltf_node.meshIndex)
				node.mesh = meshes[*gltf_node.meshIndex];

			if (gltf_node.skinIndex)
			{
				auto & skin = gltf.skins.at(*gltf_node.skinIndex);

				node.joints.resize(skin.joints.size());

				for (auto [i, joint_index]: utils::enumerate(skin.joints))
					node.joints.at(i).first = entities[joint_index];

				if (skin.inverseBindMatrices)
				{
					const fastgltf::Accessor & accessor = gltf.accessors.at(*skin.inverseBindMatrices);

					fastgltf::iterateAccessorWithIndex<glm::mat4>(gltf, accessor, [&](const glm::mat4 & value, std::size_t idx) {
						node.joints.at(idx).second = value;
					});
				}
			}

			for (size_t child: gltf_node.children)
			{
				registry.get<components::node>(entities[child]).parent = entity;
			}

			auto TRS = std::get<fastgltf::TRS>(gltf_node.transform);

			node.position = glm::make_vec3(TRS.translation.data());
			node.orientation = glm::make_quat(TRS.rotation.data());
			node.scale = glm::make_vec3(TRS.scale.data());
			node.visible = true;
			std::ranges::fill(node.clipping_planes, glm::vec4{0, 0, 0, 1});
		}

		return registry;
	}
};

} // namespace

renderer::vertex::description renderer::vertex::describe()
{
	renderer::vertex::description desc;

	desc.binding = vk::VertexInputBindingDescription{
	        .binding = 0,
	        .stride = sizeof(vertex),
	        .inputRate = vk::VertexInputRate::eVertex,
	};

	vk::VertexInputAttributeDescription attribute{
	        .location = 0,
	        .binding = 0,
	};

#define VERTEX_ATTR(member)                                                                                         \
	attribute.format = vk_attribute_format<decltype(vertex::member)>;                                           \
	for (size_t i = 0; i < nb_attributes<decltype(vertex::member)>; i++)                                        \
	{                                                                                                           \
		attribute.offset = offsetof(vertex, member) + i * sizeof(remove_array_t<decltype(vertex::member)>); \
		desc.attributes.push_back(attribute);                                                               \
		attribute.location++;                                                                               \
		if (is_array_v<decltype(vertex::member)>)                                                           \
		{                                                                                                   \
			desc.attribute_names.push_back(#member "_" + std::to_string(i));                            \
		}                                                                                                   \
		else                                                                                                \
		{                                                                                                   \
			desc.attribute_names.push_back(#member);                                                    \
		}                                                                                                   \
	}

	VERTEX_ATTR(position);
	VERTEX_ATTR(normal);
	VERTEX_ATTR(tangent);
	VERTEX_ATTR(texcoord);
	VERTEX_ATTR(color);
	VERTEX_ATTR(joints);
	VERTEX_ATTR(weights);

	static_assert(vertex{}.joints.size() == vertex{}.weights.size());

	return desc;
}

entt::registry scene_loader::operator()(const std::filesystem::path & gltf_path)
{
	vk::PhysicalDeviceProperties physical_device_properties = physical_device.getProperties();
	vk::raii::CommandPool cb_pool{device, vk::CommandPoolCreateInfo{
	                                              .flags = vk::CommandPoolCreateFlagBits::eTransient | vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
	                                              .queueFamilyIndex = queue_family_index,
	                                      }};

	spdlog::debug("Loading {}", gltf_path.native());

	asset asset_file(gltf_path);
	fastgltf::GltfDataBuffer data_buffer;
	data_buffer.copyBytes(reinterpret_cast<const uint8_t *>(asset_file.data()), asset_file.size());

	fastgltf::Asset asset = load_gltf_asset(data_buffer, gltf_path.parent_path());
	loader_context ctx(gltf_path.parent_path(), asset, physical_device, device, queue, cb_pool);

#ifndef NDEBUG
	if (auto error = fastgltf::validate(asset); error != fastgltf::Error::None)
		throw std::runtime_error(std::string(fastgltf::getErrorMessage(error)));
#endif

	// Load all buffers from URIs
	ctx.load_all_buffers();

	gpu_buffer staging_buffer(physical_device_properties, asset);

	// Load all textures
	auto textures = ctx.load_all_textures();

	// Load all materials
	auto materials = ctx.load_all_materials(textures, staging_buffer, *default_material);

	// Load all meshes
	auto meshes = ctx.load_all_meshes(materials, staging_buffer);

	// Load all nodes
	entt::registry loaded_scene = ctx.load_all_nodes(meshes);

	// Copy the staging buffer to the GPU
	spdlog::debug("Uploading scene data ({} bytes) to GPU memory", staging_buffer.size());
	auto buffer = std::make_shared<buffer_allocation>(staging_buffer.copy_to_gpu());

	for (auto & i: materials)
		i->buffer = buffer;

	for (auto & i: meshes)
		i->buffer = buffer;

	return loaded_scene;
}

template <typename T>
void copy_components(entt::registry & scene, const entt::registry & prefab, const std::unordered_map<entt::entity, entt::entity> & entity_map)
{
	for (const auto & [entity, component]: prefab.view<T>().each())
	{
		scene.emplace<T>(entity_map.at(entity), component);
	}
}

void scene_loader::add_prefab(entt::registry & scene, const entt::registry & prefab, entt::entity root)
{
	assert(scene.valid(root) or root == entt::null);

	auto prefab_entities = prefab.view<entt::entity>();

	std::vector<entt::entity> scene_entities{prefab_entities.size()};
	scene.create(scene_entities.begin(), scene_entities.end());

	std::unordered_map<entt::entity, entt::entity> entity_map; // key: prefab entity, value: scene entity

	entity_map.emplace(entt::null, root);
	for (auto [prefab_entity, scene_entity]: std::ranges::zip_view(prefab_entities, scene_entities))
		entity_map.emplace(prefab_entity, scene_entity);

	copy_components<components::node>(scene, prefab, entity_map);

	// update links
	for (auto [prefab_entity, scene_entity]: entity_map)
	{
		if (prefab_entity == entt::null)
			continue;

		auto & node = scene.get<components::node>(scene_entity);

		node.parent = entity_map.at(node.parent);

		for (auto & joint: node.joints)
		{
			joint.first = entity_map.at(joint.first);
		}
	}
}
