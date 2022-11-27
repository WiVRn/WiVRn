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
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <variant>
#include <vector>

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

public:
	serialization_packet() = default;

	void write(const void * data, size_t size)
	{
		size_t index = buffer.size();
		buffer.resize(index + size);
		memcpy(&buffer[index], data, size);
	}

	const uint8_t * data() const
	{
		return buffer.data();
	}
	size_t size() const
	{
		return buffer.size();
	}

	template <typename T>
	void serialize(const T & value)
	{
		serialization_traits<T>::serialize(value, *this);
	}

	operator std::vector<uint8_t> &&() &&
	{
		return std::move(buffer);
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
	void deserialize(T& v)
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
		packet.serialize<uint32_t>(value.index());
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
		uint32_t type_index = packet.deserialize<uint32_t>();
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

namespace unit_tests
{
template <typename T>
constexpr uint64_t hash(T s)
{
	return details::hash_context{}.feed(s);
}

static_assert(hash("test") == 0xf9e6e6ef197c2b25);
static_assert(hash(1234) == 0x1fabbdf10314a21d);
static_assert(hash(-1234) == 0x77fa1c9653db5a84);
static_assert(hash(0) == 0xaf63ad4c86019caf);

template <typename T>
constexpr uint64_t serialization_type_hash()
{
	details::hash_context h;
	serialization_traits<T>::type_hash(h);
	return h.hash;
}

static_assert(serialization_type_hash<bool>() == hash("uint8"));
static_assert(serialization_type_hash<int>() == hash("int32"));
static_assert(serialization_type_hash<float>() == hash("float32"));
static_assert(serialization_type_hash<double>() == hash("float64"));
static_assert(serialization_type_hash<std::chrono::nanoseconds>() == hash("duration<int64,1/1000000000>"));
static_assert(serialization_type_hash<std::optional<int>>() == hash("optional<int32>"));
static_assert(serialization_type_hash<std::vector<int>>() == hash("vector<int32>"));
static_assert(serialization_type_hash<std::array<int, 42>>() == hash("array<int32,42>"));
static_assert(serialization_type_hash<std::string>() == hash("string"));
static_assert(serialization_type_hash<std::variant<int, float>>() == hash("variant<int32,float32>"));

struct test
{
	int x;
	float y;
};
static_assert(serialization_type_hash<test>() == hash("structure{int32,float32}"));
} // namespace unit_tests

} // namespace xrt::drivers::wivrn
