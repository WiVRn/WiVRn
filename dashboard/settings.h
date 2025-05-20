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

#pragma once

#include <QJSValue>
#include <QList>
#include <QObject>
#include <QPointF>
#include <QSizeF>
#include <nlohmann/json.hpp>
#include <qqmlintegration.h>

#define SETTER_GETTER_NOTIFY(type, prop_name)    \
private:                                         \
	type m_##prop_name;                      \
                                                 \
public:                                          \
	const type & prop_name() const           \
	{                                        \
		return m_##prop_name;            \
	}                                        \
	void set_##prop_name(const type & value) \
	{                                        \
		m_##prop_name = value;           \
		prop_name##Changed(value);       \
	}                                        \
Q_SIGNALS:                                       \
	void prop_name##Changed(const type &);

class wivrn_server;

struct encoder
{
	enum class video_codec
	{
		h264,
		h265,
		av1,
	};

	enum class encoder_name
	{
		nvenc,
		vaapi,
		x264,
		vulkan
	};

	std::optional<encoder_name> name;
	double width;
	double height;
	double offset_x;
	double offset_y;
	std::optional<int> group;
	std::optional<video_codec> codec;
	std::map<std::string, std::string> options;
	std::optional<std::string> device;

	double top() const
	{
		return offset_y;
	}
	void set_top(double value)
	{
		height = bottom() - value;
		offset_y = value;
	}

	double bottom() const
	{
		return offset_y + height;
	}
	void set_bottom(double value)
	{
		height = value - top();
	}

	double left() const
	{
		return offset_x;
	}
	void set_left(double value)
	{
		width = right() - value;
		offset_x = value;
	}

	double right() const
	{
		return offset_x + width;
	}
	void set_right(double value)
	{
		width = value - left();
	}
};

class Settings : public QObject
{
	Q_OBJECT
	QML_ELEMENT

	Q_PROPERTY(bool manualEncoders READ manualEncoders WRITE set_manualEncoders NOTIFY manualEncodersChanged)
	Q_PROPERTY(std::vector<encoder> encoders READ encoders WRITE set_encoders NOTIFY encodersChanged)
	Q_PROPERTY(encoder encoderPassthrough READ encoderPassthrough WRITE set_encoderPassthrough NOTIFY encoderPassthroughChanged)
	Q_PROPERTY(int bitrate READ bitrate WRITE set_bitrate NOTIFY bitrateChanged)
	Q_PROPERTY(float scale READ scale WRITE set_scale NOTIFY scaleChanged)
	Q_PROPERTY(QString application READ application WRITE set_application NOTIFY applicationChanged)
	Q_PROPERTY(bool debugGui READ debugGui WRITE set_debugGui NOTIFY debugGuiChanged)
	Q_PROPERTY(bool steamVrLh READ steamVrLh WRITE set_steamVrLh NOTIFY steamVrLhChanged)
	Q_PROPERTY(bool tcpOnly READ tcpOnly WRITE set_tcpOnly NOTIFY tcpOnlyChanged)
	Q_PROPERTY(QString openvr READ openvr WRITE set_openvr NOTIFY openvrChanged)

	Q_PROPERTY(bool flatpak READ flatpak CONSTANT)
	Q_PROPERTY(bool debug_gui_supported READ debug_gui CONSTANT)
	Q_PROPERTY(bool steamvr_lh_supported READ steamvr_lh CONSTANT)

	SETTER_GETTER_NOTIFY(bool, manualEncoders)
	SETTER_GETTER_NOTIFY(std::vector<encoder>, encoders)
	SETTER_GETTER_NOTIFY(encoder, encoderPassthrough)
	SETTER_GETTER_NOTIFY(int, bitrate)
	SETTER_GETTER_NOTIFY(float, scale)
	SETTER_GETTER_NOTIFY(QString, application)
	SETTER_GETTER_NOTIFY(bool, debugGui)
	SETTER_GETTER_NOTIFY(bool, steamVrLh)
	SETTER_GETTER_NOTIFY(bool, tcpOnly)
	SETTER_GETTER_NOTIFY(QString, openvr)

public:
	Settings(QObject * parent = nullptr) :
	        QObject(parent) {}

	Q_INVOKABLE void load(const wivrn_server * server);
	Q_INVOKABLE void save(wivrn_server * server);
	Q_INVOKABLE void restore_defaults();
	Q_INVOKABLE void set_encoder_preset(QJSValue preset);

	bool flatpak() const;
	bool debug_gui() const;
	bool steamvr_lh() const;

	static std::optional<encoder::encoder_name> encoder_id_from_string(std::string_view s);
	static std::optional<encoder::video_codec> codec_id_from_string(std::string_view s);
	static const std::string & encoder_from_id(std::optional<encoder::encoder_name> id);
	static const std::string & codec_from_id(std::optional<encoder::video_codec> id);
};

#undef SETTER_GETTER_NOTIFY
