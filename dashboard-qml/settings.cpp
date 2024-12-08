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
#include "wivrn_server.h"

void Settings::restore_defaults()
{
	set_encoders({});
	set_encoderPassthrough({});
	set_bitrate(50'000'000);
	set_scale(-1);
	set_application("");
	set_tcpOnly(false);
}

void Settings::load(const wivrn_server * server)
{
	// std::vector<QRectF> rectangles;
	// std::vector<QVariant> encoder_config;

	// Encoders configuration
	try
	{
		auto conf = server->jsonConfiguration();
		json_doc = nlohmann::json::parse(conf.toUtf8());
	}
	catch (std::exception & e)
	{
		qWarning() << "Cannot read configuration: " << e.what();
		restore_defaults();
		return;
	}

	// try
	// {
	// 	nlohmann::json encoders;
	//
	// 	if (json_doc.contains("encoders"))
	// 	{
	// 		encoders = json_doc["encoders"];
	// 		ui->radio_auto_encoder->setChecked(false);
	// 		ui->radio_manual_encoder->setChecked(true);
	// 	}
	// 	else if (json_doc.contains("encoders.disabled"))
	// 	{
	// 		encoders = json_doc["encoders.disabled"];
	// 		ui->radio_auto_encoder->setChecked(true);
	// 		ui->radio_manual_encoder->setChecked(false);
	// 		ui->layout_encoder_config->setVisible(false);
	// 	}
	// 	else
	// 	{
	// 		ui->radio_auto_encoder->setChecked(true);
	// 		ui->radio_manual_encoder->setChecked(false);
	// 		ui->layout_encoder_config->setVisible(false);
	// 	}
	//
	// 	for (auto & i: encoders)
	// 	{
	// 		int encoder = encoder_id_from_string(i.value("encoder", "auto"));
	// 		int codec = codec_id_from_string(i.value("codec", "auto"));
	// 		double width = i.value("width", 1.0);
	// 		double height = i.value("height", 1.0);
	// 		double offset_x = i.value("offset_x", 0.0);
	// 		double offset_y = i.value("offset_y", 0.0);
	// 		int group = i.value("group", 0); // TODO: handle groups
	//
	// 		rectangles.push_back(QRectF(offset_x, offset_y, width, height));
	// 		encoder_config.push_back(QVariant::fromValue(std::pair(encoder, codec)));
	// 	}
	// }
	// catch (...)
	// {
	// 	QMessageBox msgbox{QMessageBox::Information, tr("Invalid settings"), tr("The encoder configuration is invalid, the default values will be restored."), QMessageBox::Close, this};
	// 	msgbox.exec();
	//
	// 	rectangles.clear();
	// 	encoder_config.clear();
	// }
	//
	// if (rectangles.empty())
	// {
	// 	ui->radio_auto_encoder->setChecked(true);
	// 	ui->radio_manual_encoder->setChecked(false);
	// 	ui->layout_encoder_config->setVisible(false);
	//
	// 	rectangles.push_back(QRectF(0, 0, 1, 1));
	// 	std::pair<int, int> config{0, 0};
	// 	encoder_config.push_back(QVariant::fromValue(config));
	// }
	//
	// ui->partitionner->set_rectangles(rectangles);
	// ui->partitionner->set_rectangles_data(encoder_config);
	//
	// if (rectangles.size() == 1)
	// {
	// 	ui->partitionner->set_selected_index(0);
	// 	// Force updating the combo boxes, if the selected index is already 0 the signal is not called
	// 	selected_rectangle_changed(0);
	// }
	// else
	// 	ui->partitionner->set_selected_index(-1);

	// Foveation
	try
	{
		if (json_doc.contains("scale"))
		{
			set_scale(json_doc.value("scale", 1.0));
		}
		else
		{
			set_scale(-1);
		}
	}
	catch (...)
	{
		set_scale(-1);
	}

	// Bitrate
	try
	{
		set_bitrate(json_doc.value("bitrate", 50'000'000));
	}
	catch (...)
	{
		set_bitrate(50'000'000);
	}

	// Automatically started application
	std::vector<std::string> application;
	try
	{
		if (json_doc["application"].is_array())
			application = json_doc.value<std::vector<std::string>>("application", {});
		else if (json_doc["application"].is_string())
			application.push_back(json_doc["application"]);

		set_application(escape_string(application));
	}
	catch (...)
	{
		set_application("");
	}
}

void Settings::save(wivrn_server * server)
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

	if (scale() > 0)
		json_doc["scale"] = scale();

	json_doc["bitrate"] = bitrate();

	// std::vector<nlohmann::json> encoders;
	//
	// const auto & rect = ui->partitionner->rectangles();
	// const auto & data = ui->partitionner->rectangles_data();
	//
	// for (int i = 0, n = rect.size(); i < n; i++)
	// {
	// 	nlohmann::json encoder;
	// 	auto [encoder_id, codec_id] = data[i].value<std::pair<int, int>>();
	//
	// 	if (auto value = encoder_from_id(encoder_id); value != "auto")
	// 		encoder["encoder"] = value;
	//
	// 	if (auto value = codec_from_id(codec_id); value != "auto")
	// 		encoder["codec"] = value;
	//
	// 	encoder["width"] = rect[i].width();
	// 	encoder["height"] = rect[i].height();
	// 	encoder["offset_x"] = rect[i].x();
	// 	encoder["offset_y"] = rect[i].y();
	//
	// 	// TODO encoder groups
	//
	// 	encoders.push_back(encoder);
	// }
	//
	// std::ranges::stable_sort(encoders, [](const nlohmann::json & i, const nlohmann::json & j) {
	// 	double size_i = i.value("width", 0.0) * i.value("height", 0.0);
	// 	double size_j = j.value("width", 0.0) * j.value("height", 0.0);
	// 	return size_i < size_j;
	// });
	//
	// // If there is only one automatic encoder, don't save it
	// if (encoders.size() != 1 or encoders[0].contains("codec") or encoders[0].contains("encoder"))
	// {
	// 	if (ui->radio_manual_encoder->isChecked())
	// 		json_doc["encoders"] = encoders;
	// 	else
	// 		json_doc["encoders.disabled"] = encoders;
	// }

	if (application() != "")
	{
		json_doc["application"] = unescape_string(application());
	}

	server->setJsonConfiguration(QString::fromStdString(json_doc.dump(2)));
}

#include "moc_settings.cpp"
