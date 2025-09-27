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

#include "gpu_buffer.h"
#include "image_loader.h"
#include "render/scene_components.h"
#include "render/vertex_layout.h"
#include "utils/files.h"
#include "utils/json_string.h"
#include "utils/mapped_file.h"
#include "utils/ranges.h"
#include "vk/shader.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <entt/entt.hpp>
#include <fastgltf/base64.hpp>
#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/math.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/util.hpp>
#include <filesystem>
#include <fstream>
#include <glm/ext.hpp>
#include <glm/fwd.hpp>
#include <limits>
#include <ranges>
#include <simdjson/dom.h>
#include <spdlog/spdlog.h>
#include <string>
#include <vulkan/vulkan_format_traits.hpp>

namespace fastgltf
{
// glm_element_traits.hpp is missing all the glm::vec<1, T> and glm:::vec<n, int32_t> specializations
// clang-format off
template <> struct ElementTraits<glm::u8vec1>  : ElementTraitsBase<glm::u8vec1,  AccessorType::Scalar, std::uint8_t>  {};
template <> struct ElementTraits<glm::i8vec1>  : ElementTraitsBase<glm::i8vec1,  AccessorType::Scalar, std::int8_t>   {};
template <> struct ElementTraits<glm::u16vec1> : ElementTraitsBase<glm::u16vec1, AccessorType::Scalar, std::uint16_t> {};
template <> struct ElementTraits<glm::i16vec1> : ElementTraitsBase<glm::i16vec1, AccessorType::Scalar, std::int16_t>  {};
template <> struct ElementTraits<glm::u32vec1> : ElementTraitsBase<glm::u32vec1, AccessorType::Scalar, std::uint32_t> {};
template <> struct ElementTraits<glm::i32vec1> : ElementTraitsBase<glm::i32vec1, AccessorType::Scalar, std::int32_t>  {};
template <> struct ElementTraits<glm::i32vec2> : ElementTraitsBase<glm::i32vec2, AccessorType::Vec2,   std::int32_t>  {};
template <> struct ElementTraits<glm::i32vec3> : ElementTraitsBase<glm::i32vec3, AccessorType::Vec3,   std::int32_t>  {};
template <> struct ElementTraits<glm::i32vec4> : ElementTraitsBase<glm::i32vec4, AccessorType::Vec4,   std::int32_t>  {};
template <> struct ElementTraits<glm::fvec1>   : ElementTraitsBase<glm::fvec1,   AccessorType::Scalar, float>         {};
template <> struct ElementTraits<glm::dvec1>   : ElementTraitsBase<glm::dvec1,   AccessorType::Scalar, float>         {};
// clang-format on
} // namespace fastgltf

