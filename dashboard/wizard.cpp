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
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <QWizard>

#include "ui_wizard.h"

#include "wivrn_server.h"

#include "gui_config.h"
#include <filesystem>

enum class wizard_page
{
	select_headset_model,
	sideload_download,
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

wizard::wizard() :
        server(new wivrn_server(this)),
        apk_file(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/wivrn.apk")
{
	ui = new Ui::Wizard;
	ui->setupUi(this);

	retranslate();

	connect(this, &QWizard::currentIdChanged, this, &wizard::on_page_changed);
	connect(this, &QWizard::customButtonClicked, this, &wizard::on_custom_button_clicked);

	connect(ui->copy_steam_command, &QPushButton::clicked, this, [&]() {
		QGuiApplication::clipboard()->setText(ui->label_steam_command->text());
	});

	connect(ui->copy_adb_command, &QPushButton::clicked, this, [&]() {
		QGuiApplication::clipboard()->setText(ui->label_adb_install->text());
	});

	connect(server, &wivrn_server::headsetConnectedChanged, this, &wizard::on_headset_connected_changed);

	connect(ui->button_cancel_download, &QPushButton::clicked, this, &wizard::cancel_download);

	connect(server, &wivrn_server::steamCommandChanged, this, [this](QString value) {
		ui->label_steam_command->setText(value);
	});
	ui->label_steam_command->setText(server->steamCommand());

	setPixmap(WatermarkPixmap, QPixmap(":/images/wivrn.svg"));

	ui->label_adb_install->setText("adb install -r " + apk_file.fileName());
	apk_downloaded = QFileInfo(apk_file).exists() and (settings.value("client_apk_url").toString() == WIVRN_CLIENT_URL);

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
	connect(ui->combo_hmd_model, &QComboBox::currentIndexChanged, this, &wizard::on_headset_model_changed);
	connect(ui->check_sideload, &QCheckBox::clicked, this, &wizard::on_headset_model_changed);

	setWindowModality(Qt::WindowModality::ApplicationModal);
}

void wizard::on_headset_model_changed()
{
	auto devmode_url = ui->combo_hmd_model->currentData(role_devmode_url).toString();
	auto store_name = ui->combo_hmd_model->currentData(role_store_name).toString();
	auto store_url = ui->combo_hmd_model->currentData(role_store_url).toString();

	ui->label_store_url->setText(QString(R"(<html><head/><body><p><a href="%1"><span style="text-decoration: underline; color:#2980b9;">%2</span></a></p></body></html>)").arg(store_url).arg(store_name));
	ui->label_devmode_url->setText(QString(R"(<html><head/><body><p><a href="%1"><span style="text-decoration: underline; color:#2980b9;">%2</span></a></p></body></html>)").arg(devmode_url).arg(tr("How?")));

	if (ui->combo_hmd_model->currentIndex() < 0)
	{
		ui->widget_store->setVisible(false);
		ui->check_sideload->setVisible(false);
		button(NextButton)->setEnabled(false);
	}
	else if (store_name != "")
	{
		ui->widget_store->setVisible(not ui->check_sideload->isChecked());
		ui->check_sideload->setVisible(true);
		button(NextButton)->setEnabled(true);
	}
	else
	{
		ui->widget_store->setVisible(false);
		ui->check_sideload->setVisible(false);
		button(NextButton)->setEnabled(true);
	}

	ui->label_devmode_url->setVisible(devmode_url != "");

	if (nextId() == (int)wizard_page::sideload_download and ui->combo_hmd_model->currentIndex() >= 0)
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
			on_headset_model_changed();
			return;

		case wizard_page::sideload_download:
			assert(not apk_downloaded);
			start_download();

			setButtonLayout({QWizard::Stretch, QWizard::BackButton, QWizard::NextButton, QWizard::CancelButton});
			button(BackButton)->setEnabled(false);
			button(NextButton)->setEnabled(false);
			return;

		case wizard_page::sideload_install:
			setButtonLayout({QWizard::Stretch, QWizard::BackButton, QWizard::NextButton, QWizard::CancelButton});
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
				return (int)wizard_page::sideload_install;
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

	QUrl url(WIVRN_CLIENT_URL);

	qDebug() << "Downloading from " << url.toString();

	auto apk_dir = std::filesystem::path(apk_file.fileName().toStdString()).parent_path();
	std::filesystem::create_directories(apk_dir);

	apk_file.open(QIODeviceBase::WriteOnly | QIODeviceBase::Truncate);

	QNetworkRequest request(url);

	if (reply)
	{
		disconnect(reply, &QNetworkReply::downloadProgress, this, &wizard::on_download_progress);
		disconnect(reply, &QNetworkReply::errorOccurred, this, &wizard::on_download_error);
		disconnect(reply, &QNetworkReply::finished, this, &wizard::on_download_finished);
		reply->abort();
		reply->deleteLater();
	}

	reply = manager.get(request);

	connect(reply, &QNetworkReply::downloadProgress, this, &wizard::on_download_progress);
	connect(reply, &QNetworkReply::errorOccurred, this, &wizard::on_download_error);
	connect(reply, &QNetworkReply::finished, this, &wizard::on_download_finished);

	ui->progress_download->setValue(0);
}

void wizard::cancel_download()
{
	assert(reply);

	disconnect(reply, &QNetworkReply::downloadProgress, this, &wizard::on_download_progress);
	disconnect(reply, &QNetworkReply::errorOccurred, this, &wizard::on_download_error);
	disconnect(reply, &QNetworkReply::finished, this, &wizard::on_download_finished);
	reply->abort();
	reply->deleteLater();
	reply = nullptr;

	// Cancel and commit to close without saving the already written data, so that it can be reopened
	apk_file.cancelWriting();
	apk_file.commit();

	back();
}

void wizard::on_download_progress(qint64 bytesReceived, qint64 bytesTotal)
{
	assert(reply);

	if (not reply->error())
	{
		if (bytesTotal > 0)
			ui->progress_download->setValue(bytesReceived * 100 / bytesTotal);

		apk_file.write(reply->readAll());
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
	msgbox.setText(tr("Error downloading the client:\n%1").arg(QMetaEnum::fromType<QNetworkReply::NetworkError>().valueToKey(code)));
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

	if (not reply->error())
	{
		reply->deleteLater();
		reply = nullptr;

		ui->button_cancel_download->setEnabled(false);
		apk_file.commit();
		apk_downloaded = true;
		settings.setValue("client_apk_url", WIVRN_CLIENT_URL);

		back(); // Remove current page from history before going to the install page
		setCurrentId((int)wizard_page::sideload_install);
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
