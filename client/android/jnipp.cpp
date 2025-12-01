/*
 * WiVRn VR streaming
 * Copyright (C) 2025  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2025  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "jnipp.h"

namespace jni::details
{
void handle_java_exception()
{
	auto & env = jni_thread::env();
	if (auto exc = env.ExceptionOccurred())
	{
		env.ExceptionClear();

		std::string str = object<"java/lang/Object">{exc}.call<jni::string>("toString");

		throw std::runtime_error("Java exception " + str);
	}
}
} // namespace jni::details