namespace
{
// BEGIN GLTF -> Vulkan conversion functions
std::pair<vk::Filter, vk::SamplerMipmapMode> convert(fastgltf::Filter filter)
{
	switch (filter)
	{
		case fastgltf::Filter::Nearest:
			return {vk::Filter::eNearest, {}};

		case fastgltf::Filter::Linear:
			return {vk::Filter::eLinear, {}};

		case fastgltf::Filter::NearestMipMapNearest:
			return {vk::Filter::eNearest, vk::SamplerMipmapMode::eNearest};

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

glm::vec4 convert(const fastgltf::math::nvec4 & v)
{
	return {v[0], v[1], v[2], v[3]};
}

glm::vec3 convert(const fastgltf::math::nvec3 & v)
{
	return {v[0], v[1], v[2]};
}

components::animation_track_base::interpolation_t convert(fastgltf::AnimationInterpolation interpolation)
{
	switch (interpolation)
	{
		case fastgltf::AnimationInterpolation::Linear:
			return components::animation_track_base::interpolation_t::linear;
		case fastgltf::AnimationInterpolation::Step:
			return components::animation_track_base::interpolation_t::step;
		case fastgltf::AnimationInterpolation::CubicSpline:
			return components::animation_track_base::interpolation_t::cubic_spline;
	}

	throw std::invalid_argument("interpolation");
}
// END

// BEGIN Vertex attributes loading
template <typename T>
std::vector<glm::dvec4> read_vertex_attribute_aux2(const fastgltf::Asset & asset, const fastgltf::Accessor & accessor)
{
	std::vector<glm::dvec4> output;
	output.resize(accessor.count);

	size_t index = 0;
	switch (fastgltf::getNumComponents(accessor.type))
	{
		case 1:
			fastgltf::iterateAccessorWithIndex<T>(asset, accessor, [&](const T & value, size_t index) {
				output[index] = glm::dvec4(value[0], 0, 0, 1);
			});
			break;
		case 2:
			fastgltf::iterateAccessorWithIndex<T>(asset, accessor, [&](const T & value, size_t index) {
				output[index] = glm::dvec4(value[0], value[1], 0, 1);
			});
			break;
		case 3:
			fastgltf::iterateAccessorWithIndex<T>(asset, accessor, [&](const T & value, size_t index) {
				output[index] = glm::dvec4(value[0], value[1], value[2], 1);
			});
			break;
		case 4:
			fastgltf::iterateAccessorWithIndex<T>(asset, accessor, [&](const T & value, size_t index) {
				output[index] = glm::dvec4(value[0], value[1], value[2], value[3]);
			});
			break;
		default:
			abort();
	}

	return output;
}

template <typename T>
std::vector<glm::dvec4> read_vertex_attribute_aux(const fastgltf::Asset & asset, const fastgltf::Accessor & accessor)
{
	switch (fastgltf::getNumComponents(accessor.type))
	{
			// clang-format off
		case 1: return read_vertex_attribute_aux2<glm::vec<1, T>>(asset, accessor);
		case 2: return read_vertex_attribute_aux2<glm::vec<2, T>>(asset, accessor);
		case 3: return read_vertex_attribute_aux2<glm::vec<3, T>>(asset, accessor);
		case 4: return read_vertex_attribute_aux2<glm::vec<4, T>>(asset, accessor);
			// clang-format on
	}
	return {};
}

std::vector<glm::dvec4> read_vertex_attribute(const fastgltf::Asset & asset, const fastgltf::Accessor & accessor)
{
	switch (accessor.componentType)
	{
			// clang-format off
		case fastgltf::ComponentType::Byte:          return read_vertex_attribute_aux<int8_t>(asset, accessor);
		case fastgltf::ComponentType::UnsignedByte:  return read_vertex_attribute_aux<uint8_t>(asset, accessor);
		case fastgltf::ComponentType::Short:         return read_vertex_attribute_aux<int16_t>(asset, accessor);
		case fastgltf::ComponentType::UnsignedShort: return read_vertex_attribute_aux<uint16_t>(asset, accessor);
		case fastgltf::ComponentType::Int:           return read_vertex_attribute_aux<int32_t>(asset, accessor);
		case fastgltf::ComponentType::UnsignedInt:   return read_vertex_attribute_aux<uint32_t>(asset, accessor);
		case fastgltf::ComponentType::Float:         return read_vertex_attribute_aux<float>(asset, accessor);
		// clang-format on
		case fastgltf::ComponentType::Double: // TODO
			abort();
			break;

		case fastgltf::ComponentType::Invalid:
			abort();
			break;
	}

	return {};
}

template <typename T>
void write_vertex_attribute_aux(std::byte * output, size_t stride, int components, const std::vector<glm::dvec4> & input)
{
	switch (components)
	{
		case 1:
			for (size_t i = 0, n = input.size(); i < n; i++, output += stride)
				*reinterpret_cast<glm::vec<1, T> *>(output) = glm::vec<1, T>(input[i].x);
			break;
		case 2:
			for (size_t i = 0, n = input.size(); i < n; i++, output += stride)
				*reinterpret_cast<glm::vec<2, T> *>(output) = glm::vec<2, T>(input[i].x, input[i].y);
			break;
		case 3:
			for (size_t i = 0, n = input.size(); i < n; i++, output += stride)
				*reinterpret_cast<glm::vec<3, T> *>(output) = glm::vec<3, T>(input[i].x, input[i].y, input[i].z);
			break;
		case 4:
			for (size_t i = 0, n = input.size(); i < n; i++, output += stride)
				*reinterpret_cast<glm::vec<3, T> *>(output) = glm::vec<4, T>(input[i].x, input[i].y, input[i].z, input[i].w);
			break;
		default:
			abort();
	}
}

void write_vertex_attribute(std::vector<std::byte> & output, size_t offset, size_t stride, vk::Format format, std::vector<glm::dvec4> input)
{
	assert(output.size() >= input.size() * stride);

	switch (format)
	{
		case vk::Format::eR32Sfloat:
		case vk::Format::eR32G32Sfloat:
		case vk::Format::eR32G32B32Sfloat:
		case vk::Format::eR32G32B32A32Sfloat:
			write_vertex_attribute_aux<float>(output.data() + offset, stride, vk::componentCount(format), input);
			return;

		default:
			return;
	}
}

void copy_vertex_attributes(
        const fastgltf::Asset & asset,
        const fastgltf::Accessor & accessor,
        std::vector<std::byte> & buffer,
        const vk::VertexInputBindingDescription & binding,
        const vk::VertexInputAttributeDescription & attribute)
{
	assert(buffer.size() >= accessor.count * binding.stride);

	write_vertex_attribute(buffer, attribute.offset, binding.stride, attribute.format, read_vertex_attribute(asset, accessor));
}

std::pair<size_t, std::vector<std::vector<std::byte>>> create_vertex_buffers(
        const renderer::vertex_layout & layout,
        const fastgltf::Asset & asset,
        const fastgltf::Primitive & primitive)
{
	std::pair<size_t, std::vector<std::vector<std::byte>>> size_buffers;
	auto & [vertex_count, buffers] = size_buffers;

	// Count the vertices
	vertex_count = 0;
	for (const auto & name: layout.attribute_names)
	{
		if (auto attribute = primitive.findAttribute(name); attribute != primitive.attributes.cend())
			vertex_count = std::max(vertex_count, asset.accessors.at(attribute->accessorIndex).count);
	}

	// Allocate buffers
	int max_binding = std::ranges::max(layout.bindings, {}, &vk::VertexInputBindingDescription::binding).binding;
	buffers.resize(max_binding + 1);
	for (const vk::VertexInputBindingDescription & binding: layout.bindings)
	{
		switch (binding.inputRate)
		{
			case vk::VertexInputRate::eVertex:
				buffers[binding.binding].resize(binding.stride * vertex_count);
				break;
			case vk::VertexInputRate::eInstance:
				buffers[binding.binding].resize(binding.stride);
				break;
		}
	}

	// Copy attributes
	for (auto && [name, attribute]: std::views::zip(layout.attribute_names, layout.attributes))
	{
		auto binding_iter = std::ranges::find(layout.bindings, attribute.binding, &vk::VertexInputBindingDescription::binding);
		assert(binding_iter != layout.bindings.end());

		const vk::VertexInputBindingDescription & binding = *binding_iter;
		std::vector<std::byte> & buffer = buffers.at(binding.binding);

		const fastgltf::Attribute * gltf_attribute = primitive.findAttribute(name);
		if (gltf_attribute == primitive.attributes.cend())
		{
			// Also check if it's an array of size 1
			gltf_attribute = primitive.findAttribute(name + "_0");

			if (gltf_attribute == primitive.attributes.cend())
				continue;
		}

		const fastgltf::Accessor & accessor = asset.accessors.at(gltf_attribute->accessorIndex);

		copy_vertex_attributes(asset, accessor, buffer, binding, attribute);
	}

	return size_buffers;
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

loaded_image do_load_image(
        image_loader & loader,
        std::span<const std::byte> image_data,
        bool srgb,
        const std::pmr::string & name,
        const std::filesystem::path & output_path)
{
	switch (guess_mime_type(image_data))
	{
		case fastgltf::MimeType::JPEG:
		case fastgltf::MimeType::PNG:
		case fastgltf::MimeType::KTX2: {
			auto image = loader.load(image_data, srgb, name.c_str(), false /*premultiply*/, output_path);

			spdlog::debug("{}: {}x{}, format {}, {} mipmaps, {} bytes", name, image.extent.width, image.extent.height, vk::to_string(image.format), image.num_mipmaps, image.image.size());
			return image;
		}

		default:
			throw std::runtime_error{"Unsupported image MIME type"};
	}
}
// END

// BEGIN Error category
struct : std::error_category
{
	const char * name() const noexcept override
	{
		return "fastgltf";
	}

	std::string message(int condition) const override
	{
		return (std::string)fastgltf::getErrorMessage(static_cast<fastgltf::Error>(condition));
	}
} fastgltf_error_category;
// END

fastgltf::Asset load_gltf_asset(fastgltf::GltfDataBuffer & buffer, const std::filesystem::path & directory)
{
	fastgltf::Parser parser(fastgltf::Extensions::KHR_texture_basisu);

	auto gltf_options =
	        fastgltf::Options::DontRequireValidAssetMember |
	        fastgltf::Options::AllowDouble |
	        fastgltf::Options::DecomposeNodeMatrices;

	auto expected_asset = parser.loadGltf(buffer, directory, gltf_options);

	if (auto error = expected_asset.error(); error != fastgltf::Error::None)
		throw std::system_error((int)error, fastgltf_error_category);

	return std::move(expected_asset.get());
}

class loader_context
{
	std::filesystem::path base_directory;
	std::string name;
	fastgltf::Asset & gltf;
	vk::raii::PhysicalDevice physical_device;
	vk::raii::Device & device;
	thread_safe<vk::raii::Queue> & queue;
	image_loader & loader;

	std::vector<utils::mapped_file> loaded_assets;
	utils::mapped_file & load_from_asset(const std::filesystem::path & path)
	{
		try
		{
			return loaded_assets.emplace_back(path);
		}
		catch (std::exception & e)
		{
			spdlog::error("{}: error loading {}: {}", name, path.native(), e.what());
			throw;
		}
	}

public:
	loader_context(std::filesystem::path base_directory,
	               std::string name,
	               fastgltf::Asset & gltf,
	               vk::raii::PhysicalDevice physical_device,
	               vk::raii::Device & device,
	               thread_safe<vk::raii::Queue> & queue,
	               image_loader & loader) :
	        base_directory(std::move(base_directory)),
	        name(std::move(name)),
	        gltf(gltf),
	        physical_device(physical_device),
	        device(device),
	        queue(queue),
	        loader(loader)
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
	std::shared_ptr<vk::raii::ImageView> load_image(int index, const std::filesystem::path & texture_cache, bool srgb)
	{
		auto it = images.find(index);
		if (it != images.end())
			return it->second;

		if (index > gltf.images.size())
			throw std::runtime_error(std::format("Image index {} out of range", index));

		std::pmr::string image_name;
		if (gltf.images[index].name == "")
			image_name = std::format("{}(image {})", name, index);
		else
			image_name = std::format("{}({})", name, gltf.images[index].name);

		std::filesystem::path cached_texture;

		if (texture_cache != "")
		{
			cached_texture = texture_cache / (std::to_string(index) + ".ktx");

			if (std::filesystem::exists(cached_texture))
			{
				try
				{
					utils::mapped_file image_data{cached_texture};
					auto image_ptr = std::make_shared<loaded_image>(do_load_image(loader, image_data, srgb, image_name, ""));
					return std::shared_ptr<vk::raii::ImageView>(image_ptr, &image_ptr->image_view);
				}
				catch (std::exception & e)
				{
					spdlog::warn("Cannot load cached image {}: {}, deleting it", cached_texture.native(), e.what());
					std::error_code ec;
					std::filesystem::remove(cached_texture);
				}
			}
		}

		auto [image_data, mime_type] = visit_source(gltf.images[index].data);
		auto image_ptr = std::make_shared<loaded_image>(do_load_image(loader, image_data, srgb, image_name, cached_texture));

		return std::shared_ptr<vk::raii::ImageView>(image_ptr, &image_ptr->image_view);
	}

	std::vector<std::shared_ptr<renderer::texture>> load_all_textures(const std::filesystem::path & texture_cache, std::function<void(float)> progress_cb)
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

		int loaded_textures = 0;
		int total_textures = gltf.textures.size();
		for (auto && [srgb, gltf_texture]: std::views::zip(srgb_array, gltf.textures))
		{
			auto & texture_ref = *textures.emplace_back(std::make_shared<renderer::texture>());

			if (gltf_texture.samplerIndex)
				texture_ref.sampler = convert(gltf.samplers.at(*gltf_texture.samplerIndex));

			std::vector<std::string> errors;

			if (gltf_texture.basisuImageIndex)
			{
				try
				{
					texture_ref.image_view = load_image(*gltf_texture.basisuImageIndex, texture_cache, srgb);
					loaded_textures++;
					if (progress_cb)
						progress_cb((float)loaded_textures / total_textures);
					continue;
				}
				catch (std::exception & e)
				{
					errors.push_back(std::format("Cannot load image {}: {}", *gltf_texture.basisuImageIndex, e.what()));
				}
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
				try
				{
					texture_ref.image_view = load_image(*gltf_texture.imageIndex, texture_cache, srgb);
					loaded_textures++;
					if (progress_cb)
						progress_cb((float)loaded_textures / total_textures);
					continue;
				}
				catch (std::exception & e)
				{
					errors.push_back(std::format("Cannot load image {}: {}", *gltf_texture.imageIndex, e.what()));
				}
			}

			spdlog::error("{}: cannot load texture {}:", name, name);
			for (auto & error: errors)
				spdlog::error("    {}", error);

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
			auto & material = *materials.emplace_back(std::make_shared<renderer::material>(default_material));
			material.name = gltf_material.name;
			material.buffer.reset();

			material.double_sided = gltf_material.doubleSided;

			switch (gltf_material.alphaMode)
			{
				case fastgltf::AlphaMode::Opaque:
					material.fragment_shader_name = "lit.frag";
					material.blend_enable = false;
					break;

				case fastgltf::AlphaMode::Mask:
					material.fragment_shader_name = "lit_mask.frag";
					material.blend_enable = false;
					break;

				case fastgltf::AlphaMode::Blend:
					material.fragment_shader_name = "lit.frag";
					material.blend_enable = true;
					break;
			}

			renderer::material::gpu_data & material_data = material.staging;

			material_data.base_color_factor = convert(gltf_material.pbrData.baseColorFactor);
			material_data.base_emissive_factor = glm::vec4(convert(gltf_material.emissiveFactor), 0);
			material_data.metallic_factor = gltf_material.pbrData.metallicFactor;
			material_data.roughness_factor = gltf_material.pbrData.roughnessFactor;
			material_data.alpha_cutoff = gltf_material.alphaCutoff;

			if (gltf_material.pbrData.baseColorTexture)
			{
				material.base_color_texture = textures.at(gltf_material.pbrData.baseColorTexture->textureIndex);
				material_data.base_color_texcoord = gltf_material.pbrData.baseColorTexture->texCoordIndex;
			}

			if (gltf_material.pbrData.metallicRoughnessTexture)
			{
				material.metallic_roughness_texture = textures.at(gltf_material.pbrData.metallicRoughnessTexture->textureIndex);
				material_data.metallic_roughness_texcoord = gltf_material.pbrData.metallicRoughnessTexture->texCoordIndex;
			}

			if (gltf_material.occlusionTexture)
			{
				material.occlusion_texture = textures.at(gltf_material.occlusionTexture->textureIndex);
				material_data.occlusion_texcoord = gltf_material.occlusionTexture->texCoordIndex;
				material_data.occlusion_strength = gltf_material.occlusionTexture->strength;
			}

			if (gltf_material.emissiveTexture)
			{
				material.emissive_texture = textures.at(gltf_material.emissiveTexture->textureIndex);
				material_data.emissive_texcoord = gltf_material.emissiveTexture->texCoordIndex;
			}

			if (gltf_material.normalTexture)
			{
				material.normal_texture = textures.at(gltf_material.normalTexture->textureIndex);
				material_data.normal_texcoord = gltf_material.normalTexture->texCoordIndex;
				material_data.normal_scale = gltf_material.normalTexture->scale;
			}

			material.offset = staging_buffer.add_uniform(material_data);
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
					primitive_ref.indexed = false;

				renderer::vertex_layout & layout = primitive_ref.layout;

				if (gltf_primitive.findAttribute("JOINTS_0") == gltf_primitive.attributes.cend())
					primitive_ref.vertex_shader = "lit.vert";
				else
					primitive_ref.vertex_shader = "lit_skinned.vert";

				// For attributes types, see https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#meshes-overview (§ 3.7.2.1)
				auto shader = shader_loader{device}(primitive_ref.vertex_shader);
				for (const auto & input: shader->inputs)
				{
					std::string semantic = input.name;
					std::transform(semantic.begin(), semantic.end(), semantic.begin(), [](auto c) { return std::toupper(c); });

					if (semantic.starts_with("IN_"))
						semantic = semantic.substr(3);

					int binding;

					if (semantic == "POSITION" or
					    semantic == "JOINTS" or
					    semantic == "WEIGHTS")
						binding = 0;
					else
						binding = 1;

					layout.add_vertex_attribute(semantic, input.format, binding, input.location, input.array_size);
				}

				auto [vertex_count, buffers] = create_vertex_buffers(layout, gltf, gltf_primitive);
				primitive_ref.vertex_count = vertex_count;

				for (auto & buffer: buffers)
					primitive_ref.vertex_offset.push_back(staging_buffer.add_vertices(buffer));

				// Compute the OBB
				glm::vec3 obb_min{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
				glm::vec3 obb_max = -obb_min;
				fastgltf::iterateAccessor<glm::vec3>(gltf, gltf.accessors.at(gltf_primitive.findAttribute("POSITION")->accessorIndex), [&](glm::vec3 position) {
					obb_min.x = std::min(obb_min.x, position.x);
					obb_min.y = std::min(obb_min.y, position.y);
					obb_min.z = std::min(obb_min.z, position.z);
					obb_max.x = std::max(obb_max.x, position.x);
					obb_max.y = std::max(obb_max.y, position.y);
					obb_max.z = std::max(obb_max.z, position.z);
				});
				primitive_ref.obb_min = obb_min;
				primitive_ref.obb_max = obb_max;

				if (gltf_primitive.materialIndex)
					primitive_ref.material_ = materials.at(*gltf_primitive.materialIndex);

				if (primitive_ref.material_ and primitive_ref.material_->double_sided)
				{
					primitive_ref.cull_mode = vk::CullModeFlagBits::eFrontAndBack;
					primitive_ref.front_face = vk::FrontFace::eCounterClockwise;
				}
				else
				{
					primitive_ref.cull_mode = vk::CullModeFlagBits::eBack;
					primitive_ref.front_face = vk::FrontFace::eCounterClockwise;
				}

				primitive_ref.topology = convert(gltf_primitive.type);
			}
		}

		return meshes;
	}

	std::vector<entt::entity> load_all_nodes(entt::registry & registry, const std::vector<std::shared_ptr<renderer::mesh>> & meshes)
	{
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

		return entities;
	}

	void load_all_animations(entt::registry & registry, const std::vector<entt::entity> & node_entities)
	{
		std::vector<entt::entity> entities{gltf.animations.size()};
		registry.create(entities.begin(), entities.end());

		// Create all animation components first
		registry.insert<components::animation>(entities.begin(), entities.end());

		for (const auto & [entity, gltf_animation]: std::ranges::zip_view(entities, gltf.animations))
		{
			auto & animation = registry.get<components::animation>(entity);
			animation.name = gltf_animation.name;
			animation.tracks.reserve(gltf_animation.channels.size());

			for (const auto & channel: gltf_animation.channels)
			{
				if (not channel.nodeIndex.has_value() or *channel.nodeIndex > node_entities.size())
					// Ignore this channel
					continue;

				const auto & sampler = gltf_animation.samplers.at(channel.samplerIndex);
				const auto & time = gltf.accessors.at(sampler.inputAccessor);
				const auto & value = gltf.accessors.at(sampler.outputAccessor);
				const auto interpolation = convert(sampler.interpolation);

				switch (interpolation)
				{
					case components::animation_track_base::interpolation_t::step:
					case components::animation_track_base::interpolation_t::linear:
						if (time.count != value.count)
							// There must be the same number of elements
							continue;
						break;

					case components::animation_track_base::interpolation_t::cubic_spline:
						// TODO: does not work for morph targets
						if (3 * time.count != value.count or time.count < 2)
							continue;
						break;
				}

				size_t count = time.count;
				switch (channel.path)
				{
					// TODO: templatize the switch
					case fastgltf::AnimationPath::Translation: {
						components::animation_track_position track;
						track.target = node_entities.at(*channel.nodeIndex);
						track.interpolation = interpolation;
						track.timestamp.resize(time.count);
						track.value.resize(value.count);

						fastgltf::iterateAccessorWithIndex<float>(gltf, time, [&](float t, size_t index) {
							track.timestamp[index] = t;
							animation.duration = std::max(animation.duration, t);
						});

						fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, value, [&](glm::vec3 value, size_t index) {
							track.value[index] = value;
						});

						animation.tracks.emplace_back(std::move(track));
						break;
					}
					case fastgltf::AnimationPath::Rotation: {
						components::animation_track_orientation track;
						track.target = node_entities.at(*channel.nodeIndex);
						track.interpolation = interpolation;
						track.timestamp.resize(time.count);
						track.value.resize(value.count);

						fastgltf::iterateAccessorWithIndex<float>(gltf, time, [&](float t, size_t index) {
							track.timestamp[index] = t;
							animation.duration = std::max(animation.duration, t);
						});

						fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf, value, [&](glm::vec4 value, size_t index) {
							track.value[index] = glm::quat::wxyz(value.w, value.x, value.y, value.z);
						});

						animation.tracks.emplace_back(std::move(track));
						break;
					}
					case fastgltf::AnimationPath::Scale: {
						components::animation_track_scale track;
						track.target = node_entities.at(*channel.nodeIndex);
						track.interpolation = interpolation;
						track.timestamp.resize(time.count);
						track.value.resize(value.count);

						fastgltf::iterateAccessorWithIndex<float>(gltf, time, [&](float t, size_t index) {
							track.timestamp[index] = t;
							animation.duration = std::max(animation.duration, t);
						});

						fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, value, [&](glm::vec3 value, size_t index) {
							track.value[index] = value;
						});

						animation.tracks.emplace_back(std::move(track));
						break;
					}
					case fastgltf::AnimationPath::Weights:
						// TODO morph weights
						break;
				}
			}
		}
	}
};

} // namespace

