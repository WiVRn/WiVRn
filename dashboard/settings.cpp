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
#include "rectangle_partitionner.h"
#include "steam_app.h"
#include "ui_settings.h"
#include "wivrn_server.h"

#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QToolTip>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace
{
// The index must match the order in the combo boxes
const std::vector<std::pair<int, std::string>> encoder_ids{
        {0, "auto"},
        {1, "nvenc"},
        {2, "vaapi"},
        {3, "x264"},
};

const std::vector<std::pair<int, std::string>> codec_ids{
        {0, "auto"},
        {1, "h264"},
        {1, "avc"},
        {2, "h265"},
        {2, "hevc"},
        {3, "av1"},
        {3, "AV1"},
};

const std::vector<std::pair<int, int>> compatible_combos{
        // Automatically chosen encoder
        {0, 0},
        // nvenc
        {1, 0},
        {1, 1},
        {1, 2},
        // {1, 3},
        // vaapi
        {2, 0},
        {2, 1},
        {2, 2},
        {2, 3},
        // x264
        {3, 1},
};

int encoder_id_from_string(std::string_view s)
{
	for (auto & [i, j]: encoder_ids)
	{
		if (j == s)
			return i;
	}
	return 0;
}

const std::string & encoder_from_id(int id)
{
	for (auto & [i, j]: encoder_ids)
	{
		if (i == id)
			return j;
	}

	static const std::string default_value = "auto";
	return default_value;
}

int codec_id_from_string(std::string_view s)
{
	for (auto & [i, j]: codec_ids)
	{
		if (j == s)
			return i;
	}
	return 0;
}

const std::string & codec_from_id(int id)
{
	for (auto & [i, j]: codec_ids)
	{
		if (i == id)
			return j;
	}

	static const std::string default_value = "auto";
	return default_value;
}

} // namespace

settings::settings(wivrn_server * server_interface) :
        server_interface(server_interface)
{
	ui = new Ui::Settings;
	ui->setupUi(this);

	connect(ui->combo_select_game, &QComboBox::currentIndexChanged, this, &settings::on_selected_game_changed);
	connect(ui->button_game_browse, &QPushButton::clicked, this, &settings::on_browse_game);

	// Use a queued connection so that the slot is called after the widgets have been laid out
	connect(ui->radio_manual_encoder, &QRadioButton::toggled, this, [&](bool value) {
		if (value)
			ui->scrollArea->ensureWidgetVisible(ui->layout_encoder_config); }, Qt::QueuedConnection);

	connect(ui->partitionner, &rectangle_partitionner::selected_index_change, this, &settings::selected_rectangle_changed);

	connect(ui->encoder, &QComboBox::currentIndexChanged, this, &settings::on_selected_encoder_changed);
	connect(ui->encoder, &QComboBox::currentIndexChanged, this, &settings::on_encoder_settings_changed);
	connect(ui->codec, &QComboBox::currentIndexChanged, this, &settings::on_encoder_settings_changed);

	connect(this, &QDialog::accepted, this, &settings::save_settings);
	connect(ui->buttonBox->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this, &settings::restore_defaults);

	setWindowModality(Qt::WindowModality::ApplicationModal);

	fill_steam_games_list();
	load_settings();

	ui->partitionner->set_paint([&](QPainter & painter, QRect rect, const QVariant & data, int index, bool selected) {
		QPalette palette = QApplication::palette();

		if (selected)
		{
			painter.fillRect(rect.adjusted(1, 1, 0, 0), palette.color(QPalette::Highlight));
		}

		auto [encoder_id, codec_id] = data.value<std::pair<int, int>>();

		QString codec = ui->codec->itemText(codec_id);
		QString encoder = ui->encoder->itemText(encoder_id);

		QFont font = painter.font();
		QFont font2 = font;

		font2.setPixelSize(24);

		QString text = QString("%1\n%2").arg(encoder, codec);

		QFontMetrics metrics{font2};
		QSize size = metrics.size(0, text);

		if (double ratio = std::max((double)size.width() / rect.width(), (double)size.height() / rect.height()); ratio > 1)
		{
			int pixel_size = font2.pixelSize() / ratio;
			if (pixel_size > 0)
				font2.setPixelSize(pixel_size);
		}

		painter.setFont(font2);
		painter.drawText(rect, Qt::AlignCenter, text);
		painter.setFont(font);
	});
}

