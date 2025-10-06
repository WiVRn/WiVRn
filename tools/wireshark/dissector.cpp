/*
 * WiVRn VR streaming
 * Copyright (C) 2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2024  Patrick Nicolas <patricknicolas@laposte.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
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

#include <algorithm>
#include <chrono>
#include <cmath>
#include <epan/ftypes/ftypes.h>
#include <epan/packet.h>
#include <epan/proto.h>
#include <epan/tvbuff.h>
#include <epan/unit_strings.h>
#include <ws_version.h>

#include <boost/pfr.hpp>
#include <magic_enum.hpp>

#include "smp.h"
#include "wivrn_config.h"
#include "wivrn_packets.h"

#if WIRESHARK_VERSION_MAJOR > 4 || (WIRESHARK_VERSION_MAJOR == 4 && WIRESHARK_VERSION_MINOR >= 4)
static inline uint16_t get_uint16(tvbuff_t * tvb, const int offset, const unsigned encoding)
{
	return tvb_get_uint16(tvb, offset, encoding);
}

static inline uint16_t get_uint8(tvbuff_t * tvb, const int offset)
{
	return tvb_get_uint8(tvb, offset);
}
#else
static inline uint16_t get_uint16(tvbuff_t * tvb, const int offset, const unsigned encoding)
{
	return tvb_get_guint16(tvb, offset, encoding);
}

static inline uint16_t get_uint8(tvbuff_t * tvb, const int offset)
{
	return tvb_get_guint8(tvb, offset);
}
#endif

using namespace wivrn;

namespace
{
int proto;

namespace details
{
template <class>
struct is_stdarray : public std::false_type
{};

template <class T, size_t N>
struct is_stdarray<std::array<T, N>> : public std::true_type
{};

template <typename T>
inline constexpr bool is_stdarray_v = is_stdarray<T>::value;

template <typename T>
inline constexpr bool is_struct_v = std::is_aggregate_v<T> && !is_stdarray_v<T>;

template <size_t N>
struct fixed_string
{
	char value[N];

	consteval fixed_string() = default;

	consteval fixed_string(const char (&str)[N])
	{
		std::copy_n(str, N, value);
	}

	consteval fixed_string(std::string_view str)
	{
		std::copy_n(str.data(), N - 1, value);
		value[N - 1] = '\0';
	}

	constexpr size_t size() const
	{
		return N;
	}
};

template <size_t N1, size_t N2>
consteval auto join(const fixed_string<N1> & str1, const fixed_string<N2> & str2) -> fixed_string<N1 + N2>
{
	fixed_string<N1 + N2> value;
	std::copy_n(str1.value, N1, &value.value[0]);
	value.value[N1 - 1] = '.';
	std::copy_n(str2.value, N2, &value.value[N1]);
	return value;
}

template <size_t N1, size_t N2>
consteval auto join(const fixed_string<N1> & str1, std::string_view str2)
{
	fixed_string<N1 + N2 + 1> value;
	std::copy_n(str1.value, N1, &value.value[0]);
	value.value[N1 - 1] = '.';
	std::copy_n(str2.data(), str2.size(), &value.value[N1]);
	return value;
}

std::string name_from_abbrev(const std::string & name)
{
	auto pos = name.find_last_of('.');
	if (pos == std::string::npos)
		return name;
	else
		return name.substr(pos + 1);
}

template <typename T>
constexpr auto type_name()
{
	constexpr std::string_view sv = __PRETTY_FUNCTION__;
	constexpr std::string_view sv2 = sv.substr(sv.find("T = ") + 4);
	constexpr std::string_view sv3 = sv2.substr(0, sv2.size() - 1);

	constexpr size_t pos = sv3.rfind("::");
	constexpr size_t pos2 = pos == std::string_view::npos ? 0 : (pos + 2);

	constexpr size_t pos3 = sv3.find_first_of(", ");

	constexpr std::string_view sv4 = sv3.substr(pos2,
	                                            pos3 == std::string_view::npos ? pos3 : pos3 - pos2);

	return fixed_string<sv4.size() + 1>(sv4);
}

template <typename T>
ftenum field_type = FT_NONE;

template <>
ftenum field_type<bool> = FT_BOOLEAN;
template <>
ftenum field_type<uint8_t> = FT_UINT8;
template <>
ftenum field_type<uint16_t> = FT_UINT16;
template <>
ftenum field_type<uint32_t> = FT_UINT32;
template <>
ftenum field_type<uint64_t> = FT_UINT64;
template <>
ftenum field_type<int8_t> = FT_INT8;
template <>
ftenum field_type<int16_t> = FT_INT16;
template <>
ftenum field_type<int32_t> = FT_INT32;
template <>
ftenum field_type<int64_t> = FT_INT64;
template <>
ftenum field_type<float> = FT_FLOAT;
template <>
ftenum field_type<double> = FT_DOUBLE;

} // namespace details

template <details::fixed_string abbrev, typename T, typename Enable = void>
struct tree_traits;

std::unordered_map<std::string, int> subtree_handles;
std::vector<hf_register_info> fields;

template <details::fixed_string abbrev, typename T>
struct tree_traits<abbrev, T, std::enable_if_t<std::is_arithmetic_v<T>>>
{
	static inline int field_handle = -1;
	static void info()
	{
		hf_register_info hf = {
		        .p_id = &field_handle,
		        .hfinfo = {
		                .name = strdup(details::name_from_abbrev(abbrev.value).c_str()),
		                .abbrev = abbrev.value,
		                .type = details::field_type<T>,
		                .display = BASE_DEC,
		                .strings = nullptr,
		                .bitmask = 0,
		                .blurb = "",
		        }};
		HFILL_INIT(hf);

		fields.push_back(hf);
	}

	static void dissect(proto_tree * tree, tvbuff_t * tvb, int & start)
	{
		std::string s = abbrev.value;
		auto member = s.substr(s.find_last_of('.') + 1);

		if (std::is_same_v<T, float> && member.starts_with("angle"))
		{
			float value = tvb_get_ieee_float(tvb, start, ENC_LITTLE_ENDIAN) * 180 / M_PI;
			proto_tree_add_float_format_value(tree, field_handle, tvb, start, sizeof(T), value, "%f deg", value);
		}
		else
			proto_tree_add_item(tree, field_handle, tvb, start, sizeof(T), ENC_LITTLE_ENDIAN);
		start += sizeof(T);
	}

	static size_t size(tvbuff_t * tvb, int & start)
	{
		start += sizeof(T);
		return sizeof(T);
	}
};

template <details::fixed_string abbrev, typename T>
struct tree_traits<abbrev, T, std::enable_if_t<std::is_enum_v<T>>>
{
	static inline int field_handle = -1;

	using U = std::underlying_type_t<T>;
	constexpr static auto entries = magic_enum::enum_entries<T>();

	static void info()
	{
		value_string * strings = new value_string[entries.size() + 1];
		for (size_t i = 0; i < entries.size(); i++)
		{
			strings[i].value = (uint32_t)entries[i].first;
			strings[i].strptr = strndup(entries[i].second.data(), entries[i].second.size());
		}
		strings[entries.size()] = {0, nullptr};

		hf_register_info hf = {
		        .p_id = &field_handle,
		        .hfinfo = {
		                .name = strdup(details::name_from_abbrev(abbrev.value).c_str()),
		                .abbrev = abbrev.value,
		                .type = details::field_type<U>,
		                .display = BASE_DEC,
		                .strings = strings,
		                .bitmask = 0,
		                .blurb = "",
		        }};
		HFILL_INIT(hf);

		fields.push_back(hf);
	}

	static void dissect(proto_tree * tree, tvbuff_t * tvb, int & start)
	{
		proto_tree_add_item(tree, field_handle, tvb, start, sizeof(U), ENC_LITTLE_ENDIAN);
		start += sizeof(U);
	}

	static size_t size(tvbuff_t * tvb, int & start)
	{
		start += sizeof(U);
		return sizeof(U);
	}
};

template <details::fixed_string abbrev, typename T>
struct tree_traits<abbrev, T, std::enable_if_t<details::is_struct_v<T>>>
{
	static inline int field_handle = -1;

	template <size_t I>
	static void info_aux2()
	{
		constexpr static auto names = boost::pfr::names_as_array<T>();
		tree_traits<details::join<abbrev.size(), names[I].size()>(abbrev, names[I]), boost::pfr::tuple_element_t<I, T>>::info();
	}

	template <size_t... I>
	static void info_aux1(std::index_sequence<I...>)
	{
		(info_aux2<I>(), ...);
	}

	static void info()
	{
		subtree_handles.emplace(abbrev.value, -1);
		hf_register_info hf = {
		        .p_id = &field_handle,
		        .hfinfo = {
		                .name = strdup(details::name_from_abbrev(abbrev.value).c_str()),
		                .abbrev = abbrev.value,
		                .type = FT_NONE,
		                .display = BASE_NONE,
		                .strings = nullptr,
		                .bitmask = 0,
		                .blurb = "",
		        }};
		HFILL_INIT(hf);

		fields.push_back(hf);

		info_aux1(std::make_index_sequence<boost::pfr::tuple_size_v<T>>());
	}

	template <size_t I>
	static void dissect_aux2(proto_tree * tree, tvbuff_t * tvb, int & start)
	{
		constexpr static auto names = boost::pfr::names_as_array<T>();
		tree_traits<details::join<abbrev.size(), names[I].size()>(abbrev, names[I]), boost::pfr::tuple_element_t<I, T>>::dissect(tree, tvb, start);
	}

	template <size_t... I>
	static void dissect_aux1([[maybe_unused]] proto_tree * tree, [[maybe_unused]] tvbuff_t * tvb, int & start, std::index_sequence<I...>)
	{
		(dissect_aux2<I>(tree, tvb, start), ...);
	}

	static void dissect(proto_tree * tree, tvbuff_t * tvb, int & start)
	{
		int start2 = start;
		size_t size2 = size(tvb, start2);

		proto_item * ti = proto_tree_add_item(tree, field_handle, tvb, start, size2, ENC_NA);
		proto_tree * subtree = proto_item_add_subtree(ti, subtree_handles.at(abbrev.value));

		dissect_aux1(subtree, tvb, start, std::make_index_sequence<boost::pfr::tuple_size_v<T>>());
	}

	template <size_t I>
	static size_t size_aux2(tvbuff_t * tvb, int & start)
	{
		constexpr static auto names = boost::pfr::names_as_array<T>();
		return tree_traits<details::join<abbrev.size(), names[I].size()>(abbrev, names[I]), boost::pfr::tuple_element_t<I, T>>::size(tvb, start);
	}

	template <size_t... I>
	static size_t size_aux1([[maybe_unused]] tvbuff_t * tvb, int & start, std::index_sequence<I...>)
	{
		return (size_aux2<I>(tvb, start) + ... + 0);
	}

	static size_t size(tvbuff_t * tvb, int & start)
	{
		return size_aux1(tvb, start, std::make_index_sequence<boost::pfr::tuple_size_v<T>>());
	}
};

template <details::fixed_string abbrev>
struct tree_traits<abbrev, std::string>
{
	static inline int field_handle = -1;

	static void info()
	{
		hf_register_info hf = {
		        .p_id = &field_handle,
		        .hfinfo = {
		                .name = strdup(details::name_from_abbrev(abbrev.value).c_str()),
		                .abbrev = abbrev.value,
		                .type = FT_STRING,
#if WIRESHARK_VERSION_MAJOR >= 4 && WIRESHARK_VERSION_MINOR >= 2
		                .display = BASE_STR_WSP,
#else
		                .display = BASE_NONE,
#endif
		                .strings = nullptr,
		                .bitmask = 0,
		                .blurb = "",
		        }};
		HFILL_INIT(hf);

		fields.push_back(hf);
	}

	static void dissect(proto_tree * tree, tvbuff_t * tvb, int & start)
	{
		size_t string_size = get_uint16(tvb, start, ENC_LITTLE_ENDIAN);
		start += sizeof(uint16_t);

		proto_tree_add_item(tree, field_handle, tvb, start, string_size, ENC_STRING);

		start += string_size;
	}

	static size_t size(tvbuff_t * tvb, int & start)
	{
		size_t string_size = get_uint16(tvb, start, ENC_LITTLE_ENDIAN);
		start += sizeof(uint16_t);
		start += string_size;

		return sizeof(uint16_t) + string_size;
	}
};

template <details::fixed_string abbrev, typename T>
struct tree_traits<abbrev, std::vector<T>>
{
	static inline int field_handle = -1;

	static void info()
	{
		subtree_handles.emplace(abbrev.value, -1);
		hf_register_info hf = {
		        .p_id = &field_handle,
		        .hfinfo = {
		                .name = strdup(details::name_from_abbrev(abbrev.value).c_str()),
		                .abbrev = abbrev.value,
		                .type = FT_NONE,
		                .display = BASE_NONE,
		                .strings = nullptr,
		                .bitmask = 0,
		                .blurb = "",
		        }};
		HFILL_INIT(hf);

		fields.push_back(hf);

		tree_traits<details::join(abbrev, details::type_name<T>()), T>::info();
	}

	static void dissect(proto_tree * tree, tvbuff_t * tvb, int & start)
	{
		int start2 = start;
		size_t size2 = size(tvb, start2);

		proto_item * ti = proto_tree_add_item(tree, field_handle, tvb, start, size2, ENC_NA);
		proto_tree * subtree = proto_item_add_subtree(ti, subtree_handles.at(abbrev.value));

		size_t count = get_uint16(tvb, start, ENC_LITTLE_ENDIAN);
		start += sizeof(uint16_t);

		for (size_t i = 0; i < count; i++)
		{
			tree_traits<details::join(abbrev, details::type_name<T>()), T>::dissect(subtree, tvb, start);
		}
	}

	static size_t size(tvbuff_t * tvb, int & start)
	{
		size_t count = get_uint16(tvb, start, ENC_LITTLE_ENDIAN);
		start += sizeof(uint16_t);
		size_t size = sizeof(uint16_t);

		for (size_t i = 0; i < count; i++)
		{
			size += tree_traits<details::join(abbrev, details::type_name<T>()), T>::size(tvb, start);
		}

		return size;
	}
};

template <details::fixed_string abbrev, typename T>
struct tree_traits<abbrev, std::optional<T>>
{
	static void info()
	{
		tree_traits<abbrev, T>::info();
	}

	static void dissect(proto_tree * tree, tvbuff_t * tvb, int & start)
	{
		int start2 = start;
		size_t size2 = size(tvb, start2);

		bool has_value = get_uint8(tvb, start);

		if (has_value)
		{
			start += sizeof(uint8_t);
			tree_traits<abbrev, T>::dissect(tree, tvb, start);
		}
		else
		{
			// auto pi = proto_tree_add_format_text(tree, tvb, start, sizeof(uint8_t));
			// proto_item_set_text(pi, "none");
			start += sizeof(uint8_t);
		}
	}

	static size_t size(tvbuff_t * tvb, int & start)
	{
		bool has_value = get_uint8(tvb, start);
		start += sizeof(uint8_t);
		size_t size = sizeof(uint8_t);

		if (has_value)
		{
			size += tree_traits<abbrev, T>::size(tvb, start);
		}

		return size;
	}
};

template <details::fixed_string abbrev, typename T, size_t N>
struct tree_traits<abbrev, std::array<T, N>>
{
	static inline int field_handle = -1;

	static void info()
	{
		subtree_handles.emplace(abbrev.value, -1);
		hf_register_info hf = {
		        .p_id = &field_handle,
		        .hfinfo = {
		                .name = strdup(details::name_from_abbrev(abbrev.value).c_str()),
		                .abbrev = abbrev.value,
		                .type = FT_NONE,
		                .display = BASE_NONE,
		                .strings = nullptr,
		                .bitmask = 0,
		                .blurb = "",
		        }};
		HFILL_INIT(hf);

		fields.push_back(hf);

		tree_traits<details::join(abbrev, details::type_name<T>()), T>::info();
	}

	static void dissect(proto_tree * tree, tvbuff_t * tvb, int & start)
	{
		int start2 = start;
		size_t size2 = size(tvb, start2);

		proto_item * ti = proto_tree_add_item(tree, field_handle, tvb, start, size2, ENC_NA);
		proto_tree * subtree = proto_item_add_subtree(ti, subtree_handles.at(abbrev.value));

		for (size_t i = 0; i < N; i++)
		{
			tree_traits<details::join(abbrev, details::type_name<T>()), T>::dissect(subtree, tvb, start);
		}
	}

	static size_t size(tvbuff_t * tvb, int & start)
	{
		size_t size = 0;

		for (size_t i = 0; i < N; i++)
		{
			size += tree_traits<details::join(abbrev, details::type_name<T>()), T>::size(tvb, start);
		}

		return size;
	}
};

template <details::fixed_string abbrev, typename... T>
struct tree_traits<abbrev, std::variant<T...>>
{
	template <size_t I>
	using i_th_type = std::tuple_element_t<I, std::tuple<T...>>;

	template <size_t I>
	static void info_aux2()
	{
		tree_traits<details::join(abbrev, details::type_name<i_th_type<I>>()), i_th_type<I>>::info();
	}

	template <size_t... I>
	static void info_aux1(std::index_sequence<I...>)
	{
		(info_aux2<I>(), ...);
	}

	static void info()
	{
		info_aux1(std::make_index_sequence<sizeof...(T)>());
	}

	template <size_t... I>
	static void dissect_aux(proto_tree * tree, tvbuff_t * tvb, int & start, size_t type_index, std::index_sequence<I...>)
	{
		((I == type_index ? (void)(tree_traits<details::join(abbrev, details::type_name<i_th_type<I>>()), i_th_type<I>>::dissect(tree, tvb, start)) : (void)0), ...);
	}

	static void dissect(proto_tree * tree, tvbuff_t * tvb, int & start)
	{
		uint8_t type_index = get_uint8(tvb, start);
		start += 1;

		dissect_aux(tree, tvb, start, type_index, std::make_index_sequence<sizeof...(T)>());
	}

	template <size_t... I>
	static size_t size_aux(tvbuff_t * tvb, int & start, size_t type_index, std::index_sequence<I...>)
	{
		size_t value;
		((I == type_index ? (void)(value = tree_traits<details::join(abbrev, details::type_name<i_th_type<I>>()), i_th_type<I>>::size(tvb, start)) : (void)0), ...);
		return value;
	}

	static size_t size(tvbuff_t * tvb, int & start)
	{
		uint8_t type_index = get_uint8(tvb, start);
		start += sizeof(uint8_t);

		return sizeof(uint8_t) + size_aux(tvb, start, type_index, std::make_index_sequence<sizeof...(T)>());
	}
};

template <details::fixed_string abbrev>
struct tree_traits<abbrev, std::chrono::nanoseconds>
{
	static inline int field_handle = -1;
	using T = std::chrono::nanoseconds::rep;

	static void info()
	{
		hf_register_info hf = {
		        .p_id = &field_handle,
		        .hfinfo = {
		                .name = strdup(details::name_from_abbrev(abbrev.value).c_str()),
		                .abbrev = abbrev.value,
		                .type = details::field_type<T>,
		                .display = BASE_DEC | BASE_UNIT_STRING,
		                .strings = &units_nanoseconds,
		                .bitmask = 0,
		                .blurb = "",
		        }};
		HFILL_INIT(hf);

		fields.push_back(hf);
	}

	static void dissect(proto_tree * tree, tvbuff_t * tvb, int & start)
	{
		proto_tree_add_item(tree, field_handle, tvb, start, sizeof(T), ENC_LITTLE_ENDIAN);
		start += sizeof(T);
	}

	static size_t size(tvbuff_t * tvb, int & start)
	{
		start += sizeof(T);
		return sizeof(T);
	}
};

template <details::fixed_string abbrev>
struct tree_traits<abbrev, std::chrono::seconds>
{
	static inline int field_handle = -1;
	using T = std::chrono::seconds::rep;

	static void info()
	{
		hf_register_info hf = {
		        .p_id = &field_handle,
		        .hfinfo = {
		                .name = strdup(details::name_from_abbrev(abbrev.value).c_str()),
		                .abbrev = abbrev.value,
		                .type = details::field_type<T>,
		                .display = BASE_DEC | BASE_UNIT_STRING,
		                .strings = &units_seconds,
		                .bitmask = 0,
		                .blurb = "",
		        }};
		HFILL_INIT(hf);

		fields.push_back(hf);
	}

	static void dissect(proto_tree * tree, tvbuff_t * tvb, int & start)
	{
		proto_tree_add_item(tree, field_handle, tvb, start, sizeof(T), ENC_LITTLE_ENDIAN);
		start += sizeof(T);
	}

	static size_t size(tvbuff_t * tvb, int & start)
	{
		start += sizeof(T);
		return sizeof(T);
	}
};

template <details::fixed_string abbrev>
struct tree_traits<abbrev, std::span<uint8_t>>
{
	static inline int field_handle = -1;

	static void info()
	{
		hf_register_info hf = {
		        .p_id = &field_handle,
		        .hfinfo = {
		                .name = strdup(details::name_from_abbrev(abbrev.value).c_str()),
		                .abbrev = abbrev.value,
		                .type = FT_BYTES,
		                .display = BASE_NONE,
		                .strings = nullptr,
		                .bitmask = 0,
		                .blurb = "",
		        }};
		HFILL_INIT(hf);

		fields.push_back(hf);
	}

	static void dissect(proto_tree * tree, tvbuff_t * tvb, int & start)
	{
		size_t span_size = get_uint16(tvb, start, ENC_LITTLE_ENDIAN);
		start += sizeof(uint16_t);

		proto_tree_add_item(tree, field_handle, tvb, start, span_size, ENC_NA);

		start += span_size;
	}

	static size_t size(tvbuff_t * tvb, int & start)
	{
		size_t span_size = get_uint16(tvb, start, ENC_LITTLE_ENDIAN);
		start += sizeof(uint16_t);
		start += span_size;

		return sizeof(uint16_t) + span_size;
	}
};

template <details::fixed_string abbrev>
struct tree_traits<abbrev, data_holder>
{
	static void info()
	{
	}

	static void dissect(proto_tree * tree, tvbuff_t * tvb, int & start)
	{
	}

	static size_t size(tvbuff_t * tvb, int & start)
	{
		return 0;
	}
};

template <details::fixed_string abbrev>
struct tree_traits<abbrev, crypto::bignum>
{
	static inline int field_handle = -1;

	static void info()
	{
		hf_register_info hf = {
		        .p_id = &field_handle,
		        .hfinfo = {
		                .name = strdup(details::name_from_abbrev(abbrev.value).c_str()),
		                .abbrev = abbrev.value,
		                .type = FT_BYTES,
		                .display = BASE_NONE,
		                .strings = nullptr,
		                .bitmask = 0,
		                .blurb = "",
		        }};
		HFILL_INIT(hf);

		fields.push_back(hf);
	}

	static void dissect(proto_tree * tree, tvbuff_t * tvb, int & start)
	{
		size_t string_size = get_uint16(tvb, start, ENC_LITTLE_ENDIAN);
		start += sizeof(uint16_t);

		proto_tree_add_item(tree, field_handle, tvb, start, string_size, ENC_STRING);

		start += string_size;
	}

	static size_t size(tvbuff_t * tvb, int & start)
	{
		size_t string_size = get_uint16(tvb, start, ENC_LITTLE_ENDIAN);
		start += sizeof(uint16_t);
		start += string_size;

		return sizeof(uint16_t) + string_size;
	}
};

int dissect_wivrn(tvbuff_t * tvb, packet_info * pinfo, proto_tree * tree _U_, void * data _U_, bool tcp)
{
	col_set_str(pinfo->cinfo, COL_PROTOCOL, "WiVRn");
	/* Clear out stuff in the info column */
	col_clear(pinfo->cinfo, COL_INFO);

	proto_item * ti = proto_tree_add_item(tree, proto, tvb, 0, -1, ENC_NA);
	proto_tree * subtree = proto_item_add_subtree(ti, subtree_handles.at(""));
	int start = 0;

	if (tcp)
		start += sizeof(uint16_t);

	if (pinfo->destport == wivrn::default_port)
		tree_traits<"wivrn.from_headset", from_headset::packets>::dissect(subtree, tvb, start);
	else
		tree_traits<"wivrn.to_headset", to_headset::packets>::dissect(subtree, tvb, start);

	return tvb_captured_length(tvb);
}

