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

#pragma once

#include "boost/pfr/core.hpp"
#include "boost/pfr/tuple_size.hpp"
#include <array>
#include <boost/pfr.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <variant>
#include <vector>

#include "wivrn_serialization_types.h"

namespace xrt::drivers::wivrn
{

namespace details
{
struct hash_context
{
	// FNV1a hash
	static const uint64_t fnv_prime = 0x100000001b3;
	static const uint64_t fnv_offset_basis = 0xcbf29ce484222325;
	uint64_t hash = fnv_offset_basis;

	constexpr uint64_t feed(std::string_view s)
	{
		for (const char c: s)
			hash = (hash ^ c) * fnv_prime;

		return hash;
	}

	constexpr uint64_t feed_unsigned(uint64_t n)
	{
		if (n >= 10)
			feed(n / 10);

		char digit[] = "0";
		digit[0] += n % 10;
		feed(digit);

		return hash;
	}

	constexpr uint64_t feed(int64_t n)
	{
		if (n < 0)
		{
			feed("-");
			feed_unsigned(static_cast<uint64_t>(-n));
		}
		else
		{
			feed_unsigned(static_cast<uint64_t>(n));
		}

		return hash;
	}
};
} // namespace details

template <typename T, typename Enable = void>
struct serialization_traits;

class deserialization_error : public std::runtime_error
{
public:
	deserialization_error() :
	        std::runtime_error("Deserialization error") {}
};

class serialization_packet
{
	std::vector<uint8_t> buffer;
	// Either the size to read from the buffer, or an actual span
	// Last element is always an integer
	std::vector<std::variant<size_t, std::span<uint8_t>>> spans = {size_t(0)};

public:
	serialization_packet() = default;

	void write(const void * data, size_t size)
	{
		size_t index = buffer.size();
		buffer.resize(index + size);
		memcpy(&buffer[index], data, size);
		std::get<size_t>(spans.back()) += size;
	}

	void write(std::span<uint8_t> span)
	{
		spans.push_back(span);
		spans.push_back(size_t(0));
	}

	template <typename T>
	void serialize(const T & value)
	{
		serialization_traits<T>::serialize(value, *this);
	}

	operator std::vector<std::span<uint8_t>>()
	{
		struct visitor
		{
			std::vector<uint8_t>::iterator it;
			std::vector<std::span<uint8_t>> res;
			void operator()(size_t s)
			{
				if (s > 0)
					res.emplace_back(it, it + s);
				it += s;
			}
			void operator()(const std::span<uint8_t> & span)
			{
				res.push_back(span);
			}
		};
		visitor v{.it = buffer.begin()};
		for (const auto & span_variant: spans)
		{
			std::visit(v, span_variant);
		}
		return v.res;
	}
};

class deserialization_packet
{
	std::vector<uint8_t> buffer;
	size_t read_index;

public:
	deserialization_packet() :
	        read_index(0) {}
	explicit deserialization_packet(std::vector<uint8_t> buffer, size_t skip = 0) :
	        buffer(std::move(buffer)), read_index(skip)
	{}

	void read(void * data, size_t size)
	{
		check_remaining_size(size);

		memcpy(data, &buffer[read_index], size);
		read_index += size;
	}

	std::span<uint8_t> read_span(size_t size)
	{
		check_remaining_size(size);
		std::span<uint8_t> res(&buffer[read_index], size);
		read_index += size;
		return res;
	}

	size_t remaining() const
	{
		return buffer.size() - read_index;
	}

	bool empty() const
	{
		return buffer.size() <= read_index;
	}

	void check_remaining_size(size_t min_size) const
	{
		if (min_size > remaining())
			throw deserialization_error();
	}

	template <typename T>
	T deserialize()
	{
		return serialization_traits<T>::deserialize(*this);
	}

	template <typename T>
	void deserialize(T & v)
	{
		v = deserialize<T>();
	}

	std::pair<size_t, std::vector<uint8_t>> steal_buffer()
	{
		return {read_index, std::move(buffer)};
	}
};

template <typename T>
struct serialization_traits<T, std::enable_if_t<std::is_arithmetic_v<T>>>
{
	static constexpr void type_hash(details::hash_context & h)
	{
		if constexpr (std::is_floating_point_v<T>)
			h.feed("float");
		else if constexpr (std::is_integral_v<T>)
		{
			if constexpr (std::is_unsigned_v<T>)
				h.feed("uint");
			else
				h.feed("int");
		}

		h.feed(sizeof(T) * 8);
	}

	static void serialize(T value, serialization_packet & packet)
	{
		packet.write(&value, sizeof(value));
	}