settings::~settings()
{
	delete ui;
	ui = nullptr;
}

void settings::fill_steam_games_list()
{
	ui->combo_select_game->clear();
	ui->combo_select_game->addItem(tr("None", "Game selection combo box"));
	ui->combo_select_game->addItem(tr("Custom...", "Game selection combo box"));

	auto apps = steam_apps();
	for (auto & app: apps)
	{
		ui->combo_select_game->addItem(QString::fromUtf8(app.name), QVariant::fromValue(app));
	}
}

void settings::on_selected_encoder_changed()
{
	int encoder = ui->encoder->currentIndex();

	QStandardItemModel * model = qobject_cast<QStandardItemModel *>(ui->codec->model());
	assert(model);

	for (int i = 0, n = model->rowCount(); i < n; i++)
	{
		auto * item = model->item(i);
		item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
	}

	for (auto [i, j]: compatible_combos)
	{
		if (encoder == i)
		{
			auto * item = model->item(j);
			item->setFlags(item->flags() | Qt::ItemIsEnabled);
		}
	}
}

void settings::on_encoder_settings_changed()
{
	if (ui->partitionner->selected_index() < 0)
		return;

	QString status;

	int encoder = ui->encoder->currentIndex();
	int codec = ui->codec->currentIndex();

	bool compatible = false;

	for (auto [i, j]: compatible_combos)
	{
		if (encoder == i and codec == j)
		{
			compatible = true;
			break;
		}
	}

	if (!compatible)
	{
		// Find a compatible codec for this encoder
		for (auto [i, j]: compatible_combos)
		{
			if (encoder == i)
			{
				codec = j;
				ui->codec->setCurrentIndex(j);
			}
		}
	}

	if (codec == 3 /* av1 */)
	{
		status += tr("Not all headsets and GPUs support AV1\n");
	}

	ui->partitionner->set_rectangles_data(ui->partitionner->selected_index(), QVariant::fromValue(std::pair(encoder, codec)));

	ui->message->setText(status);
}

void settings::on_selected_game_changed(int index)
{
	if (index == 0) // No game selected
	{
		ui->edit_game_path->setEnabled(false);
		ui->button_game_browse->setEnabled(false);
	}
	else if (index == 1) // Custom path
	{
		ui->edit_game_path->setEnabled(true);
		ui->button_game_browse->setEnabled(true);
	}
	else // From Steam vrmanifest
	{
		ui->edit_game_path->setEnabled(false);
		ui->button_game_browse->setEnabled(false);
	}
}

void settings::on_browse_game()
{
	QFileInfo info(application().empty() ? QString{} : QString::fromStdString(application().front()));

	QFileDialog dlg(this, tr("Select game"));
	dlg.setFilter(QDir::Executable | QDir::Files);
	dlg.setFileMode(QFileDialog::ExistingFile);
	dlg.setDirectory(info.dir());

	if (not dlg.exec())
		return;

	auto files = dlg.selectedFiles();

	if (files.empty())
		return;

	set_application({files.front().toStdString()});
}

void settings::selected_rectangle_changed(int index)
{
	if (index >= 0)
	{
		auto [encoder, codec] = ui->partitionner->rectangles_data(index).value<std::pair<int, int>>();

		ui->encoder->setCurrentIndex(encoder);
		ui->codec->setCurrentIndex(codec);
		ui->encoder->setEnabled(true);
		ui->codec->setEnabled(true);
	}
	else
	{
		ui->encoder->setCurrentIndex(-1);
		ui->codec->setCurrentIndex(-1);
		ui->encoder->setEnabled(false);
		ui->codec->setEnabled(false);
	}
}