int dissect_wivrn_udp(tvbuff_t * tvb, packet_info * pinfo, proto_tree * tree _U_, void * data _U_)
{
	return dissect_wivrn(tvb, pinfo, tree, data, false);
}

int dissect_wivrn_tcp(tvbuff_t * tvb, packet_info * pinfo, proto_tree * tree _U_, void * data _U_)
{
	return dissect_wivrn(tvb, pinfo, tree, data, true);
}

void proto_register_wivrn()
{
	subtree_handles.emplace("", -1);
	tree_traits<"wivrn.from_headset", from_headset::packets>::info();
	tree_traits<"wivrn.to_headset", to_headset::packets>::info();

	proto = proto_register_protocol(
	        "WiVRn protocol",
	        "WiVRn",
	        "wivrn");

	proto_register_field_array(proto, fields.data(), fields.size());

	std::vector<int *> tree;
	for (auto & [i, j]: subtree_handles)
	{
		tree.push_back(&j);
	}

	proto_register_subtree_array(tree.data(), tree.size());
}

void proto_reg_handoff_wivrn()
{
	static dissector_handle_t handle_tcp = create_dissector_handle(dissect_wivrn_tcp, proto);
	static dissector_handle_t handle_udp = create_dissector_handle(dissect_wivrn_udp, proto);
	dissector_add_uint("udp.port", wivrn::default_port, handle_udp);
	dissector_add_uint("tcp.port", wivrn::default_port, handle_tcp);
}

} // namespace
extern "C" WS_DLL_PUBLIC_DEF const char plugin_version[] = "0.11";
extern "C" WS_DLL_PUBLIC_DEF const int plugin_want_major = WIRESHARK_VERSION_MAJOR;
extern "C" WS_DLL_PUBLIC_DEF const int plugin_want_minor = WIRESHARK_VERSION_MINOR;

extern "C" WS_DLL_PUBLIC_DEF void plugin_register(void)
{
	static proto_plugin plug;

	plug.register_protoinfo = proto_register_wivrn;
	plug.register_handoff = proto_reg_handoff_wivrn;
	proto_register_plugin(&plug);
}
