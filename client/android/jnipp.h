/*
 * WiVRn VR streaming
 * Copyright (C) 2023  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2023  Patrick Nicolas <patricknicolas@laposte.net>
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

#include <algorithm>
#include <cassert>
#include <string>
#include <tuple>

#include <jni.h>
#include <type_traits>

namespace jni
{
class jni_thread
{
	JNIEnv * jni_env = nullptr;
	JavaVM * vm = nullptr;

	jni_thread() = default;
	~jni_thread()
	{
		if (vm)
			vm->DetachCurrentThread();
	}

	static jni_thread & instance()
	{
		thread_local jni_thread inst;
		return inst;
	}

public:
	static JNIEnv & env()
	{
		auto res = instance().jni_env;
		assert(res);
		return *res;
	}

	static void setup_thread(JavaVM * vm)
	{
		auto & inst = instance();
		if (inst.vm)
			return;
		inst.vm = vm;
		inst.vm->AttachCurrentThread(&inst.jni_env, nullptr);
	}

	static void setup_thread(JNIEnv * env)
	{
		auto & inst = instance();
		inst.jni_env = env;
	}

	static void detach()
	{
		instance() = jni_thread();
	}
};

struct Void
{
	static std::string type()
	{
		return "V";
	}
	constexpr static const auto call_method = &_JNIEnv::CallVoidMethod;
	constexpr static const auto call_static_method = &_JNIEnv::CallStaticVoidMethod;
};

struct Int
{
	constexpr static auto static_field = &_JNIEnv::GetStaticIntField;
	constexpr static const auto call_method = &_JNIEnv::CallIntMethod;
	constexpr static const auto call_static_method = &_JNIEnv::CallStaticIntMethod;

	static std::string type()
	{
		return "I";
	}

	int value;

	int handle() const
	{
		return value;
	}

	operator int() const
	{
		return value;
	}
};

namespace details
{

struct deleter
{
	void operator()(jobject j)
	{
		jni_thread::env().DeleteGlobalRef(j);
	}
	void operator()(jobjectArray j)
	{
		jni_thread::env().DeleteGlobalRef(j);
	}
};

template <typename... Args>
std::string build_type(Args &&... args)
{
	return (std::string() + ... + std::remove_cvref_t<Args>::type());
}

template <size_t N>
struct string_literal
{
	constexpr string_literal(const char (&str)[N])
	{
		std::copy_n(str, N, value);
	};
	char value[N];
};

inline auto handle()
{
	return std::tuple<>();
}

template <typename T, typename... Args>
auto handle(const T & t, Args &&... args)
{
	return std::tuple_cat(std::make_tuple(t.handle()), handle(std::forward<Args>(args)...));
}

template <typename T>
struct type_map
{
	using type = T;
};

template <>
struct type_map<void>
{
	using type = jni::Void;
};

template <>
struct type_map<int>
{
	using type = jni::Int;
};

template <typename T>
using type_map_t = type_map<T>::type;

void handle_java_exception();

} // namespace details

struct klass
{
	std::unique_ptr<std::remove_pointer_t<jclass>, details::deleter> self;

	operator jclass()
	{
		return self.get();
	}

	template <typename T>
	klass(T & instance)
	{
		auto & env = jni_thread::env();
		self.reset((jclass)env.NewGlobalRef(env.GetObjectClass(instance)));
		assert(self.get());
	}

	klass(const char * name)
	{
		auto & env = jni_thread::env();
		self.reset((jclass)env.NewGlobalRef(env.FindClass(name)));
		assert(self.get());
	}

	template <typename T>
	T field(const std::string & name)
	{
		auto & env = jni_thread::env();
		jfieldID id = env.GetStaticFieldID(*this, name.c_str(), T::type().c_str());
		return T((env.*T::static_field)(*this, id));
	}

	template <typename R, typename... Args>
	auto call(const char * method, Args &&... args)
	{
		using R1 = details::type_map_t<R>;
		auto & env = jni_thread::env();
		std::string signature = "(" + details::build_type(args...) + ")" + R1::type();
		auto method_id = env.GetStaticMethodID(self.get(), method, signature.c_str());
		assert(method_id);
		auto handles = details::handle(std::forward<Args>(args)...);
		return R(std::apply([&](auto &... t) {
			if constexpr (std::is_void_v<R>)
			{
				(env.*R1::call_static_method)(*this, method_id, t...);
				details::handle_java_exception();
			}
			else
			{
				auto res = (env.*R1::call_static_method)(*this, method_id, t...);
				details::handle_java_exception();
				return res;
			}
		},
		                    handles));
	}

	template <typename R, typename... Args>
	auto method(const char * method, Args &&... args)
	{
		using R1 = details::type_map_t<R>;
		auto & env = jni_thread::env();
		std::string signature = "(" + details::build_type(args...) + ")" + R1::type();
		auto method_id = env.GetMethodID(self.get(), method, signature.c_str());
		assert(method_id);
		return method_id;
	}
};

struct Bool
{
	static std::string type()
	{
		return "Z";
	}
	constexpr static const auto call_method = &_JNIEnv::CallBooleanMethod;
	constexpr static const auto call_static_method = &_JNIEnv::CallStaticBooleanMethod;

	bool value;

	int handle() const
	{
		return value;
	}

	operator bool() const
	{
		return value;
	}
};

template <details::string_literal Type>
struct object
{
	static std::string type()
	{
		return std::string("L") + Type.value + ";";
	}
	std::unique_ptr<std::remove_pointer_t<jobject>, details::deleter> self;

	constexpr static const auto call_method = &_JNIEnv::CallObjectMethod;
	constexpr static const auto call_static_method = &_JNIEnv::CallStaticObjectMethod;

	object(jobject o)
	{
		auto & env = jni_thread::env();
		if (o)
			self.reset(env.NewGlobalRef(o));
	}

	template <typename R, typename... Args>
	auto call(const char * method, Args &&... args)
	{
		using R1 = details::type_map_t<R>;
		auto & env = jni_thread::env();
		std::string signature = "(" + details::build_type(args...) + ")" + R1::type();
		auto method_id = env.GetMethodID(klass(), method, signature.c_str());
		assert(method_id);
		auto handles = details::handle(std::forward<Args>(args)...);
		return R(std::apply([&](auto &... t) {
			if constexpr (std::is_void_v<R>)
			{
				(env.*R1::call_method)(*this, method_id, t...);
				details::handle_java_exception();
			}
			else
			{
				auto res = (env.*R1::call_method)(*this, method_id, t...);
				details::handle_java_exception();
				return res;
			}
		},
		                    handles));
	}

	template <typename R, typename... Args>
	auto call(jmethodID method_id, Args &&... args)
	{
		using R1 = details::type_map_t<R>;
		auto & env = jni_thread::env();
		assert(method_id);
		auto handles = details::handle(std::forward<Args>(args)...);
		return R(std::apply([&](auto &... t) {
			if constexpr (std::is_void_v<R>)
			{
				(env.*R1::call_method)(*this, method_id, t...);
				details::handle_java_exception();
			}
			else
			{
				auto res = (env.*R1::call_method)(*this, method_id, t...);
				details::handle_java_exception();
				return res;
			}
		},
		                    handles));
	}

	jobject handle() const
	{
		return *this;
	}

	operator jobject() const
	{
		return self.get();
	}

	klass klass()
	{
		return *this;
	}
};

template <details::string_literal Type, typename... Args>
static object<Type> new_object(Args &&... args)
{
	auto & env = jni_thread::env();
	auto klass = jni::klass(Type.value);
	std::string signature = "(" + details::build_type(args...) + ")V";
	auto method_id = env.GetMethodID(klass, "<init>", signature.c_str());
	assert(method_id);
	auto handles = details::handle(std::forward<Args>(args)...);
	return object<Type>(std::apply([&](auto &... t) {
		auto res = env.NewObject(klass, method_id, t...);
		details::handle_java_exception();
		return res;
	},
	                               handles));
}

using string_t = object<"java/lang/String">;

struct string : public string_t
{
	using string_t::type;
	constexpr static auto static_field = &_JNIEnv::GetStaticObjectField;

	operator jstring()
	{
		return (jstring)self.get();
	}

	string(const char * str) :
	        string_t(jni_thread::env().NewStringUTF(str)) {}
	string(jobject j) :
	        string_t(j) {}

	operator std::string()
	{
		auto & env = jni_thread::env();
		const char * dataString_c = env.GetStringUTFChars(*this, NULL);
		std::string res = dataString_c;
		env.ReleaseStringUTFChars(*this, dataString_c);
		return res;
	}
};

template <typename T>
struct array
{
	static std::string type()
	{
		return std::string("[") + T::type();
	}

	std::unique_ptr<std::remove_pointer_t<jobjectArray>, details::deleter> self;

	auto handle() const
	{
		return self.get();
	}

	array(T & element)
	{
		auto & env = jni_thread::env();
		self.reset((jobjectArray)env.NewGlobalRef(env.NewObjectArray(1, element.klass(), element)));
	}
};

} // namespace jni
