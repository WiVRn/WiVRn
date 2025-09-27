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

#include "file_picker.h"

#include "application.h"
#include "utils/mapped_file.h"
#include <exception>

#ifdef __ANDROID__
#include "android/intent.h"
#endif

std::future<file_picker_result> file_picker::open()
{
	// Put it in a shared_ptr because clang does not support std::move_only_function
	auto promise = std::make_shared<std::promise<file_picker_result>>();
	auto future = promise->get_future();

#ifdef __ANDROID__
	// https://developer.android.com/training/data-storage/shared/documents-files#open
	// https://developer.android.com/reference/android/content/Intent#ACTION_OPEN_DOCUMENT
	// https://developer.android.com/guide/components/intents-common#OpenFile

	intent open_doc{"android.intent.action.OPEN_DOCUMENT"}; // Intent.ACTION_OPEN_DOCUMENT
	open_doc.set_type("*/*");
	open_doc.add_category("android.intent.category.OPENABLE"); // Intent.CATEGORY_OPENABLE
	open_doc.start([promise](int result_code, intent && data) mutable {
		try
		{
			if (result_code != -1 /* Activity.RESULT_OK */)
				return;

			auto uri = data.get_uri();
			auto uri_string = (std::string)uri.call<jni::string>("toString");

			jni::object<""> activity(application::native_app()->activity->clazz);
			auto content_resolver = activity.call<jni::object<"android/content/ContentResolver">>("getContentResolver");

			content_resolver.call<void>("takePersistableUriPermission", uri, jni::Int{1} /* Intent.FLAG_GRANT_READ_URI_PERMISSION */);

			auto pfd = content_resolver.call<jni::object<"android/os/ParcelFileDescriptor">>("openFileDescriptor", uri, jni::string{"r"});
			int fd = pfd.call<int>("getFd");

			// The file descriptor is owned by the ParcelFileDescriptor, dup it before
			// passing it to mapped_file to ensure the original fd is not closed
			promise->set_value(file_picker_result{
			        .path = uri_string,
			        .file = utils::mapped_file{fd},
			});
		}
		catch (...)
		{
			promise->set_exception(std::current_exception());
		}
	});
#else
	// TODO
#endif
	return future;
}

void file_picker::display()
{
#ifdef __ANDROID__
	// No-op on Android, the file picker is displayed by the OS
#else
	// TODO
#endif
}
