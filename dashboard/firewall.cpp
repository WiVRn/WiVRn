/*
 * WiVRn VR streaming
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

#include "firewall.h"

#include "dashboard_utils.h"
#include "escape_sandbox.h"
#include "utils/flatpak.h"
#include "wivrn_config.h"

#include <QDBusInterface>
#include <QDBusMetaType>
#include <QDBusReply>
#include <QDebug>
#include <QProcess>
#include <filesystem>
#include <string>

class firewall::Impl : public QObject
{
	Q_OBJECT
public:
	virtual bool need_setup()
	{
		return false;
	}
	virtual void do_setup() {}
Q_SIGNALS:
	void needSetupChanged(bool);
};

class ufw : public firewall::Impl
{
	std::unique_ptr<QProcess> pkexec;
	static const std::filesystem::path conf;

public:
	bool need_setup() override
	{
		return not std::filesystem::exists(
		        (wivrn::is_flatpak() ? "/run/host" : "/") / conf);
	}

	void do_setup() override
	{
		pkexec = escape_sandbox("pkexec",
		                        "sh",
		                        "-c",
		                        "printf '[WiVRn]\\ntitle=WiVRn server\ndescription=WiVRn OpenXR streaming server\nports=" + std::to_string(wivrn::default_port) + "\n' > /" + conf.string() + " && ufw allow wivrn");
		pkexec->setProcessChannelMode(QProcess::MergedChannels);
		pkexec->start();

		QObject::connect(pkexec.get(), &QProcess::finished, this, [this](int exit_code, QProcess::ExitStatus exit_status) {
			if (exit_status != QProcess::NormalExit or exit_code)
				qWarning() << "ufw configuration exited with status" << exit_status << "and code" << exit_code;
			needSetupChanged(need_setup());
			pkexec.release()->deleteLater();
		});
	}
};

const std::filesystem::path ufw::conf = "etc/ufw/applications.d/wivrn";

class firewalld : public firewall::Impl
{
	QDBusInterface fw = QDBusInterface("org.fedoraproject.FirewallD1", "/org/fedoraproject/FirewallD1", "org.fedoraproject.FirewallD1", QDBusConnection::systemBus());
	QDBusInterface zone = QDBusInterface("org.fedoraproject.FirewallD1", "/org/fedoraproject/FirewallD1", "org.fedoraproject.FirewallD1.zone", QDBusConnection::systemBus());
	QDBusInterface conf = QDBusInterface("org.fedoraproject.FirewallD1", "/org/fedoraproject/FirewallD1/config", "org.fedoraproject.FirewallD1.config", QDBusConnection::systemBus());

public:
	bool enabled()
	{
		return fw.isValid();
	}

	bool need_setup() override
	{
		QDBusReply<QStringList> res = zone.call("getServices", "");
		if (not res.isValid())
		{
			qWarning() << "Failed to list enabled firewalld services" << res.error();
			return false;
		}
		return not res.value().contains("wivrn");
	}

	void do_setup() override
	{
		if (QDBusReply<void> res = fw.call("authorizeAll"); not res.isValid())
		{
			qWarning() << "Failed to get firewalld authorization: " << res.error();
			return;
		}
		// Check if configuration needs to be created
		QDBusReply<QStringList> res = conf.call("getServiceNames");
		if (not res.isValid())
		{
			qWarning() << "Failed to list firewalld services: " << res.error();
			return;
		}
		if (not res.value().contains("wivrn"))
		{
			qInfo() << "Creating firewalld wivrn service";
			QMap<QString, QVariant> map;

			map["short"] = QString("WiVRn");
			map["description"] = QString("OpenXR streaming service");
			{
				QList<QVariant> ports;
				for (auto proto: {"tcp", "udp"})
				{
					QDBusArgument port;
					port.beginStructure();
					port << QString::number(wivrn::default_port) << QString(proto);
					port.endStructure();
					ports.push_back(QVariant::fromValue(port));
				}
				map["ports"] = ports;
			}
			if (QDBusReply<void> res = conf.call("addService2", "wivrn", map);
			    not res.isValid())
			{
				qWarning() << "Failed to create firewalld wivrn service: " << res.error();
				return;
			}
		}
		QDBusReply<QString> default_zone = fw.call("getDefaultZone");
		if (not default_zone.isValid())
		{
			qWarning() << "Failed to get firewalld default zone: " << default_zone.error();
			return;
		}

		QDBusReply<QDBusObjectPath> zone_path = conf.call("getZoneByName", default_zone.value());
		if (not zone_path.isValid())
		{
			qWarning() << "Failed to get firewalld zone " << default_zone.value() << " configuration: " << default_zone.error();
			return;
		}

		QDBusInterface zone_conf = QDBusInterface("org.fedoraproject.FirewallD1", zone_path.value().path(), "org.fedoraproject.FirewallD1.config.zone", QDBusConnection::systemBus());

		if (QDBusReply<void> res = zone_conf.call("addService", "wivrn"); not res.isValid())
		{
			qWarning() << "Failed to enable firewalld wivrn service: " << res.error();
			return;
		}

		if (QDBusReply<void> res = fw.call("reload"); not res.isValid())
			qWarning() << "Failed to reload firewalld configuration: " << res.error();
		needSetupChanged(false);
	}
};

std::unique_ptr<firewall::Impl> make_impl()
{
	if (auto fwd = std::make_unique<firewalld>(); fwd->enabled())
		return fwd;

	if (not find_executable("ufw").isEmpty())
		return std::make_unique<ufw>();

	return std::make_unique<firewall::Impl>();
}

firewall::firewall() :
        impl(make_impl())
{
	connect(impl.get(), &firewall::Impl::needSetupChanged, this, &firewall::needSetupChanged);
}

firewall::~firewall() = default;

bool firewall::needSetup()
{
	return impl->need_setup();
}

void firewall::doSetup()
{
	return impl->do_setup();
}

#include "firewall.moc"
