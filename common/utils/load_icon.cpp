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

#include "load_icon.h"

#include <archive.h>
#include <archive_entry.h>
#include <bit>
#include <cairo.h>
#include <fstream>
#include <librsvg/rsvg.h>
#include <png.h>
#include <span>
#include <unordered_map>

namespace
{
std::vector<std::byte> load_file(const std::filesystem::path & filename)
{
	std::ifstream file(filename, std::ios::binary | std::ios::ate);
	file.exceptions(std::ios_base::badbit | std::ios_base::failbit);

	size_t size = file.tellg();
	file.seekg(0);

	std::vector<std::byte> bytes(size);

	file.read(reinterpret_cast<char *>(bytes.data()), size);
	return bytes;
}

template <typename T>
        requires std::is_integral_v<T>
T read(std::span<const std::byte> & buffer)
{
	if (sizeof(T) > buffer.size())
		throw std::runtime_error{"File truncated"};

	T value;
	memcpy(&value, buffer.data(), sizeof(T));
	buffer = buffer.subspan(sizeof(T));

	static_assert(std::endian::native == std::endian::big or std::endian::native == std::endian::little);

	if constexpr (std::endian::native == std::endian::big)
		return std::byteswap(value);
	else
		return value;
}

template <typename T>
        requires std::is_integral_v<T>
std::span<const T> read(std::span<const std::byte> & buffer, size_t count)
{
	if (count * sizeof(T) > buffer.size())
		throw std::runtime_error{"File truncated"};

	std::span<const T> value{reinterpret_cast<const T *>(buffer.data()), count};
	buffer = buffer.subspan(count * sizeof(T));
	return value;
}

std::vector<wivrn::icon> try_load_svg(const std::vector<std::byte> & data, int size)
{
	// TODO check if aspect ratio needs to be handled, dpi

	struct deleter
	{
		void operator()(RsvgHandle * ptr)
		{
			g_object_unref(ptr);
		}

		void operator()(cairo_surface_t * ptr)
		{
			cairo_surface_destroy(ptr);
		}

		void operator()(cairo_t * ptr)
		{
			cairo_destroy(ptr);
		}
	};

	using RsvgHandle_ptr = std::unique_ptr<RsvgHandle, deleter>;
	using cairo_surface_t_ptr = std::unique_ptr<cairo_surface_t, deleter>;
	using cairo_t_ptr = std::unique_ptr<cairo_t, deleter>;

	GError * error{};

	RsvgHandle_ptr handle{rsvg_handle_new_from_data((guint8 *)data.data(), data.size(), &error)};
	if (!handle)
		throw std::runtime_error{std::string{"Could not open: "} + error->message};

	// Create a Cairo image surface and a rendering context for it
	cairo_surface_t_ptr surface{cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size)};
	cairo_t_ptr cr{cairo_create(surface.get())};

	// Set the dots-per-inch
	rsvg_handle_set_dpi(handle.get(), 96.0);

	// Render the handle scaled proportionally into that whole surface
	RsvgRectangle viewport = {
	        .x = 0.0,
	        .y = 0.0,
	        .width = (double)size,
	        .height = (double)size,
	};

	if (!rsvg_handle_render_document(handle.get(), cr.get(), &viewport, &error))
		throw std::runtime_error{std::string{"Could not render: "} + error->message};

	// Write a PNG file
	std::vector<std::byte> png;
	auto cb = [](void * closure, const unsigned char * data, unsigned int length) -> cairo_status_t {
		std::vector<std::byte> & png = *reinterpret_cast<std::vector<std::byte> *>(closure);

		size_t old_size = png.size();
		png.resize(old_size + length);
		memcpy(&png[old_size], data, length);

		return CAIRO_STATUS_SUCCESS;
	};

	if (auto status = cairo_surface_write_to_png_stream(surface.get(), cb, &png); status != CAIRO_STATUS_SUCCESS)
		throw std::runtime_error{std::string{"Could not write output: "} + cairo_status_to_string(status)};

