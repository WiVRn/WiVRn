/*
 * WiVRn VR streaming
 * Copyright (C) 2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
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

#include "settings.h"

#include "escape_string.h"
#include "gui_config.h"
#include "utils/flatpak.h"
#include "wivrn_server.h"
#include <QList>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <cassert>
#include <nlohmann/json.hpp>
#include <qqmlintegration.h>

namespace
{
const std::vector<std::pair<encoder::encoder_name, std::string>> encoder_ids{
        {encoder::encoder_name::nvenc, "nvenc"},
        {encoder::encoder_name::vaapi, "vaapi"},
        {encoder::encoder_name::x264, "x264"},
        {encoder::encoder_name::vulkan, "vulkan"},
};

const std::vector<std::pair<encoder::video_codec, std::string>> codec_ids{
        {encoder::video_codec::h264, "h264"},
        {encoder::video_codec::h264, "avc"},
        {encoder::video_codec::h265, "h265"},
        {encoder::video_codec::h265, "hevc"},
        {encoder::video_codec::av1, "av1"},
        {encoder::video_codec::av1, "AV1"},
};
} // namespace

std::optional<encoder::encoder_name> Settings::encoder_id_from_string(std::string_view s)
{
	for (auto & [i, j]: encoder_ids)
	{
		if (j == s)
			return i;
	}
	return std::nullopt;
}

std::optional<encoder::video_codec> Settings::codec_id_from_string(std::string_view s)
{
	for (auto & [i, j]: codec_ids)
	{
		if (j == s)
			return i;
	}
	return std::nullopt;
}

const std::string & Settings::encoder_from_id(std::optional<encoder::encoder_name> id)
{
	static const std::string default_value = "auto";
	if (not id)
		return default_value;

	for (auto & [i, j]: encoder_ids)
	{
		if (i == *id)
			return j;
	}

	return default_value;
}

const std::string & Settings::codec_from_id(std::optional<encoder::video_codec> id)
{
	static const std::string default_value = "auto";
	if (not id)
		return default_value;

	for (auto & [i, j]: codec_ids)
	{
		if (i == *id)
			return j;
	}

	return default_value;
}

void Settings::load(const wivrn_server * server)
{
	// Encoders configuration
	nlohmann::json json_doc;
	try
	{
		auto conf = server->jsonConfiguration();
		json_doc = nlohmann::json::parse(conf.toUtf8());
	}
	catch (std::exception & e)
	{
		qWarning() << "Cannot read configuration: " << e.what();
		restore_defaults();
		return;
	}

	std::vector<encoder> new_encoders;
	try
	{
		nlohmann::json json_encoders;

		if (json_doc.contains("encoders"))
		{
			json_encoders = json_doc["encoders"];
			set_manualEncoders(true);
		}
		else if (json_doc.contains("encoders.disabled"))
		{
			json_encoders = json_doc["encoders.disabled"];
			set_manualEncoders(false);
		}
		else
		{
			set_manualEncoders(false);
		}

		for (auto & i: json_encoders)
		{
			encoder enc;
			enc.name = encoder_id_from_string(i.value("encoder", "auto"));
			enc.codec = codec_id_from_string(i.value("codec", "auto"));
			enc.width = i.value("width", 1.0);
			enc.height = i.value("height", 1.0);
			enc.offset_x = i.value("offset_x", 0.0);
			enc.offset_y = i.value("offset_y", 0.0);
			enc.group = i.value("group", 0); // TODO: handle groups
			new_encoders.push_back(enc);
		}
	}
	catch (...)
	{
		// QMessageBox msgbox{QMessageBox::Information, tr("Invalid settings"), tr("The encoder configuration is invalid, the default values will be restored."), QMessageBox::Close, this};
		// msgbox.exec();

		new_encoders.clear();
	}
	set_encoders(new_encoders);

	if (encoders().empty())
	{
		set_manualEncoders(false);
		std::vector<encoder> new_encoders;
		new_encoders.push_back(encoder{
		        .width = 1,
		        .height = 1,
		        .offset_x = 0,
		        .offset_y = 0,
		});
		set_encoders(new_encoders);
	}

	// Foveation
	try
	{
		if (json_doc.contains("scale"))
		{
			set_scale(json_doc.value("scale", 1.0));
		}
		else
		{
			set_scale(-1);
		}
	}
	catch (...)
	{
		set_scale(-1);
	}

	// Bitrate
	try
	{
		set_bitrate(json_doc.value("bitrate", 50'000'000));
	}
	catch (...)
	{
		set_bitrate(50'000'000);
	}

	// Automatically started application
	std::vector<std::string> application;
	try
	{
		if (json_doc["application"].is_array())
			application = json_doc.value<std::vector<std::string>>("application", {});
		else if (json_doc["application"].is_string())
			application.push_back(json_doc["application"]);

		set_application(escape_string(application));
	}
	catch (...)
	{
		set_application("");
	}

	// Advanced options (debug window, steamvr_lh)
	try
	{
		set_debugGui(json_doc.value("debug-gui", false));
	}
	catch (...)
	{
		set_debugGui(false);
	}

	try
	{
		set_steamVrLh(json_doc.value("use-steamvr-lh", false));
	}
	catch (...)
	{
		set_steamVrLh(false);
	}

	// OpenVR compat library
	try
	{
		if (auto it = json_doc.find("openvr-compat-path"); it != json_doc.end())
		{
			if (it->is_null())
				set_openvr("-");
			else
				set_openvr(QString::fromStdString(*it));
		}
	}
	catch (...)
	{
		set_openvr("");
	}
}

void Settings::save(wivrn_server * server)
{
	nlohmann::json json_doc;
	try
	{
		auto conf = server->jsonConfiguration();
		json_doc = nlohmann::json::parse(conf.toUtf8());
	}
	catch (...)
	{
	}

	// Remove all optional keys that might not be overwritten
	auto it = json_doc.find("scale");
	if (it != json_doc.end())
		json_doc.erase(it);

	it = json_doc.find("encoders.disabled");
	if (it != json_doc.end())
		json_doc.erase(it);

	it = json_doc.find("encoders");
	if (it != json_doc.end())
		json_doc.erase(it);

	it = json_doc.find("application");
	if (it != json_doc.end())
		json_doc.erase(it);

	if (scale() > 0)
		json_doc["scale"] = scale();

	json_doc["bitrate"] = bitrate();

	std::vector<nlohmann::json> encoders;

	for (auto & enc: m_encoders)
	{
		nlohmann::json encoder;

		if (auto value = encoder_from_id(enc.name); value != "auto")
			encoder["encoder"] = value;
		if (auto value = codec_from_id(enc.codec); value != "auto")
			encoder["codec"] = value;
		encoder["width"] = enc.width;
		encoder["height"] = enc.height;
		encoder["offset_x"] = enc.offset_x;
		encoder["offset_y"] = enc.offset_y;

		// TODO encoder groups

		encoders.push_back(encoder);
	}

	std::ranges::stable_sort(encoders, [](const nlohmann::json & i, const nlohmann::json & j) {
		double size_i = i.value("width", 0.0) * i.value("height", 0.0);
		double size_j = j.value("width", 0.0) * j.value("height", 0.0);
		return size_i < size_j;
	});

	if (manualEncoders())
		json_doc["encoders"] = encoders;
	else
		json_doc["encoders.disabled"] = encoders;

	if (application() != "")
		json_doc["application"] = unescape_string(application());

	json_doc["debug-gui"] = debugGui();
	json_doc["use-steamvr-lh"] = steamVrLh();

	if (openvr() == "-")
		json_doc["openvr-compat-path"] = nullptr;
	else if (openvr() == "")
		json_doc.erase("openvr-compat-path");
	else
		json_doc["openvr-compat-path"] = openvr().toStdString();

	server->setJsonConfiguration(QString::fromStdString(json_doc.dump(2)));
}

void Settings::restore_defaults()
{
	set_encoders({});
	set_encoderPassthrough({});
	set_bitrate(50'000'000);
	set_scale(-1);
	set_application("");
	set_debugGui(false);
	set_steamVrLh(false);
	set_tcpOnly(false);
}

void Settings::set_encoder_preset(QJSValue preset)
{
	QJSEngine engine;
	const QJsonArray json_encoders = engine.fromScriptValue<QJsonValue>(preset).toArray();

	std::vector<encoder> encoders;
	for (const QJsonValue & json_encoder_value: json_encoders)
	{
		QJsonObject json_encoder = json_encoder_value.toObject();

		encoders.push_back(encoder{
		        .name = encoder_id_from_string(json_encoder["encoder"].toString("auto").toStdString()),
		        .width = json_encoder["width"].toDouble(),
		        .height = json_encoder["height"].toDouble(),
		        .offset_x = json_encoder["offset_x"].toDouble(),
		        .offset_y = json_encoder["offset_y"].toDouble(),
		        .codec = codec_id_from_string(json_encoder["codec"].toString("auto").toStdString()),
		});
	}

	set_manualEncoders(true);
	set_encoders(encoders);
}

bool Settings::flatpak() const
{
	return wivrn::is_flatpak();
}

bool Settings::debug_gui() const
{
#if WIVRN_FEATURE_DEBUG_GUI
	return true;
#else
	return false;
#endif
}
bool Settings::steamvr_lh() const
{
#if WIVRN_FEATURE_STEAMVR_LIGHTHOUSE
	return true;
#else
	return false;
#endif
}

#include "moc_settings.cpp"
