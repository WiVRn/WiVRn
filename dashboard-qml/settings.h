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

#include <QList>
#include <QObject>
#include <nlohmann/json.hpp>
#include <qqmlintegration.h>

class wivrn_server;

class Encoder
{
};

class Settings : public QObject
{
	Q_OBJECT
	QML_NAMED_ELEMENT(Settings2)

	nlohmann::json json_doc;

	Q_PROPERTY(QList<Encoder> encoders READ encoders WRITE set_encoders NOTIFY encodersChanged)
	Q_PROPERTY(Encoder encoderPassthrough READ encoderPassthrough WRITE set_encoderPassthrough NOTIFY encoderPassthroughChanged)
	Q_PROPERTY(int bitrate READ bitrate WRITE set_bitrate NOTIFY bitrateChanged)
	Q_PROPERTY(float scale READ scale WRITE set_scale NOTIFY scaleChanged)
	Q_PROPERTY(QString application READ application WRITE set_application NOTIFY applicationChanged)
	Q_PROPERTY(bool tcpOnly READ tcpOnly WRITE set_tcpOnly NOTIFY tcpOnlyChanged)

#define SETTER_GETTER(type, prop_name)     \
private:                                   \
	type m_##prop_name;                \
                                           \
public:                                    \
	type prop_name() const             \
	{                                  \
		return m_##prop_name;      \
	}                                  \
	void set_##prop_name(type value)   \
	{                                  \
		m_##prop_name = value;     \
		prop_name##Changed(value); \
	}                                  \
Q_SIGNALS:                                 \
	void prop_name##Changed(type);

	SETTER_GETTER(QList<Encoder>, encoders)
	SETTER_GETTER(Encoder, encoderPassthrough)
	SETTER_GETTER(int, bitrate)
	SETTER_GETTER(float, scale)
	SETTER_GETTER(QString, application)
	SETTER_GETTER(bool, tcpOnly)

#undef SETTER_GETTER

public:
	Settings(QObject * parent = nullptr) :
	        QObject(parent) {}

	Q_INVOKABLE void load(const wivrn_server * server);
	Q_INVOKABLE void save(wivrn_server * server);
	Q_INVOKABLE void restore_defaults();
};