	static T deserialize(deserialization_packet & packet)
	{
		T value;
		packet.read((char *)&value, sizeof(value));
		return value;
	}
};

template <typename T>
struct serialization_traits<T, std::enable_if_t<std::is_enum_v<T>>>
{
	static constexpr void type_hash(details::hash_context & h)
	{
		h.feed("enum");
		h.feed(sizeof(T) * 8);
	}

	static void serialize(T value, serialization_packet & packet)
	{
		packet.write(&value, sizeof(value));
	}

	static T deserialize(deserialization_packet & packet)
	{
		T value;
		packet.read((char *)&value, sizeof(value));
		return value;
	}
};

template <class>
struct is_stdarray : public std::false_type
{};

template <class T, size_t N>
struct is_stdarray<std::array<T, N>> : public std::true_type
{};

template <typename T>
inline constexpr bool is_stdarray_v = is_stdarray<T>::value;

template <typename T>
struct serialization_traits<T, std::enable_if_t<std::is_aggregate_v<T> && !is_stdarray_v<T>>>
{
	template <size_t I>
	static constexpr void aux2(details::hash_context & h)
	{
		if constexpr (I > 0)
			h.feed(",");
		serialization_traits<boost::pfr::tuple_element_t<I, T>>::type_hash(h);
	}

	template <size_t... I>
	static constexpr void aux1(details::hash_context & h, std::index_sequence<I...>)
	{
		(aux2<I>(h), ...);
	}

	static constexpr void type_hash(details::hash_context & h)
	{
		h.feed("structure{");
		aux1(h, std::make_index_sequence<boost::pfr::tuple_size_v<T>>());
		h.feed("}");
	}

	static void serialize(const T & value, serialization_packet & packet)
	{
		boost::pfr::for_each_field(value, [&](const auto & x) { packet.serialize(x); });
	}

	static T deserialize(deserialization_packet & packet)
	{
		T value;

		boost::pfr::for_each_field(
		        value, [&](auto & x) { x = packet.deserialize<std::remove_reference_t<decltype(x)>>(); });

		return value;
	}
};

template <>
struct serialization_traits<std::string>
{
	static constexpr void type_hash(details::hash_context & h)
	{
		h.feed("string");
	}

	static void serialize(const std::string & value, serialization_packet & packet)
	{
		packet.serialize<uint64_t>(value.size());
		packet.write(value.data(), value.size());
	}

	static std::string deserialize(deserialization_packet & packet)
	{
		std::string value;
		size_t size = packet.deserialize<uint64_t>();

		packet.check_remaining_size(size);

		value.resize(size);
		packet.read(value.data(), size);
		return value;
	}
};

template <typename T>
struct serialization_traits<std::vector<T>>
{
	static constexpr void type_hash(details::hash_context & h)
	{
		h.feed("vector<");
		serialization_traits<T>::type_hash(h);
		h.feed(">");
	}

	static void serialize(const std::vector<T> & value, serialization_packet & packet)
	{
		packet.serialize<uint16_t>(value.size());

		if constexpr (std::is_arithmetic_v<T>)
		{
			packet.write(value.data(), value.size() * sizeof(T));
		}
		else
		{
			for (const T & i: value)
				packet.serialize<T>(i);
		}
	}

	static std::vector<T> deserialize(deserialization_packet & packet)
	{
		std::vector<T> value;
		size_t size = packet.deserialize<uint16_t>();

		if constexpr (std::is_arithmetic_v<T>)
		{
			packet.check_remaining_size(size * sizeof(T));
			value.resize(size);
			packet.read(value.data(), size * sizeof(T));
		}
		else
		{
			value.reserve(size);
			for (size_t i = 0; i < size; i++)
			{
				value.emplace_back(packet.deserialize<T>());
			}
		}

		return value;
	}
};

template <typename T>
struct serialization_traits<std::optional<T>>
{
	static constexpr void type_hash(details::hash_context & h)
	{
		h.feed("optional<");
		serialization_traits<T>::type_hash(h);
		h.feed(">");
	}

	static void serialize(const std::optional<T> & value, serialization_packet & packet)
	{
		if (value)
		{
			packet.serialize<bool>(true);
			packet.serialize<T>(*value);
		}
		else
		{
			packet.serialize<bool>(false);
		}
	}

	static std::optional<T> deserialize(deserialization_packet & packet)
	{
		if (packet.deserialize<bool>())
			return packet.deserialize<T>();
		else
			return std::nullopt;
	}
};

template <typename T, size_t N>
struct serialization_traits<std::array<T, N>>
{
	static constexpr void type_hash(details::hash_context & h)
	{
		h.feed("array<");
		serialization_traits<T>::type_hash(h);
		h.feed(",");
		h.feed(N);
		h.feed(">");
	}