void settings::restore_defaults()
{
	ui->bitrate->setValue(50);
	ui->radio_auto_foveation->setChecked(true);
	ui->radio_auto_encoder->setChecked(true);
	ui->layout_encoder_config->setVisible(false);

	std::vector<QRectF> rectangles;
	std::vector<QVariant> encoder_config;

	rectangles.push_back(QRectF(0, 0, 1, 1));
	std::pair<int, int> config{0, 0};
	encoder_config.push_back(QVariant::fromValue(config));

	ui->partitionner->set_rectangles(rectangles);
	ui->partitionner->set_rectangles_data(encoder_config);
	ui->partitionner->set_selected_index(0);

	ui->combo_select_game->setCurrentIndex(0);
	ui->edit_game_path->setText("");
}

void settings::set_application(const std::vector<std::string> & app)
{
	ui->edit_game_path->setText(QString::fromStdString(escape_string(app)));
}

std::vector<std::string> settings::application()
{
	return unescape_string(ui->edit_game_path->text().toStdString());
}

void settings::load_settings()
{
	std::vector<QRectF> rectangles;
	std::vector<QVariant> encoder_config;

	// Encoders configuration
	try
	{
		auto conf = server_interface->jsonConfiguration();
		qDebug() << "Server config" << conf;
		json_doc = nlohmann::json::parse(conf.toUtf8());
	}
	catch (std::exception & e)
	{
		qWarning() << "Cannot read configuration: " << e.what();
		restore_defaults();
		return;
	}

	try
	{
		nlohmann::json encoders;

		if (json_doc.contains("encoders"))
		{
			encoders = json_doc["encoders"];
			ui->radio_auto_encoder->setChecked(false);
			ui->radio_manual_encoder->setChecked(true);
		}
		else if (json_doc.contains("encoders.disabled"))
		{
			encoders = json_doc["encoders.disabled"];
			ui->radio_auto_encoder->setChecked(true);
			ui->radio_manual_encoder->setChecked(false);
			ui->layout_encoder_config->setVisible(false);
		}
		else
		{
			ui->radio_auto_encoder->setChecked(true);
			ui->radio_manual_encoder->setChecked(false);
			ui->layout_encoder_config->setVisible(false);
		}

		for (auto & i: encoders)
		{
			int encoder = encoder_id_from_string(i.value("encoder", "auto"));
			int codec = codec_id_from_string(i.value("codec", "auto"));
			double width = i.value("width", 1.0);
			double height = i.value("height", 1.0);
			double offset_x = i.value("offset_x", 0.0);
			double offset_y = i.value("offset_y", 0.0);
			int group = i.value("group", 0); // TODO: handle groups

			rectangles.push_back(QRectF(offset_x, offset_y, width, height));
			encoder_config.push_back(QVariant::fromValue(std::pair(encoder, codec)));
		}
	}
	catch (...)
	{
		QMessageBox msgbox{QMessageBox::Information, tr("Invalid settings"), tr("The encoder configuration is invalid, the default values will be restored."), QMessageBox::Close, this};
		msgbox.exec();

		rectangles.clear();
		encoder_config.clear();
	}

	if (rectangles.empty())
	{
		ui->radio_auto_encoder->setChecked(true);
		ui->radio_manual_encoder->setChecked(false);
		ui->layout_encoder_config->setVisible(false);

		rectangles.push_back(QRectF(0, 0, 1, 1));
		std::pair<int, int> config{0, 0};
		encoder_config.push_back(QVariant::fromValue(config));
	}

	ui->partitionner->set_rectangles(rectangles);
	ui->partitionner->set_rectangles_data(encoder_config);

	if (rectangles.size() == 1)
	{
		ui->partitionner->set_selected_index(0);
		// Force updating the combo boxes, if the selected index is already 0 the signal is not called
		selected_rectangle_changed(0);
	}
	else
		ui->partitionner->set_selected_index(-1);

	// Foveation
	try
	{
		if (json_doc.contains("scale"))
		{
			ui->slider_foveation->setValue(std::round((1 - json_doc.value("scale", 1.0)) * 100));
			ui->spin_foveation->setValue(std::round((1 - json_doc.value("scale", 1.0)) * 100));
			ui->radio_auto_foveation->setChecked(false);
			ui->radio_manual_foveation->setChecked(true);
		}
		else
		{
			ui->radio_auto_foveation->setChecked(true);
			ui->radio_manual_foveation->setChecked(false);
		}
	}
	catch (...)
	{
		ui->radio_auto_foveation->setChecked(true);
		ui->radio_manual_foveation->setChecked(false);
	}

	// Bitrate
	try
	{
		ui->bitrate->setValue(json_doc.value("bitrate", 50'000'000.0) / 1'000'000);
	}
	catch (...)
	{
		ui->bitrate->setValue(50);
	}

	// Automatically started application
	std::vector<std::string> application;
	try
	{
		if (json_doc["application"].is_array())
			application = json_doc.value<std::vector<std::string>>("application", {});
		else if (json_doc["application"].is_string())
			application.push_back(json_doc["application"]);

		int game_index = 0;
		if (application.size() == 2 and application[0] == "steam")
		{
			auto url = application[1];

			// Indices 0 and 1 are None and Custom
			for (int i = 2, n = ui->combo_select_game->count(); i < n; i++)
			{
				QVariant data = ui->combo_select_game->itemData(i);
				if (data.value<steam_app>().url == url)
				{
					game_index = i;
					break;
				}
			}
		}

		if (game_index == 0 and not application.empty())
		{
			game_index = 1;
			set_application(application);
		}

		ui->combo_select_game->setCurrentIndex(game_index);
	}
	catch (...)
	{
		ui->combo_select_game->setCurrentIndex(0);
	}

	// Update the compatible codecs
	on_selected_encoder_changed();

	// Update the status text
	on_encoder_settings_changed();
}

