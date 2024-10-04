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

#include "wizard.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QFileInfo>
#include <QMessageBox>
#include <QMetaEnum>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSaveFile>
#include <QScrollBar>
#include <QStandardPaths>
#include <QUrl>
#include <QWizard>
#include <algorithm>
#include <nlohmann/json.hpp>

#include "adb.h"
#include "gui_config.h"
#include "ui_wizard.h"
#include "version.h"
#include "wivrn_server.h"
#include <filesystem>
#include <regex>

using namespace Qt::Literals::StringLiterals;

enum class wizard_page
{
	select_headset_model,
	sideload_download,
	sideload_devmode,
	sideload_install,
	connect_hmd,
	start_game
};

struct headset
{
	QString name;
	QString devmode_url;
	QString store_name;
	QString store_url;
};

static const std::vector<headset> headsets_info = {
        // clang-format off
        {"HTC Vive Focus 3",  "https://developer.vive.com/resources/hardware-guides/vive-focus-specs-user-guide/how-do-i-put-focus-developer-mode/"},
        {"HTC Vive XR Elite", "https://developer.vive.com/resources/hardware-guides/vive-focus-specs-user-guide/how-do-i-put-focus-developer-mode/"},
        {"Meta Quest 1",      "https://developers.meta.com/horizon/documentation/native/android/mobile-device-setup/#enable-developer-mode"},
        {"Meta Quest 2",      "https://developers.meta.com/horizon/documentation/native/android/mobile-device-setup/#enable-developer-mode", "Meta Store", "https://www.meta.com/experiences/7959676140827574/"},
        {"Meta Quest 3",      "https://developers.meta.com/horizon/documentation/native/android/mobile-device-setup/#enable-developer-mode", "Meta Store", "https://www.meta.com/experiences/7959676140827574/"},
        {"Meta Quest Pro",    "https://developers.meta.com/horizon/documentation/native/android/mobile-device-setup/#enable-developer-mode", "Meta Store", "https://www.meta.com/experiences/7959676140827574/"},
        {"Pico Neo 4",        "https://developer.picoxr.com/document/unreal/test-and-build/#Enable%20developer%20mode"},
        // clang-format on
};

static constexpr int role_devmode_url = Qt::UserRole + 1;
static constexpr int role_store_name = Qt::UserRole + 2;
static constexpr int role_store_url = Qt::UserRole + 3;

// #define TEST 1 // Not in the WiVRn-APK repo
// #define TEST 2 // In the WiVRn-APK repo, not a tagged version
// #define TEST 3 // Current version
// #define TEST 4 // Old version

#if !defined(TEST) || TEST == 0 // Normal case
static const std::string git_version = wivrn::git_version;
static const std::string git_commit = wivrn::git_commit;
#elif TEST == 1
static const std::string git_version = "v0.19.1-60-g66ce2c5";
static const std::string git_commit = "66ce2c547dedf9b320af677ed42f32f361b32832";
#elif TEST == 2
static const std::string git_version = "v0.19.1-59-g0d62929";
static const std::string git_commit = "554c0f7ad647d9cd66e380f567f2e842fff6a27a";
#elif TEST == 3
static const std::string git_version = "v0.19.1";
static const std::string git_commit = "bcffadcd4e043d7bacd700b1331c2e98f96ca178";
#elif TEST == 4
static const std::string git_version = "v0.19";
static const std::string git_commit = "d0014823480bc0e54594b74d2475882554ed5829";
#endif

