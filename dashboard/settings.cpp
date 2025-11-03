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
#include <cassert>
#include <fcntl.h>
#include <nlohmann/json.hpp>
#include <qqmlintegration.h>

namespace
{
const std::vector<std::pair<Settings::encoder_name, std::string>> encoder_ids{
        {Settings::encoder_name::Nvenc, "nvenc"},
        {Settings::encoder_name::Vaapi, "vaapi"},
        {Settings::encoder_name::X264, "x264"},
        {Settings::encoder_name::Vulkan, "vulkan"},
};

const std::vector<std::tuple<Settings::video_codec, std::string>> codec_ids{
        {Settings::video_codec::H264, "h264"},
        {Settings::video_codec::H264, "avc"},
        {Settings::video_codec::H265, "h265"},
        {Settings::video_codec::H265, "hevc"},
        {Settings::video_codec::Av1, "av1"},
        {Settings::video_codec::Av1, "AV1"},
};

bool can10bit(Settings::video_codec c)
{
	switch (c)
	{
		case Settings::CodecAuto:
		case Settings::H264:
			return false;
		case Settings::H265:
		case Settings::Av1:
			return true;
	}
	return false;
}
} // namespace

Settings::encoder_name Settings::encoder_id_from_string(std::string_view s)
{
	for (auto & [i, j]: encoder_ids)
	{
		if (j == s)
			return i;
	}
	return Settings::encoder_name::EncoderAuto;
}

Settings::video_codec Settings::codec_id_from_string(std::string_view s)
{
	for (auto & [i, j]: codec_ids)
	{
		if (j == s)
			return i;
	}
	return Settings::video_codec::CodecAuto;
}

const std::string & Settings::encoder_from_id(Settings::encoder_name id)
{
	static const std::string default_value = "auto";

	for (auto & [i, j]: encoder_ids)
	{
		if (i == id)
			return j;
	}

	return default_value;
}

const std::string & Settings::codec_from_id(Settings::video_codec id)
{
	static const std::string default_value = "auto";

	for (auto & [i, j]: codec_ids)
	{
		if (i == id)
			return j;
	}

	return default_value;
}

void Settings::emitAllChanged()
{
	simpleConfigChanged();
	encoderChanged();
	codecChanged();
	tenbitChanged();
	bitrateChanged();
	scaleChanged();
	tcpOnlyChanged();
	applicationChanged();
	openvrChanged();
	debugGuiChanged();
	steamVrLhChanged();
	hidForwardingChanged();
}

void Settings::load(const wivrn_server * server)
{
	// Encoders configuration
	try
	{
		auto conf = server->jsonConfiguration();
		m_jsonSettings = nlohmann::json::parse(conf.toUtf8());
		if (not m_jsonSettings.is_object())
			m_jsonSettings = nlohmann::json::object();
		emitAllChanged();
	}
	catch (std::exception & e)
	{
		qWarning() << "Cannot read configuration: " << e.what();
		m_jsonSettings = nlohmann::json::object();
		emitAllChanged();
		return;
	}
}

bool Settings::simpleConfig() const
{
	// Simple configuration: either unset, or all encoders + codecs are the same
	try
	{
		auto it = m_jsonSettings.find("encoders");
		if (it == m_jsonSettings.end())
			return true;
		std::optional<encoder_name> encoder;
		std::optional<video_codec> codec;
		for (const auto & item: *it)
		{
			if (auto it = item.find("encoder"); it != item.end())
			{
				auto e = encoder_id_from_string(std::string(*it));
				if (encoder and e != encoder)
					return false;
				encoder = e;
			}
			else
			{
				encoder = encoder_name::EncoderAuto;
			}

			if (auto it = item.find("codec"); it != item.end())
			{
				auto e = codec_id_from_string(std::string(*it));
				if (codec and e != codec)
					return false;
				codec = e;
			}
			else
			{
				codec = video_codec::CodecAuto;
			}
		}
		return true;
	}
	catch (...)
	{
		return false;
	}
}

Settings::encoder_name Settings::encoder() const
{
	try
	{
		auto it = m_jsonSettings.find("encoders");
		if (it == m_jsonSettings.end())
			return encoder_name::EncoderAuto;
		for (const auto & item: *it)
		{
			if (auto it = item.find("encoder"); it != item.end())
				return encoder_id_from_string(std::string(*it));
			return encoder_name::EncoderAuto;
		}
	}
	catch (...)
	{
	}
	return encoder_name::EncoderAuto;
}