scene_loader::scene_loader(vk::raii::Device & device, vk::raii::PhysicalDevice physical_device, thread_safe<vk::raii::Queue> & queue, uint32_t queue_family_index, std::shared_ptr<renderer::material> default_material, std::filesystem::path texture_cache_) :
        device(device),
        physical_device(physical_device),
        queue(queue),
        queue_family_index(queue_family_index),
        default_material(default_material),
        texture_cache(std::move(texture_cache_))
{
	clear_texture_cache();
}

std::shared_ptr<entt::registry> scene_loader::operator()(
        std::span<const std::byte> data,
        const std::string & name,
        const std::filesystem::path & parent_path,
        const std::filesystem::path & gltf_texture_cache,
        std::function<void(float)> progress_cb)
{
	vk::PhysicalDeviceProperties physical_device_properties = physical_device.getProperties();

	auto data_buffer = fastgltf::GltfDataBuffer::FromBytes(data.data(), data.size());
	if (auto error = data_buffer.error(); error != fastgltf::Error::None)
		throw std::system_error((int)error, fastgltf_error_category);

	fastgltf::Asset asset = load_gltf_asset(data_buffer.get(), parent_path);

	image_loader loader{device, physical_device, queue, queue_family_index};
	loader_context ctx{parent_path, name, asset, physical_device, device, queue, loader};

#ifndef NDEBUG
	if (auto error = fastgltf::validate(asset); error != fastgltf::Error::None)
		throw std::system_error((int)error, fastgltf_error_category);
#endif

	// Load all buffers from URIs
	ctx.load_all_buffers();

	gpu_buffer staging_buffer(physical_device_properties, asset);

	// Load all textures
	auto textures = ctx.load_all_textures(gltf_texture_cache, progress_cb);

	// Load all materials
	auto materials = ctx.load_all_materials(textures, staging_buffer, *default_material);

	// Load all meshes
	auto meshes = ctx.load_all_meshes(materials, staging_buffer);

	// Load all nodes
	auto loaded_scene = std::make_shared<entt::registry>();
	auto node_entities = ctx.load_all_nodes(*loaded_scene, meshes);

	// Load animations
	ctx.load_all_animations(*loaded_scene, node_entities);

	// Copy the staging buffer to the GPU
	spdlog::debug("Uploading scene data ({} bytes) to GPU memory", staging_buffer.size());
	auto buffer = std::make_shared<buffer_allocation>(staging_buffer.copy_to_gpu(device));

	for (auto & i: materials)
		i->buffer = buffer;

	for (auto & i: meshes)
		i->buffer = buffer;

	return loaded_scene;
}

