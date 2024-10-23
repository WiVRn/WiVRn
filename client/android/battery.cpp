/*
 * WiVRn VR streaming
 * Copyright (C) 2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2024  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "battery.h"
#include "android/jnipp.h"
#include "application.h"

#include <chrono>

using Application = jni::object<"android/app/Application">;
using BroadcastReceiver = jni::object<"android/content/BroadcastReceiver">;
using Context = jni::object<"android/content/Context">;
using Intent = jni::object<"android/content/Intent">;
using IntentFilter = jni::object<"android/content/IntentFilter">;
using Object = jni::object<"">;

struct battery::pimpl
{
	Object act{application::native_app()->activity->clazz};
	Application app = act.call<Application>("getApplication");
	Context ctx = app.call<Context>("getApplicationContext");

	jni::string filter_jstr{"android.intent.action.BATTERY_CHANGED"};
	jni::string level_jstr{"level"};
	jni::string scale_jstr{"scale"};
	jni::string plugged_jstr{"plugged"};
	jni::Int default_jint{-1};

	BroadcastReceiver receiver = BroadcastReceiver{nullptr};
	IntentFilter filter = jni::new_object<"android/content/IntentFilter">(filter_jstr);

	jmethodID register_receiver = jni::klass("android/content/Context").method<Intent>("registerReceiver", receiver, filter);

	jmethodID get_int_extra = jni::klass("android/content/Intent").method<jni::Int>("getIntExtra", level_jstr, default_jint);
};

battery::battery()
{
	application::instance().setup_jni();

	p = std::make_unique<pimpl>();
}

battery::~battery() {}

battery::status battery::get()
{
	if (auto now = std::chrono::steady_clock::now(); now > next_battery_check)
	{
		next_battery_check = now + battery_check_interval;

		auto intent = p->ctx.call<Intent>(p->register_receiver, p->receiver, p->filter);
		if (intent)
		{
			auto level_jint = intent.call<jni::Int>(p->get_int_extra, p->level_jstr, p->default_jint);
			auto scale_jint = intent.call<jni::Int>(p->get_int_extra, p->scale_jstr, p->default_jint);

			if (level_jint && level_jint.value >= 0 && scale_jint && scale_jint.value >= 0)
				last_status.charge = (float)(level_jint.value) / (float)(scale_jint.value);
			else
				last_status.charge = std::nullopt;

			auto plugged_jint = intent.call<jni::Int>(p->get_int_extra, p->plugged_jstr, p->default_jint);
			last_status.charging = plugged_jint && plugged_jint.value > 0;
		}

		auto duration = std::chrono::steady_clock::now() - now;
		spdlog::info("Battery check took: {} µs", std::chrono::duration_cast<std::chrono::microseconds>(duration).count());
	}

	return last_status;
}