wizard::wizard() :
        server(new wivrn_server(this))
{
	ui = new Ui::Wizard;
	ui->setupUi(this);

	retranslate();

	connect(this, &QWizard::currentIdChanged, this, &wizard::on_page_changed);
	connect(this, &QWizard::customButtonClicked, this, &wizard::on_custom_button_clicked);

	connect(ui->copy_steam_command, &QPushButton::clicked, this, [&]() {
		QGuiApplication::clipboard()->setText(ui->label_steam_command->text());
	});

	connect(server, &wivrn_server::headsetConnectedChanged, this, &wizard::on_headset_connected_changed);

	connect(ui->button_cancel_download, &QPushButton::clicked, this, &wizard::cancel_download);

	connect(server, &wivrn_server::steamCommandChanged, this, [this](QString value) {
		ui->label_steam_command->setText(value);
	});
	ui->label_steam_command->setText(server->steamCommand());

	setPixmap(WatermarkPixmap, QPixmap(":/images/wivrn.svg"));
	for (const headset & i: headsets_info)
	{
		auto item = new QStandardItem(i.name);

		item->setData(i.devmode_url, role_devmode_url);
		item->setData(i.store_name, role_store_name);
		item->setData(i.store_url, role_store_url);

		headsets.appendRow(item);
	}
	headsets.appendRow(new QStandardItem(tr("Other"))); // TODO retranslatable

	ui->combo_hmd_model->setModel(&headsets);
	ui->combo_hmd_model->setCurrentIndex(-1);
	connect(ui->combo_hmd_model, &QComboBox::currentIndexChanged, this, &wizard::update_welcome_page);
	connect(ui->check_sideload, &QCheckBox::clicked, this, &wizard::update_welcome_page);

	setWindowModality(Qt::WindowModality::ApplicationModal);

	if (std::regex_match(git_version, std::regex(".*-g[0-9a-f]+")))
	{
		apk_file.setFileName(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/wivrn-" + QString::fromStdString(git_commit) + ".apk");

		qDebug() << "Not a tagged version" << git_version.c_str();

		ui->label_release_info->setText(
		        tr("This is not a tagged release.") +
		        "\n" +
		        tr("If you install the headset app from the store, it might not be compatible with this server.") +
		        "\n" +
		        tr("If you install the headset app manually, this wizard will download the version that matches the dashboard."));

		std::string url = "https://api.github.com/repos/WiVRn/WiVRn-APK/releases/tags/apk-" + git_commit;
		qDebug() << "Downloading metadata from" << url.c_str();

		apk_release_reply = manager.get(QNetworkRequest{QUrl{url.c_str()}});
		connect(apk_release_reply, &QNetworkReply::finished, this, &wizard::on_apk_release_finished);
	}
	else
	{
		apk_file.setFileName(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/wivrn-" + QString::fromStdString(git_version) + ".apk");

		qDebug() << "Tagged version" << git_version.c_str();

		// Get the current release information
		std::string url = "https://api.github.com/repos/WiVRn/WiVRn/releases/tags/" + git_version;
		qDebug() << "Downloading metadata from" << url.c_str();
		apk_release_reply = manager.get(QNetworkRequest{QUrl{url.c_str()}});
		connect(apk_release_reply, &QNetworkReply::finished, this, &wizard::on_apk_release_finished);

		// Get the latest release information
		latest_release_reply = manager.get(QNetworkRequest{QUrl{"https://api.github.com/repos/WiVRn/WiVRn/releases/latest"}});
		connect(latest_release_reply, &QNetworkReply::finished, this, &wizard::on_latest_release_finished);
	}

	// Detect connected android devices
	poll_devices_timer.setInterval(std::chrono::milliseconds(500));
	connect(&poll_devices_timer, &QTimer::timeout, this, [this]() {
		if (android_devices_future.isFinished())
		{
			android_devices_promise = adb{}.devices();
			android_devices_future = android_devices_promise->future();
			android_devices_future_watcher.setFuture(android_devices_future);
		}
	});
	poll_devices_timer.start();
	connect(&android_devices_future_watcher, &QFutureWatcher<std::vector<adb::device>>::finished, this, &wizard::on_android_devices_future_finished);
}

void wizard::on_latest_release_finished()
{
	disconnect(latest_release_reply, &QNetworkReply::finished, this, &wizard::on_latest_release_finished);

	// By default, assume this is the latest release
	latest_release = git_version;

	try
	{
		if (latest_release_reply->error())
			throw std::runtime_error(latest_release_reply->errorString().toStdString());

		auto json = nlohmann::json::parse(latest_release_reply->readAll());

		latest_release = json["tag_name"];

		if (latest_release == git_version)
		{
			// This is already the latest release
			ui->label_release_info->setText("This is the latest WiVRn release.");
		}
		else
		{
			ui->label_release_info->setText(
			        tr("A new release is available (%1 → %2).").arg(QString::fromLatin1(git_version)).arg(QString::fromLatin1(latest_release)) +
			        "\n" +
			        tr("If you install the headset app from the store, it might not be compatible with this server.") +
			        "\n" +
			        tr("If you install the headset app manually, this wizard will download the version that matches the dashboard."));
		}
	}
	catch (std::exception & e)
	{
		qWarning() << "Cannot get latest release: " << e.what();
		ui->label_release_info->setText(tr("Cannot get latest release: %1").arg(e.what()));
	}

	latest_release_reply->deleteLater();
	latest_release_reply = nullptr;
	update_welcome_page();
}

void wizard::on_apk_release_finished()
{
	disconnect(apk_release_reply, &QNetworkReply::finished, this, &wizard::on_apk_release_finished);

	try
	{
		auto error = apk_release_reply->error();

		if (error == QNetworkReply::NetworkError::ContentNotFoundError)
		{
			ui->label_release_info->setText(
			        "<html><head/><body><p>" +
			        tr("There is no precompiled APK for this version.") +
			        "</p>\n<p>" +
			        tr("Follow the <a href=\"https://github.com/WiVRn/WiVRn/blob/master/docs/building.md#client-headset\"><span style=\"text-decoration: underline; color:#2980b9;\">documentation</span></a> to build your own client.") +
			        "</p></body></html>");

			ui->combo_hmd_model->setEnabled(false);
			ui->combo_hmd_model->setCurrentIndex(-1);
			ui->check_sideload->setEnabled(false);
		}
		else if (error != QNetworkReply::NetworkError::NoError)
		{
			throw std::runtime_error(apk_release_reply->errorString().toStdString());
		}
		else
		{
			auto json = nlohmann::json::parse(apk_release_reply->readAll());

			for (auto & i: json["assets"])
			{
				std::string name = i["name"];

				if (name.ends_with("-standard-release.apk"))
				{
					apk_url = i["browser_download_url"];
					qDebug() << "Using APK URL" << apk_url.c_str();
					break;
				}
			}

			apk_downloaded = QFileInfo(apk_file).exists();
		}
	}
	catch (std::exception & e)
	{
		qWarning() << "Cannot get APK information: " << e.what();
		ui->label_release_info->setText(tr("Cannot get APK information: %1").arg(e.what()));
	}

	apk_release_reply->deleteLater();
	apk_release_reply = nullptr;
	update_welcome_page();
}

void wizard::update_welcome_page()
{
	if (latest_release == "")
	{
		// We don't have the latest release information yet
		ui->widget_store->setVisible(false);
		ui->check_sideload->setVisible(false);
		button(NextButton)->setEnabled(false);
	}

	auto devmode_url = ui->combo_hmd_model->currentData(role_devmode_url).toString();
	auto store_name = ui->combo_hmd_model->currentData(role_store_name).toString();
	auto store_url = ui->combo_hmd_model->currentData(role_store_url).toString();

	ui->label_store_url->setText(QString(R"(<html><head/><body><p><a href="%1"><span style="text-decoration: underline; color:#2980b9;">%2</span></a></p></body></html>)").arg(store_url).arg(store_name));
	ui->label_devmode_url->setText(QString(R"(<html><head/><body><p><a href="%1"><span style="text-decoration: underline; color:#2980b9;">%2</span></a></p></body></html>)").arg(devmode_url).arg(tr("How?")));

	if (ui->combo_hmd_model->currentIndex() < 0)
	{
		// No headset selected
		ui->widget_store->setVisible(false);
		ui->check_sideload->setVisible(false);
		button(NextButton)->setEnabled(false);
	}
	else if (store_name != "")
	{
		// Headset with a link to a store
		ui->widget_store->setVisible(not ui->check_sideload->isChecked());
		ui->check_sideload->setVisible(true);
		button(NextButton)->setEnabled(apk_url != "" or nextId() != (int)wizard_page::sideload_download);
	}
	else
	{
		// Headset without store
		ui->widget_store->setVisible(false);
		ui->check_sideload->setVisible(false);
		button(NextButton)->setEnabled(apk_url != "");
	}

	ui->label_devmode_url->setVisible(devmode_url != "");

	if (nextId() == (int)wizard_page::sideload_download and ui->combo_hmd_model->currentIndex() >= 0 and apk_url != "")
		button(NextButton)->setText(tr("Download"));
	else
		button(NextButton)->setText(QWizard::tr("&Next >", "do not translate, the QWizard translation will be used"));
}

void wizard::on_custom_button_clicked(int which)
{
	switch ((wizard_page)currentId())
	{
		case wizard_page::select_headset_model:
			if (which == QWizard::CustomButton1) // Download
				next();
			else if (which == QWizard::CustomButton2) // Skip
				setCurrentId((int)wizard_page::connect_hmd);
			return;

		case wizard_page::sideload_download:
			return;

		case wizard_page::sideload_devmode:
			return;

		case wizard_page::sideload_install:
			return;

		case wizard_page::connect_hmd:
			if (which == QWizard::CustomButton1) // Skip
				next();
			return;

		case wizard_page::start_game:
			return;
	}
}

void wizard::on_page_changed(int id)
{
	switch ((wizard_page)id)
	{
		case wizard_page::select_headset_model:
			button(WizardButton::CustomButton1)->setText(tr("Download"));
			button(WizardButton::CustomButton2)->setText(tr("Skip"));

			setButtonLayout({QWizard::Stretch, QWizard::BackButton, QWizard::NextButton, QWizard::CustomButton2, QWizard::CancelButton});
			update_welcome_page();
			return;

		case wizard_page::sideload_download:
			assert(not apk_downloaded);
			start_download();

			setButtonLayout({QWizard::Stretch, QWizard::BackButton, QWizard::NextButton, QWizard::CancelButton});
			button(BackButton)->setEnabled(false);
			button(NextButton)->setEnabled(false);
			return;

		case wizard_page::sideload_devmode:
			setButtonLayout({QWizard::Stretch, QWizard::BackButton, QWizard::NextButton, QWizard::CancelButton});
			on_adb_device_list_changed();
			return;

		case wizard_page::sideload_install:
			start_install();
			setButtonLayout({QWizard::Stretch, QWizard::BackButton, QWizard::NextButton, QWizard::CancelButton});
			button(BackButton)->setEnabled(false);
			button(NextButton)->setEnabled(false);
			return;

		case wizard_page::connect_hmd:
			button(WizardButton::CustomButton1)->setText(tr("Skip"));
			setButtonLayout({QWizard::Stretch, QWizard::BackButton, QWizard::NextButton, QWizard::CustomButton1, QWizard::CancelButton});
			on_headset_connected_changed(server->isHeadsetConnected());
			return;

		case wizard_page::start_game:
			setButtonLayout({QWizard::Stretch, QWizard::BackButton, QWizard::FinishButton, QWizard::CancelButton});
			return;
	}
}

void wizard::changeEvent(QEvent * e)
{
	QWidget::changeEvent(e);

	switch (e->type())
	{
		case QEvent::LanguageChange:
			retranslate();
			break;

		default:
			break;
	}
}

int wizard::nextId() const
{
	switch ((wizard_page)currentId())
	{
		case wizard_page::select_headset_model:
			if (ui->combo_hmd_model->currentData(role_store_name).toString() != "" and not ui->check_sideload->isChecked())
				return (int)wizard_page::connect_hmd;
			else if (apk_downloaded)
				return (int)wizard_page::sideload_devmode;
			else
				return (int)wizard_page::sideload_download;

		case wizard_page::sideload_install:
			return (int)wizard_page::connect_hmd;

		default:
			return QWizard::nextId();
	}
}

void wizard::retranslate()
{
	ui->retranslateUi(this);
	ui->label_how_to_connect->setText(tr("Start the WiVRn app on your headset and connect to \"%1\".").arg(server->hostname()));
	on_headset_connected_changed(server->isHeadsetConnected());
}

wizard::~wizard()
{
	delete ui;
	ui = nullptr;
}

void wizard::start_download()
{
	ui->button_cancel_download->setEnabled(true);

	QUrl url(apk_url.c_str());

	qDebug() << "Downloading from" << url.toString();

	auto apk_dir = std::filesystem::path(apk_file.fileName().toStdString()).parent_path();
	std::filesystem::create_directories(apk_dir);

	apk_file.open(QIODeviceBase::WriteOnly | QIODeviceBase::Truncate);

	QNetworkRequest request(url);

	if (apk_reply)
	{
		disconnect(apk_reply, &QNetworkReply::downloadProgress, this, &wizard::on_download_progress);
		disconnect(apk_reply, &QNetworkReply::errorOccurred, this, &wizard::on_download_error);
		disconnect(apk_reply, &QNetworkReply::finished, this, &wizard::on_download_finished);
		apk_reply->abort();
		apk_reply->deleteLater();
	}

	apk_reply = manager.get(request);

	connect(apk_reply, &QNetworkReply::downloadProgress, this, &wizard::on_download_progress);
	connect(apk_reply, &QNetworkReply::errorOccurred, this, &wizard::on_download_error);
	connect(apk_reply, &QNetworkReply::finished, this, &wizard::on_download_finished);

	ui->progress_download->setValue(0);
}

void wizard::cancel_download()
{
	assert(reply);

	disconnect(apk_reply, &QNetworkReply::downloadProgress, this, &wizard::on_download_progress);
	disconnect(apk_reply, &QNetworkReply::errorOccurred, this, &wizard::on_download_error);
	disconnect(apk_reply, &QNetworkReply::finished, this, &wizard::on_download_finished);
	apk_reply->abort();
	apk_reply->deleteLater();
	apk_reply = nullptr;

	// Cancel and commit to close without saving the already written data, so that it can be reopened
	apk_file.cancelWriting();
	apk_file.commit();

	back();
}

void wizard::on_download_progress(qint64 bytesReceived, qint64 bytesTotal)
{
	assert(reply);

	if (not apk_reply->error())
	{
		if (bytesTotal > 0)
			ui->progress_download->setValue(bytesReceived * 100 / bytesTotal);

		apk_file.write(apk_reply->readAll());
	}
}

void wizard::on_download_error(QNetworkReply::NetworkError code)
{
	qDebug() << "Download error" << code;

	// Cancel and commit to close without saving the already written data, so that it can be reopened
	apk_file.cancelWriting();
	apk_file.commit();

	QMessageBox msgbox /*(this)*/;
	msgbox.setIcon(QMessageBox::Critical);
	msgbox.setText(tr("Error downloading the client:\n%1").arg(apk_reply->errorString()));
	msgbox.setStandardButtons(QMessageBox::Retry | QMessageBox::Cancel);

#ifdef WORKAROUND_QTBUG_90005
	setEnabled(false);
#endif

	switch (msgbox.exec())
	{
		case QMessageBox::Retry:
			start_download();
			break;

		case QMessageBox::Cancel:
			back();
			break;
	}

#ifdef WORKAROUND_QTBUG_90005
	setEnabled(true);
#endif
}

void wizard::on_download_finished()
{
	assert(reply);

	if (not apk_reply->error())
	{
		auto last_modified = apk_reply->header(QNetworkRequest::LastModifiedHeader).value<QDateTime>();
		apk_file.setFileTime(last_modified, QFileDevice::FileModificationTime);

		apk_reply->deleteLater();
		apk_reply = nullptr;

		ui->button_cancel_download->setEnabled(false);
		apk_file.commit();
		apk_downloaded = true;

		back(); // Remove current page from history before going to the install page
		setCurrentId((int)wizard_page::sideload_devmode);
	}
}

void wizard::on_android_devices_future_finished()
{
	auto old_devs = android_devices;
	android_devices = android_devices_future.result();

	bool changed = false;
	for (auto & i: android_devices)
	{
		if (i.state() != "device")
			continue;

		if (std::ranges::find(old_devs, i) == old_devs.end())
			changed = true;
	}

	for (auto & i: old_devs)
	{
		if (i.state() != "device")
			continue;

		if (std::ranges::find(android_devices, i) == android_devices.end())
			changed = true;
	}

	if (changed)
		on_adb_device_list_changed();
}

void wizard::on_adb_device_list_changed()
{
	if (android_devices.empty())
	{
		ui->label_device_detected->setText(tr("No device detected."));

		if (currentId() == (int)wizard_page::sideload_devmode)
			button(NextButton)->setEnabled(false);
	}
	else
	{
		ui->label_device_detected->setText(tr("%n device(s) detected.", nullptr, android_devices.size()));

		if (currentId() == (int)wizard_page::sideload_devmode)
			button(NextButton)->setEnabled(true);
	}

	// TODO show device list
}

void wizard::start_install()
{
	if (android_devices.empty())
	{
		back();
		return;
	}

	auto dev = android_devices.back(); // TODO choose device from device list
	process_adb_install = dev.install(apk_file.fileName().toStdString());

	connect(process_adb_install.get(), &QProcess::finished, this, &wizard::on_install_finished);
	connect(process_adb_install.get(), &QProcess::readyReadStandardOutput, this, &wizard::on_install_stdout);
	connect(process_adb_install.get(), &QProcess::readyReadStandardError, this, &wizard::on_install_stderr);
	process_adb_install->setParent(this);
	process_adb_install->start();
	ui->adb_install_logs->setPlainText("");
}

void wizard::on_install_stdout()
{
	ui->adb_install_logs->appendPlainText(process_adb_install->readAllStandardOutput());
	ui->adb_install_logs->verticalScrollBar()->setValue(ui->adb_install_logs->verticalScrollBar()->maximum());
}

void wizard::on_install_stderr()
{
	ui->adb_install_logs->appendPlainText(process_adb_install->readAllStandardError());
	ui->adb_install_logs->verticalScrollBar()->setValue(ui->adb_install_logs->verticalScrollBar()->maximum());
}

void wizard::on_install_finished(int exit_code, QProcess::ExitStatus exit_status)
{
	if (exit_code == 0 and exit_status == QProcess::NormalExit)
	{
		// back(); // Remove current page from history before going to the connect page
		// setCurrentId((int)wizard_page::connect_hmd);

		button(NextButton)->setEnabled(true);
	}
}

void wizard::on_headset_connected_changed(bool connected)
{
	if (currentId() == (int)wizard_page::connect_hmd)
	{
		if (connected)
			ui->label_client_status->setText(tr("The headset is connected."));
		else
			ui->label_client_status->setText(tr("The headset is not connected."));

		button(NextButton)->setEnabled(connected);
		ui->widget_troubleshoot->setHidden(connected);
	}
}

#include "moc_wizard.cpp"
