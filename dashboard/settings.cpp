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

#include "rectangle_partitionner.h"
#include "ui_settings.h"
#include "wivrn_server.h"

#include <QFile>
#include <QJsonObject>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QToolTip>

#include <iostream>
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
        {3, "av1"},
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

	QJsonParseError error;
	json_doc = QJsonDocument::fromJson(server_interface->jsonConfiguration().toUtf8(), &error);

	std::vector<QRectF> rectangles;
	std::vector<QVariant> encoder_config;

	if (error.error)
	{
		std::cout << "Cannot read configuration: " << error.errorString().toStdString() << ", offset " << error.offset << std::endl;
	}
	else
	{
		auto encoders = json_doc.object().find("encoders");

		if (encoders->isArray())
		{
			ui->radio_auto_encoder->setChecked(false);
			ui->radio_manual_encoder->setChecked(true);
		}
		else
		{
			ui->radio_auto_encoder->setChecked(true);
			ui->radio_manual_encoder->setChecked(false);

			encoders = json_doc.object().find("encoders.disabled");
		}

		if (encoders->isArray())
		{
			for (QJsonValue i: encoders->toArray())
			{
				int encoder = encoder_id_from_string(i["encoder"].toString("auto").toLower().toStdString());
				int codec = codec_id_from_string(i["codec"].toString("auto").toLower().toStdString());
				double width = i["width"].toDouble(1);
				double height = i["height"].toDouble(1);
				double offset_x = i["offset_x"].toDouble(0);
				double offset_y = i["offset_y"].toDouble(0);
				int group = i["group"].toInt(0); // TODO: handle groups

				rectangles.push_back(QRectF(offset_x, offset_y, width, height));
				encoder_config.push_back(QVariant::fromValue(std::pair(encoder, codec)));
			}
		}
	}

	if (rectangles.empty())
	{
		rectangles.push_back(QRectF(0, 0, 1, 1));
		std::pair<int, int> config{0, 0};
		encoder_config.push_back(QVariant::fromValue(config));
	}

	try
	{
		ui->partitionner->set_rectangles(rectangles);
	}
	catch (...)
	{
		QMessageBox msgbox{QMessageBox::Information, tr("Invalid settings"), tr("The encoder configuration is invalid, the default values will be restored."), QMessageBox::Close, this};
		msgbox.exec();

		ui->radio_auto_encoder->setChecked(true);
		ui->radio_manual_encoder->setChecked(false);

		rectangles.clear();
		encoder_config.clear();

		rectangles.push_back(QRectF(0, 0, 1, 1));
		ui->partitionner->set_rectangles(rectangles);

		std::pair<int, int> config{0, 0};
		encoder_config.push_back(QVariant::fromValue(config));
	}

	ui->partitionner->set_rectangles_data(encoder_config);
	ui->partitionner->set_selected_index(0);

	if (json_doc["scale"].isDouble())
	{
		ui->slider_foveation->setValue(std::round((1 - json_doc["scale"].toDouble(1)) * 100));
		ui->spin_foveation->setValue(std::round((1 - json_doc["scale"].toDouble(1)) * 100));
		ui->radio_auto_foveation->setChecked(false);
		ui->radio_manual_foveation->setChecked(true);
	}
	else
	{
		ui->radio_auto_foveation->setChecked(true);
		ui->radio_manual_foveation->setChecked(false);
	}
	ui->bitrate->setValue(json_doc["bitrate"].toDouble(50'000'000) / 1'000'000);

	selected_rectangle_changed(0);

	connect(ui->partitionner, &rectangle_partitionner::selected_index_change, this, &settings::selected_rectangle_changed);
	// connect(ui->partitionner, &rectangle_partitionner::rectangles_change, this, [](){});

	connect(ui->encoder, &QComboBox::currentIndexChanged, this, &settings::on_encoder_changed);
	connect(ui->encoder, &QComboBox::currentIndexChanged, this, &settings::on_settings_changed);
	connect(ui->codec, &QComboBox::currentIndexChanged, this, &settings::on_settings_changed);
	connect(ui->bitrate, &QDoubleSpinBox::valueChanged, this, &settings::on_settings_changed);
	connect(ui->slider_foveation, &QSlider::valueChanged, this, &settings::on_settings_changed);
	connect(ui->spin_foveation, &QSpinBox::valueChanged, this, &settings::on_settings_changed);

	// connect(ui->foveation_info, &QPushButton::clicked, this, [&]() { QToolTip::showText(ui->radio_manual_foveation->pos(), ui->radio_manual_foveation->toolTip(), ui->radio_manual_foveation); });

	connect(this, &QDialog::accepted, this, &settings::save_settings);
	connect(ui->buttonBox->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this, &settings::restore_defaults);

	ui->partitionner->set_paint([&](QPainter & painter, QRect rect, const QVariant & data, int index, bool selected) {
		if (selected)
		{
			painter.fillRect(rect.adjusted(1, 1, 0, 0), QColorConstants::Cyan);
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

	// Update the compatible codecs
	on_encoder_changed();

	// Update the status text
	on_settings_changed();

	setWindowModality(Qt::WindowModality::ApplicationModal);
}

settings::~settings()
{
	delete ui;
	ui = nullptr;
}

void settings::on_encoder_changed()
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

void settings::on_settings_changed()
{
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

void settings::selected_rectangle_changed(int index)
{
	auto [encoder, codec] = ui->partitionner->rectangles_data(index).value<std::pair<int, int>>();

	ui->encoder->setCurrentIndex(encoder);
	ui->codec->setCurrentIndex(codec);
}

void settings::restore_defaults()
{
	ui->bitrate->setValue(50);
	ui->radio_auto_foveation->setChecked(true);
	ui->radio_auto_encoder->setChecked(true);

	std::vector<QRectF> rectangles;
	std::vector<QVariant> encoder_config;

	rectangles.push_back(QRectF(0, 0, 1, 1));
	std::pair<int, int> config{0, 0};
	encoder_config.push_back(QVariant::fromValue(config));

	ui->partitionner->set_rectangles(rectangles);
	ui->partitionner->set_rectangles_data(encoder_config);
	ui->partitionner->set_selected_index(0);
}

void settings::save_settings()
{
	QJsonObject json = json_doc.object();

	// Remove all optional keys that might not be overwritten
	auto it = json.find("scale");
	if (it != json.end())
		json.erase(it);

	it = json.find("encoders.disabled");
	if (it != json.end())
		json.erase(it);

	it = json.find("encoders");
	if (it != json.end())
		json.erase(it);

	if (not ui->radio_auto_foveation->isChecked())
		json["scale"] = 1 - ui->slider_foveation->value() / 100.0;

	json["bitrate"] = ui->bitrate->value() * 1'000'000;

	QJsonArray encoders;

	const auto & rect = ui->partitionner->rectangles();
	const auto & data = ui->partitionner->rectangles_data();

	for (int i = 0, n = rect.size(); i < n; i++)
	{
		QJsonObject encoder;
		auto [encoder_id, codec_id] = data[i].value<std::pair<int, int>>();

		if (auto value = QString::fromLatin1(encoder_from_id(encoder_id)); value != "auto")
			encoder["encoder"] = value;

		if (auto value = QString::fromLatin1(codec_from_id(codec_id)); value != "auto")
			encoder["codec"] = value;

		encoder["width"] = rect[i].width();
		encoder["height"] = rect[i].height();
		encoder["offset_x"] = rect[i].x();
		encoder["offset_y"] = rect[i].y();

		// TODO encoder groups

		encoders.push_back(encoder);
	}

	// If there is only one automatic encoder, don't save it'
	if (encoders.size() != 1 or encoders[0].toObject().contains("codec") or encoders[0].toObject().contains("encoder"))
	{
		if (ui->radio_manual_encoder->isChecked())
			json["encoders"] = encoders;
		else
			json["encoders.disabled"] = encoders;
	}

	server_interface->setJsonConfiguration(QString::fromUtf8(QJsonDocument(json).toJson()));
}

#include "moc_settings.cpp"