struct cache_entry
{
	std::string filename;
	int64_t timestamp = 0;
	int64_t size = 0;
};

static cache_entry read_cache_entry(const std::filesystem::path & path)
{
	cache_entry entry;
	std::string json = utils::read_whole_file<std::string>(path / "index.json");
	simdjson::dom::parser parser;
	simdjson::dom::element root = parser.parse(json);

	if (auto val = root["filename"]; val.is_string())
		entry.filename = val.get_string().value();

	if (auto val = root["size"]; val.is_int64())
		entry.size = val.get_int64();

	if (auto val = root["timestamp"]; val.is_int64())
		entry.timestamp = val.get_int64();

	return entry;
}

static void write_cache_entry(const std::filesystem::path & path, const cache_entry & entry)
{
	std::ofstream json{path / "index.json"};
	json << "{\"filename\":" << json_string(entry.filename);
	json << ",\"size\":" << entry.size;

	// TODO get a timestamp for assets
	if (not entry.filename.starts_with("assets://"))
	{
		auto mtime = std::chrono::file_clock::to_sys(std::filesystem::last_write_time(entry.filename));
		auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(mtime.time_since_epoch()).count();
		auto size = entry.size;

		json << ",\"timestamp\":" << timestamp;
	}
	json << "}";
}

// Returns false if the cache is out of date and must be deleted
static bool check_cache_entry(const std::filesystem::path & path)
{
	try
	{
		cache_entry entry = read_cache_entry(path);

		utils::mapped_file file{entry.filename};
		int64_t real_size = file.size();

		if (real_size != entry.size)
			return false;

		// TODO get a timestamp for assets
		if (not entry.filename.starts_with("assets://"))
		{
			auto mtime = std::chrono::file_clock::to_sys(std::filesystem::last_write_time(entry.filename));
			auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(mtime.time_since_epoch()).count();

			if (timestamp != entry.timestamp)
				return false;
		}
	}
	catch (...)
	{
		return false;
	}

	return true;
}