void Settings::set_encoder(const encoder_name & value)
{
	auto old = encoder();
	auto old_codec = codec();
	switch (value)
	{
		case EncoderAuto:
			m_jsonSettings.erase("encoders");
			break;
		case Nvenc:
		case Vaapi:
		case Vulkan: {
			auto & enc = m_jsonSettings["encoders"] = nlohmann::json::array();
			for (float offset: {0.f, 0.5f})
			{
				auto obj = nlohmann::json::object({
				        {"width", 0.5},
				        {"encoder", encoder_from_id(value)},
				});
				if (offset)
					obj["offset_x"] = offset;
				if (value == Vulkan)
					obj["codec"] = "h264";
				enc.push_back(obj);
			}
		}
		break;
		case encoder_name::X264:
			m_jsonSettings["encoders"] =
			        nlohmann::json::parse(R"([{"encoder": "x264", "codec": "h264"}])");
			break;
	}
	if (value != old)
	{
		encoderChanged();
		if (not can10bit())
			set_tenbit(false);
	}
	if (old_codec != codec())
		codecChanged();
	simpleConfigChanged();
}

Settings::video_codec Settings::codec() const
{
	try
	{
		auto it = m_jsonSettings.find("encoders");
		if (it == m_jsonSettings.end())
			return video_codec::CodecAuto;
		for (const auto & item: *it)
		{
			if (auto it = item.find("codec"); it != item.end())
				return codec_id_from_string(std::string(*it));
			return video_codec::CodecAuto;
		}
	}
	catch (...)
	{
	}
	return video_codec::CodecAuto;
}

void Settings::set_codec(const video_codec & value)
{
	auto old = codec();
	auto it = m_jsonSettings.find("encoders");
	if (it == m_jsonSettings.end() or not it->is_array())
		return;
	for (auto & item: *it)
	{
		if (not item.is_object())
			continue;
		if (value == CodecAuto)
			item.erase("codec");
		else
			item["codec"] = codec_from_id(value);
	}
	if (value != old)
	{
		if (::can10bit(value) != ::can10bit(old))
			set_tenbit(::can10bit(value));
		codecChanged();
	}
}

bool Settings::tenbit() const
{
	try
	{
		return m_jsonSettings.value("bit-depth", 8) == 10;
	}
	catch (...)
	{}
	return false;
}

void Settings::set_tenbit(const bool & value)
{
	auto old = tenbit();
	m_jsonSettings["bit-depth"] = value ? 10 : 8;
	if (value != old)
		tenbitChanged();
}

int Settings::bitrate() const
{
	try
	{
		return m_jsonSettings.value("bitrate", 50'000'000) / 1'000'000;
	}
	catch (...)
	{
		return 50;
	}
}

void Settings::set_bitrate(const int & value)
{
	auto old = bitrate();
	m_jsonSettings["bitrate"] = value * 1'000'000;
	if (old != value)
		bitrateChanged();
}

float Settings::scale() const
{
	// Foveation
	auto it = m_jsonSettings.find("scale");
	if (it != m_jsonSettings.end() and it->is_number())
		return *it;
	return -1;
}

void Settings::set_scale(const float & value)
{
	auto old = scale();
	if (value == -1)
		m_jsonSettings.erase("scale");
	else
		m_jsonSettings["scale"] = value;
	if (old != value)
		scaleChanged();
}

QString Settings::application() const
{
	// Automatically started application
	try
	{
		std::vector<std::string> application;
		auto it = m_jsonSettings.find("application");
		if (it == m_jsonSettings.end())
			return "";
		if (it->is_array())
			application = *it;
		else if (it->is_string())
			application.push_back(*it);

		return escape_string(application);
	}
	catch (...)
	{
		return "";
	}
}

void Settings::set_application(const QString & value)
{
	auto old = application();
	if (value.isEmpty())
		m_jsonSettings.erase("application");
	else
		m_jsonSettings["application"] = unescape_string(value);
	if (old != value)
		applicationChanged();
}

bool Settings::hidForwarding() const
{
	auto it = m_jsonSettings.find("hid-forwarding");
	if (it != m_jsonSettings.end() and it->is_boolean())
		return *it;
	return false;
}

