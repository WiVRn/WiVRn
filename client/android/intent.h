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

#pragma once

#include "jnipp.h"
#include <functional>

class intent
{
	jni::object<"android/content/Intent"> obj;

public:
	intent(const char * intent_type);
	intent(jobject);

	void set_type(const char * type);
	void add_category(const char * category);

	void start(std::function<void(int, intent &&)> callback);

	jni::object<"android/net/Uri"> get_uri();
};