	return {{size, size, 32, std::move(png)}};
}

std::vector<wivrn::icon> try_load_ico(const std::vector<std::byte> & ico)
{
	const uint32_t ICO_PNG_MAGIC = 0x474e5089;

	// See https://gitlab.gnome.org/GNOME/gimp/-/tree/master/plug-ins/file-ico?ref_type=heads
	auto rowstride = [](int width, int bpp) {
		int words_per_line = (width * bpp + 31) / 32;
		return words_per_line * 4;
	};

	static_assert(rowstride(1, 1) == 4);
	static_assert(rowstride(8, 1) == 4);
	static_assert(rowstride(16, 1) == 4);
	static_assert(rowstride(32, 1) == 4);
	static_assert(rowstride(33, 1) == 8);

	static_assert(rowstride(1, 4) == 4);
	static_assert(rowstride(8, 4) == 4);
	static_assert(rowstride(16, 4) == 8);
	static_assert(rowstride(32, 4) == 16);
	static_assert(rowstride(33, 4) == 20);

	static_assert(rowstride(1, 8) == 4);
	static_assert(rowstride(8, 8) == 8);
	static_assert(rowstride(16, 8) == 16);
	static_assert(rowstride(32, 8) == 32);
	static_assert(rowstride(33, 8) == 36);

	static_assert(rowstride(1, 24) == 4);
	static_assert(rowstride(8, 24) == 24);
	static_assert(rowstride(16, 24) == 48);
	static_assert(rowstride(32, 24) == 96);
	static_assert(rowstride(33, 24) == 100);

	static_assert(rowstride(1, 32) == 4);
	static_assert(rowstride(8, 32) == 32);
	static_assert(rowstride(16, 32) == 64);
	static_assert(rowstride(32, 32) == 128);
	static_assert(rowstride(33, 32) == 132);

	std::span header{ico};

	auto reserved = read<uint16_t>(header);
	auto resource_type = read<uint16_t>(header);
	auto icon_count = read<uint16_t>(header);

	if (reserved != 0 or resource_type != 1)
		throw std::runtime_error{"Cannot read icon"};

	std::vector<wivrn::icon> icons;

	for (int i = 0; i < icon_count; i++)
	{
		auto width = read<uint8_t>(header);
		auto height = read<uint8_t>(header);
		auto num_colors = read<uint8_t>(header);
		auto reserved = read<uint8_t>(header); // reserved
		auto planes = read<uint16_t>(header);
		auto bpp = read<uint16_t>(header);
		auto size = read<uint32_t>(header);
		auto offset = read<uint32_t>(header);

		if (offset + size > ico.size())
			throw std::runtime_error{"Cannot read icon"};

		auto data = std::span{ico}.subspan(offset, size);
		if (reserved != 0)
			throw std::runtime_error{"Cannot read icon"};
		if (planes != 0 and planes != 1)
			throw std::runtime_error{"Cannot read icon"};

		auto magic = read<uint32_t>(data);

		if (magic == ICO_PNG_MAGIC)
		{
			// PNG file
			std::vector<std::byte> png_data;

			// The PNG magic has already been consumed, put it back
			png_data.insert(png_data.end(), data.begin() - 4, data.end());

			// Check if the PNG is valid
			std::vector<uint8_t> pixels;
			png_image png_ref{
			        .version = PNG_IMAGE_VERSION,
			        .format = PNG_FORMAT_BGRA,
			};
			if (not png_image_begin_read_from_memory(&png_ref, png_data.data(), png_data.size()))
				continue;
			// throw std::runtime_error{"Cannot read icon: " + filename.native()};

			pixels.resize(PNG_IMAGE_SIZE(png_ref));

			if (!png_image_finish_read(&png_ref, nullptr, pixels.data(), 0, nullptr))
			{
				png_image_free(&png_ref);
				continue;
				// throw std::runtime_error{"Cannot read icon: " + filename.native()};
			}

			// TODO: get bit depth from PNG instead of trusting the ico header
			icons.emplace_back(png_ref.width, png_ref.height, bpp, std::move(png_data));
			png_image_free(&png_ref);
		}
		else if (magic == 40)
		{
			auto width = read<uint32_t>(data);       // Width of image in pixels
			auto height = read<uint32_t>(data);      // Height of image in pixels
			auto planes = read<uint16_t>(data);      // Must be 1
			auto bpp = read<uint16_t>(data);         // 1, 4, 8, 16, 24, 32
			auto compression = read<uint32_t>(data); // Must be 0 for icons
			auto image_size = read<uint32_t>(data);  // Size of image (without this header)
			auto x_res = read<uint32_t>(data);
			auto y_res = read<uint32_t>(data);
			auto used_colors = read<uint32_t>(data);
			auto important_colors = read<uint32_t>(data);

			if (planes != 1 or compression != 0)
				return {};

			if (bpp != 1 and
			    bpp != 4 and
			    bpp != 8 and
			    bpp != 16 and
			    bpp != 24 and
			    bpp != 32)
				return {};

			auto w = width;
			auto h = height / 2;

			std::span<const uint8_t> palette; // BGRX

			if (bpp <= 16)
			{
				// Load the palette
				if (used_colors == 0)
					used_colors = 1 << bpp;

				palette = read<uint8_t>(data, used_colors * 4);
			}

			size_t xor_stride = rowstride(w, bpp);
			size_t and_stride = rowstride(w, 1);
			size_t dst_stride = 4 * w;

			std::span xor_map = read<uint8_t>(data, xor_stride * h);
			std::span and_map = read<uint8_t>(data, and_stride * h);

			std::vector<uint8_t> dest_buffer;
			dest_buffer.resize(h * dst_stride);

			switch (bpp)
			{
				case 1:
					for (int y = 0; y < h; y++)
					{
						const uint8_t * src_xor_row = xor_map.data() + xor_stride * y;
						const uint8_t * src_and_row = and_map.data() + and_stride * y;
						uint8_t * dst_row = dest_buffer.data() + dst_stride * (h - 1 - y);

						for (int x = 0, i = 0; x < w; x++)
						{
							int colour = (src_xor_row[x / 8] >> (7 - (x % 8))) & 0x1;
							if (colour >= used_colors)
								return {};

							uint8_t b = palette[4 * colour + 0]; // Blue
							uint8_t g = palette[4 * colour + 1]; // Green
							uint8_t r = palette[4 * colour + 2]; // Red

							// Get alpha from AND mask (0: opaque => 0xff, 1: transparent => 0)
							uint8_t a = src_and_row[x / 8] & (1 << (7 - x % 8)) ? 0 : 0xff;

							*dst_row++ = b;
							*dst_row++ = g;
							*dst_row++ = r;
							*dst_row++ = a;
						}
					}
					break;

				case 4:
					for (int y = 0; y < h; y++)
					{
						const uint8_t * src_xor_row = xor_map.data() + xor_stride * y;
						const uint8_t * src_and_row = and_map.data() + and_stride * y;
						uint8_t * dst_row = dest_buffer.data() + dst_stride * (h - 1 - y);

						for (int x = 0, i = 0; x < w; x++)
						{
							int colour = (src_xor_row[x / 2] >> (4 - 4 * (x % 2))) & 0xf;
							if (colour >= used_colors)
								return {};

							uint8_t b = palette[4 * colour + 0]; // Blue
							uint8_t g = palette[4 * colour + 1]; // Green
							uint8_t r = palette[4 * colour + 2]; // Red

							// Get alpha from AND mask (0: opaque => 0xff, 1: transparent => 0)
							uint8_t a = src_and_row[x / 8] & (1 << (7 - x % 8)) ? 0 : 0xff;

							*dst_row++ = b;
							*dst_row++ = g;
							*dst_row++ = r;
							*dst_row++ = a;
						}
					}
					break;

				case 8:
					for (int y = 0; y < h; y++)
					{
						const uint8_t * src_xor_row = xor_map.data() + xor_stride * y;
						const uint8_t * src_and_row = and_map.data() + and_stride * y;
						uint8_t * dst_row = dest_buffer.data() + dst_stride * (h - 1 - y);

						for (int x = 0, i = 0; x < w; x++)
						{
							int colour = *src_xor_row++;
							if (colour >= used_colors)
								return {};

							uint8_t b = palette[4 * colour + 0]; // Blue
							uint8_t g = palette[4 * colour + 1]; // Green
							uint8_t r = palette[4 * colour + 2]; // Red

							// Get alpha from AND mask (0: opaque => 0xff, 1: transparent => 0)
							uint8_t a = src_and_row[x / 8] & (1 << (7 - x % 8)) ? 0 : 0xff;

							*dst_row++ = b;
							*dst_row++ = g;
							*dst_row++ = r;
							*dst_row++ = a;
						}
					}
					break;

				case 16:
					for (int y = 0; y < h; y++)
					{
						const uint8_t * src_xor_row = xor_map.data() + xor_stride * y;
						const uint8_t * src_and_row = and_map.data() + and_stride * y;
						uint8_t * dst_row = dest_buffer.data() + dst_stride * (h - 1 - y);

						for (int x = 0, i = 0; x < w; x++)
						{
							int colour = (src_xor_row[1] << 8) | src_xor_row[0];
							src_xor_row += 2;

							if (colour >= used_colors)
								return {};

							uint8_t b = palette[4 * colour + 0]; // Blue
							uint8_t g = palette[4 * colour + 1]; // Green
							uint8_t r = palette[4 * colour + 2]; // Red

							// Get alpha from AND mask (0: opaque => 0xff, 1: transparent => 0)
							uint8_t a = src_and_row[x / 8] & (1 << (7 - x % 8)) ? 0 : 0xff;

							*dst_row++ = b;
							*dst_row++ = g;
							*dst_row++ = r;
							*dst_row++ = a;
						}
					}
					break;

				case 24:
					for (int y = 0; y < h; y++)
					{
						const uint8_t * src_xor_row = xor_map.data() + xor_stride * y;
						const uint8_t * src_and_row = and_map.data() + and_stride * y;
						uint8_t * dst_row = dest_buffer.data() + dst_stride * (h - 1 - y);

						for (int x = 0, i = 0; x < w; x++)
						{
							uint8_t b = *src_xor_row++; // Blue
							uint8_t g = *src_xor_row++; // Green
							uint8_t r = *src_xor_row++; // Red

							// Get alpha from AND mask (0: opaque => 0xff, 1: transparent => 0)
							uint8_t a = src_and_row[x / 8] & (1 << (7 - x % 8)) ? 0 : 0xff;

							*dst_row++ = b;
							*dst_row++ = g;
							*dst_row++ = r;
							*dst_row++ = a;
						}
					}
					break;

				case 32:
					for (int y = 0; y < h; y++)
					{
						const uint8_t * src_xor_row = xor_map.data() + xor_stride * y;
						uint8_t * dst_row = dest_buffer.data() + dst_stride * (h - 1 - y);

						memcpy(dst_row, src_xor_row, dst_stride);
					}
					break;
			}

			png_image png{
			        .version = PNG_IMAGE_VERSION,
			        .width = w,
			        .height = h,
			        .format = PNG_FORMAT_BGRA,
			};

			std::vector<std::byte> png_data{65536};
			png_alloc_size_t png_size = png_data.size();
			png_image_write_to_memory(&png, png_data.data(), &png_size, false /* convert_to_8_bit*/, dest_buffer.data(), w * 4, nullptr);

			if (png_size > png_data.size())
			{
				png_data.resize(png_size);
				png_image_write_to_memory(&png, png_data.data(), &png_size, false /* convert_to_8_bit*/, dest_buffer.data(), w * 4, nullptr);
			}
			else
				png_data.resize(png_size);

			icons.emplace_back(w, h, bpp, std::move(png_data));
		}
	}

	return icons;
}