std::shared_ptr<entt::registry> scene_loader::operator()(
        const std::filesystem::path & gltf_path,
        std::function<void(float)> progress_cb)
{
	utils::mapped_file gltf_file(gltf_path);
	std::filesystem::path gltf_texture_cache;

	if (texture_cache != "")
	{
		// FNV1a hash
		static const uint64_t fnv_prime = 0x100000001b3;
		static const uint64_t fnv_offset_basis = 0xcbf29ce484222325;
		uint64_t hash = fnv_offset_basis;
		for (char c: gltf_path.native())
			hash = (hash ^ c) * fnv_prime;

		gltf_texture_cache = texture_cache / fmt::format("{:016x}", hash);

		bool create_cache = false;
		try
		{
			if (std::filesystem::exists(gltf_texture_cache))
			{
				if (not std::filesystem::is_directory(gltf_texture_cache))
				{
					std::filesystem::remove(gltf_texture_cache);
					std::filesystem::create_directories(gltf_texture_cache);
					spdlog::debug("\"{}\" is not a directory: creating cache", gltf_texture_cache.native());
					create_cache = true;
				}
				else if (not check_cache_entry(gltf_texture_cache))
				{
					std::error_code ec;
					std::filesystem::remove_all(gltf_texture_cache, ec);
					std::filesystem::create_directories(gltf_texture_cache);
					spdlog::debug("\"{}\" is out of date: recreating cache", gltf_texture_cache.native());
					create_cache = true;
				}
			}
			else
			{
				std::filesystem::create_directories(gltf_texture_cache);
				spdlog::debug("\"{}\" does not exist: creating cache", gltf_texture_cache.native());
				create_cache = true;
			}
		}
		catch (std::exception & e)
		{
			spdlog::warn("Cannot create texture cache directory {}: {}", gltf_texture_cache.native(), e.what());
			gltf_texture_cache = "";
			create_cache = false;
		}

		if (create_cache)
		{
			write_cache_entry(
			        gltf_texture_cache,
			        cache_entry{
			                .filename = gltf_path.native(),
			                .size = (int64_t)gltf_file.size(),
			        });
		}
	}

	return (*this)(gltf_file, gltf_path.filename(), gltf_path.parent_path(), gltf_texture_cache, progress_cb);
}

void scene_loader::clear_texture_cache()
{
	try
	{
		for (const std::filesystem::directory_entry & entry: std::filesystem::directory_iterator{texture_cache})
		{
			if (not entry.is_directory())
				continue;

			if (not check_cache_entry(entry))
			{
				spdlog::info("{}: removing cache directory", entry.path().native());
				std::error_code ec;
				std::filesystem::remove_all(entry.path(), ec);
			}
		}
	}
	catch (...)
	{
		// Ignore errors if the texture cache directory does not exist
	}
}