void Settings::set_hidForwarding(const bool & value)
{
	auto old = hidForwarding();
	m_jsonSettings["hid-forwarding"] = value;
	if (old != value)
		hidForwardingChanged();
}

bool Settings::debugGui() const
{
	// Advanced options (debug window, steamvr_lh)
	auto it = m_jsonSettings.find("debug-gui");
	if (it != m_jsonSettings.end() and it->is_boolean())
		return *it;
	return false;
}

void Settings::set_debugGui(const bool & value)
{
	auto old = debugGui();
	m_jsonSettings["debug-gui"] = value;
	if (old != value)
		debugGuiChanged();
}

bool Settings::steamVrLh() const
{
	auto it = m_jsonSettings.find("use-steamvr-lh");
	if (it != m_jsonSettings.end() and it->is_boolean())
		return *it;
	return false;
}

void Settings::set_steamVrLh(const bool & value)
{
	auto old = steamVrLh();
	m_jsonSettings["use-steamvr-lh"] = value;
	if (old != value)
		steamVrLhChanged();
}

bool Settings::tcpOnly() const
{
	auto it = m_jsonSettings.find("tcp-only");
	if (it != m_jsonSettings.end() and it->is_boolean())
		return *it;
	return false;
}

void Settings::set_tcpOnly(const bool & value)
{
	auto old = tcpOnly();
	m_jsonSettings["tcp-only"] = value;
	if (old != value)
		tcpOnlyChanged();
}

QString Settings::openvr() const
{
	// OpenVR compat library
	if (auto it = m_jsonSettings.find("openvr-compat-path"); it != m_jsonSettings.end())
	{
		if (it->is_null())
			return "-";
		if (it->is_string())
			return QString::fromStdString(*it);
	}
	return "";
}

void Settings::set_openvr(const QString & value)
{
	auto old = openvr();
	if (value == "-")
		m_jsonSettings["openvr-compat-path"] = nullptr;
	else if (value == "")
		m_jsonSettings.erase("openvr-compat-path");
	else
		m_jsonSettings["openvr-compat-path"] = value.toStdString();
	if (old != value)
		tcpOnlyChanged();
}

QList<Settings::video_codec> Settings::allowedCodecs() const
{
	switch (encoder())
	{
		case encoder_name::EncoderAuto:
			return {video_codec::CodecAuto};
		case encoder_name::Nvenc:
		case encoder_name::Vaapi:
			return {
			        video_codec::CodecAuto,
			        video_codec::H264,
			        video_codec::H265,
			        video_codec::Av1,
			};
		case encoder_name::Vulkan:
			return {
			        video_codec::CodecAuto,
			        video_codec::H264,
			        video_codec::H265,
			};
		case encoder_name::X264:
			return {
			        video_codec::H264,
			};
	}
	return {video_codec::CodecAuto};
}

bool Settings::can10bit() const
{
	return ::can10bit(codec());
}

void Settings::save(wivrn_server * server)
{
	server->setJsonConfiguration(QString::fromStdString(m_jsonSettings.dump(2)));
}

void Settings::restore_defaults()
{
	m_jsonSettings.erase("encoders");
	m_jsonSettings.erase("encoder-passthrough");
	m_jsonSettings.erase("scale");
	m_jsonSettings.erase("bitrate");
	m_jsonSettings.erase("application");
	m_jsonSettings.erase("hid-forwarding");
	m_jsonSettings.erase("debug-gui");
	m_jsonSettings.erase("use-steamvr-lh");
	m_jsonSettings.erase("tcp-only");
	m_jsonSettings.erase("application");
	emitAllChanged();
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
bool Settings::hid_forwarding() const
{
	// only called from the UI thread → no locking needed
	static std::optional<bool> can_open_uinput;
	if (can_open_uinput.has_value())
		return can_open_uinput.value();

	can_open_uinput = false;
	constexpr std::array paths = {"/dev/uinput", "/dev/input/uinput"};
	for (const char * p: paths)
	{
		int fd = ::open(p, O_WRONLY | O_NONBLOCK);
		if (fd >= 0)
		{
			::close(fd);
			can_open_uinput = true;
			break;
		}
	}
	return can_open_uinput.value();
}

#include "moc_settings.cpp"
