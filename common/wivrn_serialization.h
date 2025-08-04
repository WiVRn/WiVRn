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
#include "smp.h"
#include <array>
#include <boost/pfr.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <variant>
#include <vector>

#include "wivrn_serialization_types.h"

namespace wivrn
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

class deserialization_error : public std::runtime_error
{
	static std::string hexdump(std::span<uint8_t> raw_data)
	{
		std::string s;
		char buf[10];

		for (int i = 0; i < raw_data.size(); i += 16)
		{
			sprintf(buf, "%04x ", i);
			s += buf;
			for (int j = i; j < std::min<int>(raw_data.size(), i + 16); j++)
			{
				sprintf(buf, "%02x ", (int)raw_data[j]);
				s += buf;
			}
			s += "\n";
		}

		return s;
	}

public:
	deserialization_error(std::span<uint8_t> raw_data) :
	        std::runtime_error("Deserialization error\n" + hexdump(raw_data)) {}
};

class serialization_error : public std::runtime_error
{
public:
	serialization_error() :
	        std::runtime_error("Serialization error") {}
};

template <typename T, typename Enable = void>
struct serialization_traits;

template <typename T>
size_t serialized_size(const T & x)
{
	return serialization_traits<T>::size(x);
}

constexpr size_t serialized_size_of_size(size_t size)
{
	if (size < 0x7fff)
		return sizeof(uint16_t);
	else if (size < 0x7fff'ffff)
		return 2 * sizeof(uint16_t);
	else
		throw serialization_error{};
}

class serialization_packet
{
	std::vector<uint8_t> buffer;
	// Either the size to read from the buffer, or an actual span
	// Last element is always an integer
	std::vector<std::variant<size_t, std::span<uint8_t>>> spans = {size_t(0)};
	// expanded spans: offsets are expanded to point to the buffer
	std::vector<std::span<uint8_t>> exp_spans;

public:
	// Minimum size to prefer a span over data copy
	static constexpr size_t span_min_size = 32;

	serialization_packet() = default;

	void clear()
	{
		buffer.clear();
		spans.clear();
		spans.push_back({size_t(0)});
	}

	void write(const void * data, size_t size)
	{
		auto d = (uint8_t *)data;
		buffer.insert(buffer.end(), d, d + size);
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

	constexpr void serialize_size(size_t size)
	{
		if (size < 0x7fff)
		{
			serialize<uint16_t>(size);
		}
		else if (size < 0x7fff'ffff)
		{
			serialize<uint16_t>((size & 0x7fff) | 0x8000);
			serialize<uint16_t>(size >> 15);
		}
		else
		{
			throw serialization_error{};
		}
	}

	operator std::vector<std::span<uint8_t>> *()
	{
		return &this->operator std::vector<std::span<uint8_t>> &();
	}
	operator std::vector<std::span<uint8_t>> &()
	{
		exp_spans.clear();
		struct visitor
		{
			std::vector<uint8_t>::iterator it;
			std::vector<std::span<uint8_t>> & exp_spans;
			void operator()(size_t s)
			{
				if (s > 0)
					exp_spans.emplace_back(it, it + s);
				it += s;
			}
			void operator()(const std::span<uint8_t> & span)
			{
				exp_spans.push_back(span);
			}
		};
		visitor v{.it = buffer.begin(), .exp_spans = exp_spans};
		for (const auto & span_variant: spans)
		{
			std::visit(v, span_variant);
		}
		return exp_spans;
	}
};

class deserialization_packet
{
	std::shared_ptr<uint8_t[]> memory;
	std::span<uint8_t> buffer;

public:
	std::span<uint8_t> initial_buffer;
	deserialization_packet() = default;
	explicit deserialization_packet(std::shared_ptr<uint8_t[]> memory, std::span<uint8_t> buffer) :
	        memory(memory),
	        buffer(buffer),
	        initial_buffer(buffer)
	{}

	void read(void * data, size_t size)
	{
		check_remaining_size(size);

		memcpy(data, buffer.data(), size);
		buffer = buffer.subspan(size);
	}

	std::span<uint8_t> read_span(size_t size)
	{
		check_remaining_size(size);
		auto res = buffer.first(size);
		buffer = buffer.subspan(size);
		return res;
	}

	bool empty() const
	{
		return buffer.empty();
	}

	void check_remaining_size(size_t min_size) const
	{
		if (min_size > buffer.size_bytes())
			throw deserialization_error(initial_buffer);
	}

	template <typename T>
	T deserialize()
	{
		return serialization_traits<T>::deserialize(*this);
	}

	size_t deserialize_size()
	{
		size_t size = deserialize<uint16_t>();

		if (size & 0x8000)
			size = (size & 0x7fff) | (deserialize<uint16_t>() << 15);

		return size;
	}

	template <typename T>
	void deserialize(T & v)
	{
		v = deserialize<T>();
	}

	std::shared_ptr<uint8_t[]> steal_buffer()
	{
		return std::move(memory);
	}
};

namespace details
{

// Partition a structure into trivial portions (or single element when not trivial)
template <
        typename T,                                               // structure to partition
        typename Indices = std::tuple<>,                          // indices already partitionned
        typename Current_Indices = std::integer_sequence<size_t>, // indices of current partition
        size_t i = 0,                                             // index of the first of remaining elements
        size_t offset = 0,                                        // offset of previous element + its size
        typename Enable = void>
struct trivial_bits;

template <
        typename T,
        typename... Indices,
        size_t... current_indices,
        size_t i,
        size_t offset>
struct trivial_bits<
        T,
        std::tuple<Indices...>,
        std::integer_sequence<size_t, current_indices...>,
        i,
        offset,
        std::enable_if_t<boost::pfr::tuple_size_v<T> == i, void>>
{
	// termination condition
	using types = std::conditional_t<sizeof...(current_indices) == 0,
	                                 std::tuple<Indices...>,
	                                 std::tuple<Indices..., std::integer_sequence<size_t, current_indices...>>>;
};

template <
        typename T,
        typename... Indices,
        size_t... current_indices,
        size_t i,
        size_t offset>
struct trivial_bits<
        T,
        std::tuple<Indices...>,
        std::integer_sequence<size_t, current_indices...>,
        i,
        offset,
        std::enable_if_t<(boost::pfr::tuple_size_v<T> > i), void>>
{
	using Ti = boost::pfr::tuple_element_t<i, T>;
	static constexpr size_t alignment = std::alignment_of_v<Ti>;
	static constexpr size_t padding = (alignment - offset) % alignment;
	static constexpr bool aligned = padding == 0;
	static constexpr bool trivial = serialization_traits<Ti>::is_trivially_serializable();

	using types = trivial_bits<
	        T,
	        std::conditional_t<
	                trivial and aligned,
	                std::tuple<Indices...>,
	                std::conditional_t<trivial,
	                                   std::conditional_t<sizeof...(current_indices) == 0,
	                                                      std::tuple<Indices...>,
	                                                      std::tuple<Indices..., std::integer_sequence<size_t, current_indices...>>>,
	                                   std::conditional_t<sizeof...(current_indices) == 0,
	                                                      std::tuple<Indices..., std::integer_sequence<size_t, i>>,
	                                                      std::tuple<Indices..., std::integer_sequence<size_t, current_indices...>, std::integer_sequence<size_t, i>>>>>,
	        std::conditional_t<
	                trivial and aligned,
	                std::integer_sequence<size_t, current_indices..., i>,
	                std::conditional_t<trivial,
	                                   std::integer_sequence<size_t, i>,
	                                   std::integer_sequence<size_t>>>,
	        i + 1,
	        offset + padding + sizeof(boost::pfr::tuple_element_t<i, T>)>::types;
};

// Serialize bits of a structure, partitionned by trivial_bits
template <typename T, typename Bits>
struct serialize_bits;

template <typename T>
struct serialize_bits<T, std::tuple<>>
{
	// termination case, nothing to do
	static void serialize(const T &, serialization_packet &) {}
	static void deserialize(T &, deserialization_packet &) {}
	static size_t size(const T &)
	{
		return 0;
	}
};

template <typename T, size_t i, typename... Bits>
struct serialize_bits<T, std::tuple<std::integer_sequence<size_t, i>, Bits...>>
{
	// single element case
	static void serialize(const T & t, serialization_packet & p)
	{
		serialization_traits<boost::pfr::tuple_element_t<i, T>>::serialize(boost::pfr::get<i>(t), p);
		serialize_bits<T, std::tuple<Bits...>>::serialize(t, p);
	}
	static void deserialize(T & t, deserialization_packet & p)
	{
		// serialization of a single element
		boost::pfr::get<i>(t) = serialization_traits<boost::pfr::tuple_element_t<i, T>>::deserialize(p);
		serialize_bits<T, std::tuple<Bits...>>::deserialize(t, p);
	}
	static size_t size(const T & t)
	{
		return serialized_size(boost::pfr::get<i>(t)) + serialize_bits<T, std::tuple<Bits...>>::size(t);
	}
};

template <typename T, size_t i, size_t... mid, typename... Bits>
struct serialize_bits<T, std::tuple<std::integer_sequence<size_t, i, mid...>, Bits...>>
{
	// multiple elements, trivial serialization
	static void serialize(const T & t, serialization_packet & p)
	{
		constexpr auto size = (sizeof(boost::pfr::tuple_element_t<mid, T>) + ... + sizeof(boost::pfr::tuple_element_t<i, T>));
		auto first = (uint8_t *)&boost::pfr::get<i, T>(t);
		if constexpr (size > serialization_packet::span_min_size)
			p.write(std::span(first, size));
		else
			p.write(first, size);
		serialize_bits<T, std::tuple<Bits...>>::serialize(t, p);
	}

	static void deserialize(T & t, deserialization_packet & p)
	{
		auto size = (sizeof(boost::pfr::tuple_element_t<mid, T>) + ... + sizeof(boost::pfr::tuple_element_t<i, T>));
		auto first = (uint8_t *)&boost::pfr::get<i, T>(t);
		p.read(first, size);
		serialize_bits<T, std::tuple<Bits...>>::deserialize(t, p);
	}
	static size_t size(const T & t)
	{
		return (sizeof(boost::pfr::tuple_element_t<mid, T>) + ... + sizeof(boost::pfr::tuple_element_t<i, T>)) + serialize_bits<T, std::tuple<Bits...>>::size(t);
	}
};

} // namespace details

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

	static bool consteval is_trivially_serializable()
	{
		return true;
	}

	static size_t size(const T &)
	{
		return sizeof(T);
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
	static bool consteval is_trivially_serializable()
	{
		return true;
	}

	static size_t size(const T &)
	{
		return sizeof(T);
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
	using bits = typename details::trivial_bits<T>::types;
	static constexpr size_t bits_size = std::tuple_size_v<bits>;

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
		details::serialize_bits<T, bits>::serialize(value, packet);
	}

	static T deserialize(deserialization_packet & packet)
	{
		T value;
		details::serialize_bits<T, bits>::deserialize(value, packet);
		return value;
	}

	template <size_t... I>
	static constexpr size_t ts_aux_size(std::index_sequence<I...>)
	{
		return (sizeof(boost::pfr::tuple_element_t<I, T>) + ... + 0);
	}

	template <size_t... I>
	static constexpr bool ts_aux_trivial(std::index_sequence<I...>)
	{
		return (serialization_traits<boost::pfr::tuple_element_t<I, T>>::is_trivially_serializable() and ...);
	}

	static bool consteval is_trivially_serializable()
	{
		return sizeof(T) == ts_aux_size(std::make_index_sequence<boost::pfr::tuple_size_v<T>>()) and ts_aux_trivial(std::make_index_sequence<boost::pfr::tuple_size_v<T>>());
	}

	static size_t size(const T & value)
	{
		if constexpr (is_trivially_serializable())
			return sizeof(T);
		else
			return details::serialize_bits<T, bits>::size(value);
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
		packet.serialize_size(value.size());
		packet.write(value.data(), value.size());
	}

	static std::string deserialize(deserialization_packet & packet)
	{
		std::string value;
		size_t size = packet.deserialize_size();

		packet.check_remaining_size(size);

		value.resize(size);
		packet.read(value.data(), size);
		return value;
	}

	static bool consteval is_trivially_serializable()
	{
		return false;
	}
	static size_t size(const std::string & value)
	{
		return serialized_size_of_size(value.size()) + value.size();
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
		packet.serialize_size(value.size());

		if constexpr (serialization_traits<T>::is_trivially_serializable())
		{
			packet.write(std::span((uint8_t *)value.data(), value.size() * sizeof(T)));
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
		size_t size = packet.deserialize_size();

		if constexpr (serialization_traits<T>::is_trivially_serializable())
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
	static bool consteval is_trivially_serializable()
	{
		return false;
	}

	static size_t size(const std::vector<T> & value)
	{
		if constexpr (is_trivially_serializable())
			return serialized_size_of_size(value.size()) + value.size() * sizeof(T);
		else
		{
			size_t res = serialized_size_of_size(value.size());
			for (const auto & item: value)
				res += serialized_size(item);
			return res;
		}
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
	static bool consteval is_trivially_serializable()
	{
		return false;
	}

	static size_t size(const std::optional<T> & value)
	{
		return 1 + (value ? serialized_size(*value) : 0);
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
		if constexpr (serialization_traits<T>::is_trivially_serializable())
		{
			if constexpr (N * sizeof(T) > serialization_packet::span_min_size)
				packet.write(std::span((uint8_t *)value.data(), value.size() * sizeof(T)));
			else
				packet.write(value.data(), value.size() * sizeof(T));
		}
		else
		{
			for (const T & i: value)
				packet.serialize<T>(i);
		}
	}

	static std::array<T, N> deserialize(deserialization_packet & packet)
	{
		std::array<T, N> value;
		if constexpr (serialization_traits<T>::is_trivially_serializable())
		{
			packet.check_remaining_size(N * sizeof(T));
			packet.read(value.data(), N * sizeof(T));
		}
		else
		{
			for (size_t i = 0; i < N; i++)
			{
				value[i] = packet.deserialize<T>();
			}
		}

		return value;
	}

	static bool consteval is_trivially_serializable()
	{
		return serialization_traits<T>::is_trivially_serializable() and sizeof(std::array<T, N>) == sizeof(T) * N;
	}

	static size_t size(const std::array<T, N> & value)
	{
		if constexpr (is_trivially_serializable())
			return N * sizeof(T);
		else
		{
			size_t res = 0;
			for (const auto & item: value)
				res += serialized_size(item);
			return res;
		}
	}
};

template <typename... T>
struct serialization_traits<std::variant<T...>>
{
	using size_type = uint8_t;
	static_assert(sizeof...(T) <= std::numeric_limits<size_type>::max() + 1);

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
		packet.serialize<size_type>(value.index());
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
		size_type type_index = packet.deserialize<size_type>();
		if (type_index >= sizeof...(T))
			throw deserialization_error(packet.initial_buffer);

		return deserialize_aux(packet, type_index, std::make_index_sequence<sizeof...(T)>());
	}
	static bool consteval is_trivially_serializable()
	{
		return false;
	}

	static size_t size(const std::variant<T...> & value)
	{
		return sizeof(size_type) + std::visit([](const auto & x) { return serialized_size(x); }, value);
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

	static bool consteval is_trivially_serializable()
	{
		return sizeof(std::chrono::duration<Rep, Period>) == sizeof(Rep);
	}

	static size_t size(const std::chrono::duration<Rep, Period> & x)
	{
		return serialization_traits<Rep>::size(x);
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
		packet.serialize_size(value.size());
		packet.write(value);
	}

	static std::span<uint8_t> deserialize(deserialization_packet & packet)
	{
		size_t size = packet.deserialize_size();
		return packet.read_span(size);
	}

	static bool consteval is_trivially_serializable()
	{
		return false;
	}

	static size_t size(const std::span<uint8_t> & value)
	{
		return serialized_size_of_size(value.size()) + value.size_bytes();
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
		value.c = packet.steal_buffer();
		return value;
	}
	static bool consteval is_trivially_serializable()
	{
		return false;
	}

	static size_t size(const data_holder &)
	{
		return 0;
	}
};

template <>
struct serialization_traits<crypto::bignum>
{
	static constexpr void type_hash(details::hash_context & h)
	{
		h.feed("bignum");
	}

	static void serialize(const crypto::bignum & value, serialization_packet & packet)
	{
		serialization_traits<std::string>::serialize(value.to_data(), packet);
	}

	static crypto::bignum deserialize(deserialization_packet & packet)
	{
		return crypto::bignum::from_data(serialization_traits<std::string>::deserialize(packet));
	}

	static bool consteval is_trivially_serializable()
	{
		return false;
	}

	static size_t size(const crypto::bignum & value)
	{
		return serialized_size_of_size(value.data_size()) + value.data_size();
	}
};

template <typename T>
constexpr uint64_t serialization_type_hash(int revision)
{
	details::hash_context h;
	serialization_traits<T>::type_hash(h);

	if (revision)
		h.feed(revision);

	return h.hash;
}

} // namespace wivrn
