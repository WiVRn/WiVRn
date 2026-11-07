/*
 * WiVRn VR streaming
 * Copyright (C) 2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2024  Patrick Nicolas <patricknicolas@laposte.net>
 * Copyright (C) 2026  Sapphire <imsapphire0@gmail.com>
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

#include <mutex>
#include <spdlog/spdlog.h>

battery::battery(xr::instance & instance, xr::session & session, jobject activity) : battery(instance, session)
{
	jni::object<""> act(activity);
	auto app = act.call<jni::object<"android/app/Application">>("getApplication");
	auto ctx = app.call<jni::object<"android/content/Context">>("getApplicationContext");

	auto bat_receiver = act.field<jni::object<"android/content/BroadcastReceiver">>("BatteryInfoReceiver");
	ctx.call<jni::object<"android/content/Intent">>("registerReceiver", bat_receiver, jni::new_object<"android/content/IntentFilter">(jni::string{"android.intent.action.BATTERY_CHANGED"}));
}

extern "C" void Java_org_meumeu_wivrn_BroadcastReceiver_onReceive(JNIEnv * env, jobject instance, jobject ctxt_obj, jobject intent_obj)
{
	jni::jni_thread::setup_thread(env);
	jni::object<"android/content/Intent"> intent{intent_obj};

	jni::string level_jstr{"level"};
	jni::string scale_jstr{"scale"};
	jni::string plugged_jstr{"plugged"};
	jni::Int default_jint{-1};

	jmethodID get_int_extra = jni::klass("android/content/Intent").method<jni::Int>("getIntExtra", level_jstr, default_jint);

	auto level_jint = intent.call<jni::Int>(get_int_extra, level_jstr, default_jint);
	auto scale_jint = intent.call<jni::Int>(get_int_extra, scale_jstr, default_jint);
	auto plugged_jint = intent.call<jni::Int>(get_int_extra, plugged_jstr, default_jint);

	std::optional<float> charge{};
	if (level_jint && level_jint.value >= 0 && scale_jint && scale_jint.value >= 0)
		charge = (float)(level_jint.value) / (float)(scale_jint.value);

	bool charging = plugged_jint && plugged_jint.value > 0;

	if (level_jint and scale_jint and plugged_jint)
		spdlog::debug("Received ACTION_BATTERY_CHANGED: level {}%, plugged {}", *charge * 100, charging);

	application::get_battery_monitor().on_status_update(
	        battery::device::head,
	        {
	                .charge = charge,
	                .charging = charging,
	        });
}
