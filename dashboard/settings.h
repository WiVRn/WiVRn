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

#define SETTER_GETTER_NOTIFY(type, prop_name)     \
public:                                           \
	type prop_name() const;                   \
	void set_##prop_name(const type & value); \
Q_SIGNALS:                                        \
	void prop_name##Changed();

class wivrn_server;

class Settings : public QObject
{
	Q_OBJECT
	QML_NAMED_ELEMENT(Settings)
	QML_SINGLETON
public:
	enum encoder_name
	{
		EncoderAuto,
		Nvenc,
		Vaapi,
		X264,
		Vulkan,
	};
	Q_ENUM(encoder_name)

	enum video_codec
	{
		CodecAuto,
		H264,
		H265,
		Av1,
	};
	Q_ENUM(video_codec)

	Q_PROPERTY(bool simpleConfig READ simpleConfig NOTIFY simpleConfigChanged)
	Q_PROPERTY(encoder_name encoder READ encoder WRITE set_encoder NOTIFY encoderChanged)
	Q_PROPERTY(video_codec codec READ codec WRITE set_codec NOTIFY codecChanged)
	Q_PROPERTY(QList<video_codec> allowedCodecs READ allowedCodecs NOTIFY encoderChanged)
	Q_PROPERTY(bool can10bit READ can10bit NOTIFY codecChanged)
	Q_PROPERTY(bool tenbit READ tenbit WRITE set_tenbit NOTIFY tenbitChanged)

	Q_PROPERTY(bool tcpOnly READ tcpOnly WRITE set_tcpOnly NOTIFY tcpOnlyChanged)
	Q_PROPERTY(QString application READ application WRITE set_application NOTIFY applicationChanged)
	Q_PROPERTY(QString openvr READ openvr WRITE set_openvr NOTIFY openvrChanged)

	Q_PROPERTY(bool hidForwarding READ hidForwarding WRITE set_hidForwarding NOTIFY hidForwardingChanged)
	Q_PROPERTY(bool debugGui READ debugGui WRITE set_debugGui NOTIFY debugGuiChanged)
	Q_PROPERTY(bool steamVrLh READ steamVrLh WRITE set_steamVrLh NOTIFY steamVrLhChanged)

	Q_PROPERTY(bool flatpak READ flatpak CONSTANT)
	Q_PROPERTY(bool hid_forwarding_supported READ hid_forwarding CONSTANT)
	Q_PROPERTY(bool debug_gui_supported READ debug_gui CONSTANT)
	Q_PROPERTY(bool steamvr_lh_supported READ steamvr_lh CONSTANT)

	SETTER_GETTER_NOTIFY(bool, simpleConfig)
	SETTER_GETTER_NOTIFY(encoder_name, encoder)
	SETTER_GETTER_NOTIFY(video_codec, codec)
	SETTER_GETTER_NOTIFY(bool, tenbit)
	SETTER_GETTER_NOTIFY(QString, application)
	SETTER_GETTER_NOTIFY(bool, hidForwarding)
	SETTER_GETTER_NOTIFY(bool, debugGui)
	SETTER_GETTER_NOTIFY(bool, steamVrLh)
	SETTER_GETTER_NOTIFY(bool, tcpOnly)
	SETTER_GETTER_NOTIFY(QString, openvr)
private:
	nlohmann::json m_jsonSettings = nlohmann::json::object();
	nlohmann::json m_originalSettings;
	void emitAllChanged();

public:
	Settings(QObject * parent = nullptr) :
	        QObject(parent) {}

	Q_INVOKABLE void load(const wivrn_server * server);
	Q_INVOKABLE void save(wivrn_server * server);
	Q_INVOKABLE void restore_defaults();

	QList<video_codec> allowedCodecs() const;
	bool can10bit() const;

	bool flatpak() const;
	bool debug_gui() const;
	bool steamvr_lh() const;
	bool hid_forwarding() const;

	static encoder_name encoder_id_from_string(std::string_view s);
	static video_codec codec_id_from_string(std::string_view s);
	static const std::string & encoder_from_id(encoder_name id);
	static const std::string & codec_from_id(video_codec id);

Q_SIGNALS:
	void settingsChanged();
};

#undef SETTER_GETTER_NOTIFY