std::vector<wivrn::icon> try_load_zip(const std::vector<std::byte> & data)
{
	std::unique_ptr<archive, decltype(&archive_read_free)> zip{archive_read_new(), &archive_read_free};

	archive_read_support_filter_all(zip.get());
	archive_read_support_format_all(zip.get());

	if (auto rc = archive_read_open_memory(zip.get(), data.data(), data.size()); rc != ARCHIVE_OK)
		throw std::runtime_error{std::string("Cannot open ZIP: ") + archive_error_string(zip.get())};

	archive_entry * entry;
	std::vector<wivrn::icon> icons;

	while (archive_read_next_header(zip.get(), &entry) == ARCHIVE_OK)
	{
		std::vector<std::byte> buffer;
		buffer.resize(archive_entry_size(entry));

		if (auto status = archive_read_data(zip.get(), buffer.data(), buffer.size()); status < 0)
			continue;

		// Try reading the PNG
		std::vector<uint8_t> pixels;
		png_image png_ref{
		        .version = PNG_IMAGE_VERSION,
		        .format = PNG_FORMAT_BGRA,
		};

		if (not png_image_begin_read_from_memory(&png_ref, buffer.data(), buffer.size()))
		{
			png_image_free(&png_ref);
			continue;
		}

		pixels.resize(PNG_IMAGE_SIZE(png_ref));

		if (!png_image_finish_read(&png_ref, nullptr, pixels.data(), 0, nullptr))
		{
			png_image_free(&png_ref);
			continue;
		}

		// PNG read successfully
		icons.emplace_back(png_ref.width, png_ref.height, 32, std::move(buffer)); // TODO: check how to get bit depth

		png_image_free(&png_ref);
	}

	return icons;
}

