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

#include "apk_installer.h"
#include "escape_sandbox.h"
#include "version.h"
#include <KLocalization>
#include <QCoroNetworkReply>
#include <QCoroProcess>
#include <nlohmann/json.hpp>
#include <regex>

using namespace std::chrono_literals;

apk_installer::apk_installer()
{
	if (isTagged())
		m_apkFile.setFileName(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/wivrn-" + QString::fromStdString(wivrn::git_version) + ".apk");
	else
		m_apkFile.setFileName(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/wivrn-" + QString::fromStdString(wivrn::git_commit) + ".apk");
}

bool apk_installer::isTagged() const
{
	return not std::regex_match(wivrn::git_version, std::regex(".*-g[0-9a-f]+"));
}

QString apk_installer::currentVersion() const
{
	return wivrn::display_version();
}

QCoro::Task<> apk_installer::doRefreshLatestVersion()
{
	assert(not m_busy);
	m_apkUrl = QUrl{};

	busyChanged(m_busy = true);
	latestVersionChanged(m_latestVersion = "");

	std::unique_ptr<QNetworkReply> reply{co_await manager.get(QNetworkRequest{QUrl{"https://api.github.com/repos/WiVRn/WiVRn/releases/latest"}})};

	if (auto error = reply->error(); error != QNetworkReply::NetworkError::NoError)
	{
		qWarning() << "Cannot get version information from https://api.github.com/repos/WiVRn/WiVRn/releases/latest:" << error;
	}
	else
	{
		auto json = nlohmann::json::parse(reply->readAll());
		m_latestVersion = QString::fromStdString(json["tag_name"]);
		if (m_latestVersion.startsWith('v'))
			m_latestVersion = m_latestVersion.mid(1);

		latestVersionChanged(m_latestVersion);
		qDebug() << "Latest version is" << m_latestVersion;

		qDebug() << "Getting release metadata";
	}

	QUrl metadata_url;
	if (isTagged())
		metadata_url = QString{"https://api.github.com/repos/WiVRn/WiVRn/releases/tags/"} + wivrn::git_version;
	else
		metadata_url = QString{"https://api.github.com/repos/WiVRn/WiVRn-APK/releases/tags/apk-"} + wivrn::git_commit;

	reply.reset(co_await manager.get(QNetworkRequest{metadata_url}));

	if (auto error = reply->error(); error != QNetworkReply::NoError)
	{
		qWarning() << "Cannot get release metadata from" << metadata_url.toString() << ":" << error;
	}
	else
	{
		auto json = nlohmann::json::parse(reply->readAll());
		for (auto & i: json["assets"])
		{
			std::string name = i["name"];

			if (name.ends_with("-standard-release.apk"))
			{
				m_apkUrl = QString::fromStdString(i["browser_download_url"]);
				break;
			}
		}
	}

	if (apkAvailable())
	{
		apkAvailableChanged(true);
		qDebug() << "APK URL is" << m_apkUrl.toString();
	}
	else
	{
		apkAvailableChanged(false);
		qDebug() << "No APK is available for this version";
	}

	busyChanged(m_busy = false);
}

QCoro::Task<> apk_installer::doInstallApk(QString serial)
{
	assert(not m_apkFile.isOpen());
	assert(not m_busy);
	assert(m_apkUrl != QUrl{});

	busyChanged(m_busy = true);

	if (QFile::exists(m_apkFile.fileName()))
	{
		qDebug() << "Already downloaded";
	}
	else
	{
		cancellableChanged(m_cancellable = true);

		auto apk_dir = std::filesystem::path(m_apkFile.fileName().toStdString()).parent_path();
		std::filesystem::create_directories(apk_dir);
		if (not m_apkFile.open(QIODeviceBase::WriteOnly | QIODeviceBase::Truncate))
		{
			qDebug() << "Cannot save APK file " << m_apkFile.fileName() << ": " << m_apkFile.errorString();
			installStatusChanged(m_installStatus = i18n("Cannot save APK file: %1", m_apkFile.errorString()));
			busyChanged(m_busy = false);
			co_return;
		}

		qDebug() << "Downloading from" << m_apkUrl.toString() << "to" << m_apkFile.fileName();

		std::unique_ptr<QNetworkReply> reply{manager.get(QNetworkRequest{m_apkUrl})};

		auto co_reply = qCoro(*reply);
		int64_t size_total = -1;

		m_bytesReceived = 0;
		m_bytesTotal = -1;
		bytesReceivedChanged(m_bytesReceived);
		bytesTotalChanged(m_bytesTotal);

		while ((reply->bytesAvailable() > 0 or not reply->isFinished()) and m_apkFile.isOpen())
		{
			m_apkFile.write(co_await co_reply.read(100'000, 250ms));

			if (QVariant content_length = reply->header(QNetworkRequest::KnownHeaders::ContentLengthHeader); content_length.isValid())
			{
				int64_t new_total_size = content_length.toLongLong();
				if (size_total != new_total_size)
					size_total = new_total_size;
			}

			m_bytesReceived = m_apkFile.size();
			m_bytesTotal = size_total;
			bytesReceivedChanged(m_bytesReceived);
			bytesTotalChanged(m_bytesTotal);

			if (m_bytesTotal > 0)
				installStatusChanged(m_installStatus = ki18n("Downloading APK: %1 MB / %2 MB").subs(m_bytesReceived / 1'000'000., 0, 'f', 1).subs(m_bytesTotal / 1'000'000., 0, 'f', 1).toString());
			else
				installStatusChanged(m_installStatus = ki18n("Downloading APK: %1 MB").subs(m_bytesReceived / 1'000'000., 0, 'f', 1).toString());
		};

		if (not m_apkFile.isOpen())
		{
			qDebug() << "Download cancelled";
			m_bytesReceived = 0;
			m_bytesTotal = -1;

			installStatusChanged(m_installStatus = i18n("Download cancelled"));
			bytesReceivedChanged(m_bytesReceived);
			bytesTotalChanged(m_bytesTotal);
			busyChanged(m_busy = false);
			co_return;
		}
		else if (auto error = reply->error(); error != QNetworkReply::NoError)
		{
			qDebug() << "Cannot download APK" << error;

			// Cancel and commit to close without saving the already written data, so that it can be reopened
			m_apkFile.cancelWriting();
			m_apkFile.commit();
			m_bytesReceived = 0;
			m_bytesTotal = -1;

			installStatusChanged(m_installStatus = i18n("Cannot download APK: %1", reply->errorString()));
			bytesReceivedChanged(m_bytesReceived);
			bytesTotalChanged(m_bytesTotal);
			busyChanged(m_busy = false);
			co_return;
		}
		else
		{
			qDebug() << "Download successful";
			auto last_modified = reply->header(QNetworkRequest::LastModifiedHeader).value<QDateTime>();
			m_apkFile.setFileTime(last_modified, QFileDevice::FileModificationTime);
			m_apkFile.commit();
		}
	}

	cancellableChanged(m_cancellable = false);
	bytesTotalChanged(m_bytesTotal = -1); // Make the progress bar indeterminate

	auto adb_install = escape_sandbox("adb", "-s", serial, "install", "-r", m_apkFile.fileName());
	adb_install->setProcessChannelMode(QProcess::ForwardedChannels);

	auto co_adb_install = qCoro(*adb_install);

	installStatusChanged(m_installStatus = i18n("Installing APK"));
	if (not co_await co_adb_install.start())
	{
		qDebug() << "adb failed to start:" << adb_install->error();
		installStatusChanged(m_installStatus = i18n("adb failed to start: %1", adb_install->error()));
		busyChanged(m_busy = false);
		co_return;
	}

	co_await co_adb_install.waitForFinished();

	if (adb_install->exitStatus() != QProcess::NormalExit)
	{
		qDebug() << "adb exited abnormally" << adb_install->error();
		installStatusChanged(m_installStatus = i18n("adb exited abnormally: %1", adb_install->error()));
		busyChanged(m_busy = false);
		co_return;
	}

	if (adb_install->exitCode() != 0)
	{
		qDebug() << "The 'adb install' command failed: exit code" << adb_install->exitCode();
		installStatusChanged(m_installStatus = i18n("The 'adb install' command failed: exit code %1", adb_install->exitCode()));
		busyChanged(m_busy = false);
		co_return;
	}

	installStatusChanged(m_installStatus = i18n("Installation successful"));
	busyChanged(m_busy = false);
}

void apk_installer::cancelInstallApk()
{
	qDebug() << "Cancelling download";
	m_apkFile.cancelWriting();
	m_apkFile.commit();
}