void settings::save_settings()
{
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

	if (not ui->radio_auto_foveation->isChecked())
		json_doc["scale"] = 1 - ui->slider_foveation->value() / 100.0;

	json_doc["bitrate"] = static_cast<int>(ui->bitrate->value() * 1'000'000);

	std::vector<nlohmann::json> encoders;

	const auto & rect = ui->partitionner->rectangles();
	const auto & data = ui->partitionner->rectangles_data();

	for (int i = 0, n = rect.size(); i < n; i++)
	{
		nlohmann::json encoder;
		auto [encoder_id, codec_id] = data[i].value<std::pair<int, int>>();

		if (auto value = encoder_from_id(encoder_id); value != "auto")
			encoder["encoder"] = value;

		if (auto value = codec_from_id(codec_id); value != "auto")
			encoder["codec"] = value;

		encoder["width"] = rect[i].width();
		encoder["height"] = rect[i].height();
		encoder["offset_x"] = rect[i].x();
		encoder["offset_y"] = rect[i].y();

		// TODO encoder groups

		encoders.push_back(encoder);
	}

	std::ranges::stable_sort(encoders, [](const nlohmann::json & i, const nlohmann::json & j) {
		double size_i = i.value("width", 0.0) * i.value("height", 0.0);
		double size_j = j.value("width", 0.0) * j.value("height", 0.0);
		return size_i < size_j;
	});

	// If there is only one automatic encoder, don't save it
	if (encoders.size() != 1 or encoders[0].contains("codec") or encoders[0].contains("encoder"))
	{
		if (ui->radio_manual_encoder->isChecked())
			json_doc["encoders"] = encoders;
		else
			json_doc["encoders.disabled"] = encoders;
	}

	if (ui->combo_select_game->currentIndex() == 0)
	{
	}
	else if (ui->combo_select_game->currentIndex() == 1)
	{
		json_doc["application"] = application();
	}
	else
	{
		auto app = qvariant_cast<steam_app>(ui->combo_select_game->currentData());

		nlohmann::json application;
		application.push_back("steam");
		application.push_back(app.url);
		json_doc["application"] = application;
	}

	server_interface->setJsonConfiguration(QString::fromStdString(json_doc.dump(2)));
}

#include "moc_settings.cpp"