std::vector<wivrn::icon> try_load_png(const std::vector<std::byte> & png_data)
{
	std::vector<wivrn::icon> icons;

	// Try reading the PNG
	std::vector<uint8_t> pixels;
	png_image png_ref{
	        .version = PNG_IMAGE_VERSION,
	        .format = PNG_FORMAT_BGRA,
	};

	if (not png_image_begin_read_from_memory(&png_ref, png_data.data(), png_data.size()))
	{
		png_image_free(&png_ref);
		throw std::runtime_error("");
	}

	pixels.resize(PNG_IMAGE_SIZE(png_ref));

	if (!png_image_finish_read(&png_ref, nullptr, pixels.data(), 0, nullptr))
	{
		png_image_free(&png_ref);
		throw std::runtime_error("");
	}

	// PNG read successfully
	icons.emplace_back(png_ref.width, png_ref.height, 32, std::move(png_data)); // TODO: check how to get bit depth

	png_image_free(&png_ref);

	return icons;
}

std::unordered_map<std::filesystem::path, std::vector<wivrn::icon>> icon_cache;

} // namespace

const std::vector<wivrn::icon> & wivrn::load_icon(const std::filesystem::path & filename)
{
	auto it = icon_cache.find(filename);
	if (it != icon_cache.end() and not it->second.empty())
		return it->second;

	auto data = load_file(filename);

	try
	{
		return icon_cache.emplace(filename, try_load_svg(data, 256)).first->second;
	}
	catch (...)
	{}

	try
	{
		return icon_cache.emplace(filename, try_load_ico(data)).first->second;
	}
	catch (...)
	{}

	try
	{
		return icon_cache.emplace(filename, try_load_zip(data)).first->second;
	}
	catch (...)
	{}

	try
	{
		return icon_cache.emplace(filename, try_load_png(data)).first->second;
	}
	catch (...)
	{}

	throw std::runtime_error{"Cannot load icon: " + filename.native()};
}