	static void serialize(const std::array<T, N> & value, serialization_packet & packet)
	{
		for (const T & i: value)
			packet.serialize<T>(i);
	}

	static std::array<T, N> deserialize(deserialization_packet & packet)
	{
		std::array<T, N> value;

		for (size_t i = 0; i < N; i++)
		{
			value[i] = packet.deserialize<T>();
		}

		return value;
	}
};

template <typename... T>
struct serialization_traits<std::variant<T...>>
{
	template <typename U, typename... V>
	static constexpr void type_list_hash(details::hash_context & h)
	{
		serialization_traits<U>::type_hash(h);

		if constexpr (sizeof...(V) > 0)
		{
			h.feed(",");
			type_list_hash<V...>(h);
		}
	}

	static constexpr void type_hash(details::hash_context & h)
	{
		h.feed("variant<");
		type_list_hash<T...>(h);
		h.feed(">");
	}

	static void serialize(const std::variant<T...> & value, serialization_packet & packet)
	{
		packet.serialize<uint8_t>(value.index());
		std::visit([&](const auto & x) { packet.serialize(x); }, value);
	}

	template <size_t I>
	using i_th_type = std::tuple_element_t<I, std::tuple<T...>>;

	template <size_t... I>
	static std::variant<T...> deserialize_aux(deserialization_packet & packet, size_t type_index, std::index_sequence<I...>)
	{
		std::variant<T...> value;
		((I == type_index ? (void)(value = packet.deserialize<i_th_type<I>>()) : (void)0), ...);
		return value;
	}

	static std::variant<T...> deserialize(deserialization_packet & packet)
	{
		uint8_t type_index = packet.deserialize<uint8_t>();
		if (type_index >= sizeof...(T))
			throw deserialization_error();

		return deserialize_aux(packet, type_index, std::make_index_sequence<sizeof...(T)>());
	}
};

template <typename Rep, typename Period>
struct serialization_traits<std::chrono::duration<Rep, Period>>
{
	static constexpr void type_hash(details::hash_context & h)
	{
		h.feed("duration<");
		serialization_traits<Rep>::type_hash(h);
		h.feed(",");
		h.feed(Period::num);
		h.feed("/");
		h.feed(Period::den);
		h.feed(">");
	}

	static void serialize(const std::chrono::duration<Rep, Period> & value, serialization_packet & packet)
	{
		packet.serialize<Rep>(value.count());
	}

	static std::chrono::duration<Rep, Period> deserialize(deserialization_packet & packet)
	{
		Rep nsec = packet.deserialize<Rep>();
		return std::chrono::duration<Rep, Period>{nsec};
	}
};

template <>
struct serialization_traits<std::span<uint8_t>>
{
	static constexpr void type_hash(details::hash_context & h)
	{
		h.feed("span<uint8_t>");
	}

	static void serialize(const std::span<uint8_t> & value, serialization_packet & packet)
	{
		packet.serialize<uint16_t>(value.size());
		packet.write(value);
	}

	static std::span<uint8_t> deserialize(deserialization_packet & packet)
	{
		size_t size = packet.deserialize<uint16_t>();
		return packet.read_span(size);
	}
};

template <>
struct serialization_traits<data_holder>
{
	static constexpr void type_hash(details::hash_context & h)
	{
	}

	static void serialize(const data_holder &, serialization_packet &)
	{
	}

	static data_holder deserialize(deserialization_packet & packet)
	{
		data_holder value;
		size_t size;
		std::tie(size, value.c) = packet.steal_buffer();
		return value;
	}
};

template <typename T1, typename T2>
struct serialization_traits<std::pair<T1, T2>>
{
	static constexpr void type_hash(details::hash_context & h)
	{
		h.feed("pair<");
		serialization_traits<T1>::type_hash(h);
		h.feed(",");
		serialization_traits<T2>::type_hash(h);
		h.feed(">");
	}

	static void serialize(const std::pair<T1, T2> & value, serialization_packet & packet)
	{
		packet.serialize<T1>(value.first);
		packet.serialize<T2>(value.second);
	}

	static std::pair<T1, T2> deserialize(deserialization_packet & packet)
	{
		T1 first = packet.deserialize<T1>();
		T2 second = packet.deserialize<T2>();

		return {first, second};
	}
};

template <typename T>
constexpr uint64_t serialization_type_hash()
{
	details::hash_context h;
	serialization_traits<T>::type_hash(h);
	return h.hash;
}

} // namespace xrt::drivers::wivrn
