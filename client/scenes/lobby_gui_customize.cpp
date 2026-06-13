/*
 * WiVRn VR streaming
 * Copyright (C) 2022-2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022-2024  Patrick Nicolas <patricknicolas@laposte.net>
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

#define IMGUI_DEFINE_MATH_OPERATORS

#include "lobby.h"

#include "application.h"
#include "constants.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imspinner.h"
#include "render/image_writer.h"
#include "render/scene_renderer.h"
#include "render/ui_theme.h"
#include "render/ui_widgets.h"
#include "utils/files.h"
#include "utils/i18n.h"
#include "utils/json_string.h"
#include "vk/allocation.h"
#include <algorithm>
#include <boost/url/url.hpp>
#include <boost/url/url_view.hpp>
#include <chrono>
#include <spdlog/fmt/chrono.h>
#include <spdlog/spdlog.h>

#include "IconsFontAwesome6.h"

using namespace std::chrono_literals;

void scenes::lobby::save_environment_json()
{
	std::stringstream ss;
	ss << "[";
	bool empty = true;
	for (const auto & model: local_environments)
	{
		if (model.builtin)
			continue;

		empty = false;
		ss << "{\"name\":" << json_string(model.name)
		   << ",\"author\":" << json_string(model.author)
		   << ",\"description\":" << json_string(model.description)
		   << ",\"screenshot\":" << json_string(model.screenshot_url)
		   << ",\"url\":" << json_string(model.gltf_url)
		   << ",\"size\":" << model.size;

		// TODO only if not the default value
		ss << ",\"local_screenshot\":" << json_string(model.local_screenshot_path.string());
		ss << ",\"local_path\":" << json_string(model.local_gltf_path.string());
		ss << "},";
	}
	if (not empty)
		ss.seekp(-1, ss.cur);
	ss << "]";

	utils::write_whole_file(application::get_config_path() / "environments.json", ss.str());
}

static std::string join_url(std::string_view base, std::string_view ref)
{
	if (base == "")
		return (std::string)ref;

	boost::urls::url base_url(base);
	boost::urls::url ref_url(ref);

	if (auto res = base_url.resolve(boost::urls::url_view{ref}); res.has_error())
	{
		// The only possible error is if base is not a base url
		return (std::string)ref;
	}

	return base_url.buffer();
}

std::vector<scenes::lobby::environment_model> scenes::lobby::load_environment_json(const std::string & json, std::string_view base_url)
{
	std::vector<scenes::lobby::environment_model> models;

	simdjson::dom::parser parser;
	simdjson::dom::element root = parser.parse(json);

	if (not root.is_array())
		return models;

	for (simdjson::dom::object i: simdjson::dom::array(root))
	{
		environment_model model;
		if (auto val = i["name"]; val.is_string())
			model.name = val.get_string().value();

		if (auto val = i["author"]; val.is_string())
			model.author = val.get_string().value();

		if (auto val = i["description"]; val.is_string())
			model.description = val.get_string().value();

		if (auto val = i["screenshot"]; val.is_string() and val.get_string().value() != "")
			model.screenshot_url = join_url(base_url, val.get_string().value());

		if (auto val = i["url"]; val.is_string())
			model.gltf_url = join_url(base_url, val.get_string().value());

		if (auto val = i["size"]; val.is_number())
			model.size = val.get_int64();

		model.builtin = false;

		if (auto val = i["local_screenshot"]; val.is_string())
			model.local_screenshot_path = val.get_string().value();
		else
			model.local_screenshot_path = application::get_config_path() / "environments" / (model.name + ".png");

		if (auto val = i["local_path"]; val.is_string())
			model.local_gltf_path = val.get_string().value();
		else
			model.local_gltf_path = application::get_config_path() / "environments" / (model.name + ".glb");

		models.push_back(std::move(model));
	}

	std::ranges::sort(models, std::less{});

	return models;
}

void scenes::lobby::download_environment(const environment_model & model, bool use_after_downloading)
{
	spdlog::info("Downloading {}", model.gltf_url);
	std::filesystem::create_directory(model.local_gltf_path.parent_path());

	download(model.gltf_url, model.local_gltf_path, [this, m = model, use_after_downloading](libcurl::curl_handle & handle) {
		std::filesystem::create_directory(m.local_screenshot_path.parent_path());
		utils::write_whole_file(m.local_screenshot_path, m.screenshot_png);

		local_environments.push_back(m);
		std::ranges::sort(local_environments, std::less{});
		save_environment_json();

		if (use_after_downloading)
			use_environment(m);
	});
}

void scenes::lobby::use_environment(const environment_model & model)
{
	if (model.local_gltf_path == "")
	{
		auto & config = application::get_config();
		config.passthrough_enabled = true;
		setup_passthrough();
		config.save();
	}
	else
	{
		load_environment_status = "";
		future_environment = utils::async<std::pair<std::string, std::shared_ptr<entt::registry>>, float>(
		        [this](auto token, std::filesystem::path path) {
			        return std::make_pair(
			                path,
			                load_gltf(path, [&](float progress) {
				                token.set_progress(progress);
			                }));
		        },
		        model.local_gltf_path);
	}
}

void scenes::lobby::update_file_picker()
{
	if (not lobby_file_picker_future.valid())
		return;

	lobby_file_picker.display();

	if (lobby_file_picker_future.wait_for(0s) != std::future_status::ready)
		return;

	try
	{
		load_environment_status = "";
		file_picker_result picked_file = lobby_file_picker_future.get();

		if (not picked_file)
			return;

		future_environment = utils::async<std::pair<std::string, std::shared_ptr<entt::registry>>, float>(
		        [this, picked_file = std::move(picked_file)](auto token) {
			        auto t = std::chrono::system_clock::now();
			        auto local_path = application::get_cache_path() / fmt::format("local-{:%F-%T}.glb", t);
			        auto local_screenshot_path = application::get_cache_path() / fmt::format("local-{:%F-%T}.png", t);

			        utils::write_whole_file(local_path, (std::span<const std::byte>)picked_file.file);

			        environment_model model{
			                .name = _S("Locally loaded environment"),
			                .author = "",
			                .description = "",
			                .screenshot_url = "",
			                .gltf_url = fmt::format("local-{:%F-%T}", t), // Used as key
			                .size = picked_file.file.size(),
			                .local_screenshot_path = local_screenshot_path,
			                .local_gltf_path = local_path,
			        };

			        int n = 2;
			        while (std::ranges::find(local_environments, model.name, &environment_model::name) != local_environments.end())
			        {
				        model.name = fmt::format(_F("Locally loaded environment ({})"), n++);
			        }

			        local_environments.push_back(model);
			        std::ranges::sort(local_environments, std::less{});
			        save_environment_json();

			        std::shared_ptr<entt::registry> env = load_gltf(local_path, [&](float progress) { token.set_progress(progress); });

			        // Create a screenshot of the loaded environment
			        scene_renderer::frame_info frame{
			                .projection = projection_matrix(
			                        XrFovf{
			                                .angleLeft = -0.7,
			                                .angleRight = 0.7,
			                                .angleUp = 0.7,
			                                .angleDown = -0.7,
			                        },
			                        constants::lobby::near_plane),
			                .view = view_matrix(XrPosef{
			                        .orientation = {0, 0, 0, 1},
			                        .position = {0, 1.6, 0},
			                })};

			        image_allocation output{
			                device,
			                vk::ImageCreateInfo{
			                        .imageType = vk::ImageType::e2D,
			                        .format = vk::Format::eR8G8B8A8Srgb,
			                        .extent = {512, 512, 1},
			                        .mipLevels = 1,
			                        .arrayLayers = 1,
			                        .usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc,
			                },
			                VmaAllocationCreateInfo{
			                        .usage = VmaMemoryUsage::VMA_MEMORY_USAGE_AUTO,
			                },
			                "Screenshot image",
			        };

			        scene_renderer local_renderer{device, physical_device, queue, queue_family_index};
			        local_renderer.start_frame();
			        local_renderer.render(
			                *env,
			                {
			                        constants::lobby::sky_color.r,
			                        constants::lobby::sky_color.g,
			                        constants::lobby::sky_color.b,
			                        constants::lobby::sky_color.a,
			                },
			                layer_lobby,
			                vk::Extent2D{
			                        output.info().extent.width,
			                        output.info().extent.height,

			                },                      // Output size
			                output.info().format,   // Output format
			                vk::Format::eD32Sfloat, // Depth format
			                output,                 // Output image
			                vk::Image{},            // Depth image
			                {&frame, 1},            // View info
			                false);
			        local_renderer.end_frame();
			        local_renderer.wait_idle(); // TODO get a semaphore from end_frame instead

			        write_image(device, queue, queue_family_index, local_screenshot_path, output);

			        return std::make_pair(local_path, env);
		        });
	}
	catch (std::exception & e)
	{
		spdlog::warn("Cannot load local environment: {}", e.what());
		load_environment_status = fmt::format(_F("Cannot load local environment: {}"), e.what());
	}
}

void scenes::lobby::delete_environment(const environment_model & to_be_deleted)
{
	std::error_code ec;
	std::filesystem::remove(to_be_deleted.local_gltf_path, ec);       // Ignore errors
	std::filesystem::remove(to_be_deleted.local_screenshot_path, ec); // Ignore errors
	unload_gltf(to_be_deleted.local_gltf_path);
	clear_texture_cache();

	if (std::erase_if(local_environments, [&](auto & model) { return model.local_gltf_path == to_be_deleted.local_gltf_path; }))
		save_environment_json();
}

void scenes::lobby::download_environment_list()
{
	downloadable_environment_list_status = "";
	downloadable_environments.clear();

	if (not try_get_download_handle(WIVRN_ENVIRONMENTS_URL))
	{
		spdlog::info("Downloading {}", WIVRN_ENVIRONMENTS_URL);
		download(WIVRN_ENVIRONMENTS_URL, [this](libcurl::curl_handle & handle) {
			try
			{
				// TODO cache in filesystem?
				downloadable_environments = load_environment_json(handle.get_response(), WIVRN_ENVIRONMENTS_URL);
			}
			catch (std::exception & e)
			{
				spdlog::error("Cannot load environment list: {}", e.what());
				downloadable_environment_list_status = fmt::format(_F("Cannot load environment list: {}"), e.what());
			}
		});
	}
}

scenes::lobby::environment_item_action scenes::lobby::environment_item(environment_model & model, bool download_screenshot)
{
	namespace ui = wivrn::ui;
	environment_item_action action = environment_item_action::none;
	const bool local = std::ranges::contains(local_environments, model.name, &environment_model::name);

	// lazily load the screenshot texture
	if (model.screenshot == 0)
	{
		if (download_screenshot)
		{
			if (model.screenshot_url != "" and not try_get_download_handle(model.screenshot_url))
			{
				spdlog::info("Downloading {}", model.screenshot_url);
				download(model.screenshot_url, [this, url = model.screenshot_url](libcurl::curl_handle & handle) {
					try
					{
						auto png = handle.get_response_bytes();
						auto tex = imgui_ctx->load_texture(png);
						for (auto & m: downloadable_environments)
							if (m.screenshot_url == url)
							{
								m.screenshot = tex;
								m.screenshot_png = std::vector<std::byte>{png.begin(), png.end()};
							}
					}
					catch (std::exception & e)
					{
						spdlog::warn("Cannot load image from {}: {}", handle.get_url(), e.what());
						for (auto & m: downloadable_environments)
							if (m.screenshot_url == url)
							{
								m.screenshot_url = "";
								m.screenshot = default_environment_screenshot;
							}
					}
				});
			}
		}
		else if (model.local_screenshot_path != "" and std::filesystem::exists(model.local_screenshot_path))
		{
			try
			{
				model.screenshot = imgui_ctx->load_texture(utils::mapped_file{model.local_screenshot_path});
			}
			catch (std::exception & e)
			{
				spdlog::warn("Cannot load screenshot {}: {}", model.local_screenshot_path.native(), e.what());
				std::error_code ec;
				std::filesystem::remove(model.local_screenshot_path, ec);
				model.local_screenshot_path = "";
				model.screenshot = default_environment_screenshot;
			}
		}
	}

	const auto & config = application::get_config();
	const bool selected = (config.passthrough_enabled and model.local_gltf_path == "") or
	                      (not config.passthrough_enabled and config.environment_model == model.local_gltf_path);

	// subtitle: author / description / size, one per line
	std::string subtitle;
	auto add_line = [&](const std::string & s) {
		if (s.empty())
			return;
		if (not subtitle.empty())
			subtitle += "\n";
		subtitle += s;
	};
	if (model.author != "")
		add_line(fmt::format(_F("Author: {}"), model.author));
	if (model.description != "")
		add_line(model.builtin ? std::string(_(model.description.c_str())) : model.description);
	if (not model.builtin)
		add_line(fmt::format(_F("Size: {:.1f} MB"), model.size * 1.0e-6));

	const char * icon = model.local_gltf_path == "" ? ICON_FA_VIDEO : (model.builtin ? ICON_FA_TABLE_CELLS : ICON_FA_IMAGE);
	const std::string title = model.builtin ? std::string(_(model.name.c_str())) : model.name;

	ImGui::PushID(model.gltf_url.c_str());
	const float bh = ImGui::GetFrameHeight() * ui::metrics::control_height;
	auto handle = try_get_download_handle(model.gltf_url);
	const bool transferring = handle and handle->get_state() == libcurl::state::transferring;

	// measure the trailing control so the row body click area can exclude it
	const std::string active_l = std::string(ICON_FA_CHECK "  ") + _("Active");
	const ImVec2 active_ts = ImGui::CalcTextSize(active_l.c_str());
	const float active_w = active_ts.x + ui::metrics::chip_padding.x * 2;
	float trailing = 0;
	if (selected)
		trailing = active_w + ui::metrics::list_row_pad;
	else if (transferring or (local and not model.builtin))
		trailing = bh + ui::metrics::list_row_pad;

	const auto row = ui::begin_list_row("##env", icon, model.screenshot, title, subtitle, selected, trailing);
	const float row_h = row.max.y - row.min.y;
	if (row.clicked and not selected)
		action = local ? environment_item_action::use_model : environment_item_action::download_model;

	const float x = row.max.x;

	if (selected)
	{
		const float ch = active_ts.y + ui::metrics::chip_padding.y * 2;
		ImGui::SetCursorScreenPos({x - active_w, row.min.y + (row_h - ch) * 0.5f});
		ui::chip(active_l, ui::chip_style::success);
	}
	else if (transferring)
	{
		ImGui::SetCursorScreenPos({x - bh, row.min.y + (row_h - bh) * 0.5f});
		if (ui::icon_button(ICON_FA_STOP, {bh, bh}, false, _S("Cancel")))
			handle->cancel();
	}
	else if (local and not model.builtin)
	{
		ImGui::SetCursorScreenPos({x - bh, row.min.y + (row_h - bh) * 0.5f});
		if (ui::icon_button(ICON_FA_TRASH, {bh, bh}, false, _S("Delete this model")))
			action = environment_item_action::delete_model;
	}

	// surface download errors the way the original did
	if (handle)
	{
		if (handle->get_state() == libcurl::state::error)
		{
			if (handle->get_curl_code() == CURLE_HTTP_RETURNED_ERROR)
				load_environment_status = fmt::format(_F("HTTP error {} when downloading {}"), handle->get_response_code(), handle->get_url());
			else
				load_environment_status = fmt::format(_F("Curl error when downloading {}\n{}: {}"), handle->get_url(), magic_enum::enum_name(handle->get_curl_code()), curl_easy_strerror(handle->get_curl_code()));
			handle->reset();
		}
		else if (handle->get_state() == libcurl::state::cancelled)
			handle->reset();
	}

	ui::end_list_row();
	ImGui::PopID();
	return action;
}

void scenes::lobby::environment_list(std::vector<environment_model> & models, bool download_screenshot)
{
	for (auto & model: models)
	{
		ImGui::BeginDisabled(model.local_gltf_path == "" and system.passthrough_supported() == xr::passthrough_type::none);
		switch (environment_item(model, download_screenshot))
		{
			case environment_item_action::none:
				break;

			case environment_item_action::download_model:
				download_environment(model, true);
				break;

			case environment_item_action::use_model:
				use_environment(model);
				break;

			case environment_item_action::delete_model:
				ImGui::OpenPopup("confirm delete model");
				environment_to_be_deleted = &model;
				break;
		}
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) and (ImGui::GetItemFlags() & ImGuiItemFlags_Disabled))
			imgui_ctx->tooltip(_("This feature is not supported by your headset"));
		ImGui::EndDisabled();
	}

	if (environment_to_be_deleted)
	{
		const std::string msg = fmt::format(_F("Permanently delete {}?"), environment_to_be_deleted->name);
		switch (wivrn::ui::confirm_modal("confirm delete model", _("Delete environment"), msg, _("Delete"), _("Cancel"), true))
		{
			case 1:
				delete_environment(*environment_to_be_deleted);
				environment_to_be_deleted = nullptr;
				break;
			case -1:
				environment_to_be_deleted = nullptr;
				break;
		}
	}
}

void scenes::lobby::popup_load_environment(XrTime predicted_display_time)
{
	if (future_environment.valid() or load_environment_status != "")
	{
		if (not ImGui::IsPopupOpen("loading environment model"))
		{
			if (popup_load_environment_display_time == 0)
				popup_load_environment_display_time = predicted_display_time + 50'000'000;
			else if (predicted_display_time > popup_load_environment_display_time)
				ImGui::OpenPopup("loading environment model");
		}

		const auto & popup_layer = imgui_ctx->layers()[1];
		const glm::vec2 popup_layer_center = popup_layer.vp_origin + popup_layer.vp_size / 2;
		ImGui::SetNextWindowPos({popup_layer_center.x, popup_layer_center.y}, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, constants::style::window_padding);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, constants::style::window_rounding);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, constants::style::window_border_size);
		if (ImGui::BeginPopupModal("loading environment model", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize))
		{
			if (future_environment.valid())
			{
				if (future_environment.poll() == utils::future_status::ready)
				{
					try
					{
						load_environment_status = "";
						auto [gltf_path, env] = future_environment.get();
						ImGui::CloseCurrentPopup();
						popup_load_environment_display_time = 0;

						// Keep the current lobby position / orientation
						auto & old_lobby_node = world.get<components::node>(lobby_entity);
						auto position = old_lobby_node.position;
						auto orientation = old_lobby_node.orientation;

						remove(lobby_entity);
						auto [new_lobby_entity, new_lobby_node] = add_gltf(env, layer_lobby);
						new_lobby_node.position = position;
						new_lobby_node.orientation = orientation;
						lobby_entity = new_lobby_entity;

						auto & config = application::get_config();

						config.passthrough_enabled = false;
						config.environment_model = gltf_path;
						setup_passthrough();
						config.save();
					}
					catch (std::exception & e)
					{
						load_environment_status = fmt::format(_F("Cannot load environment: {}"), e.what());
						spdlog::error("Cannot load environment: {}", e.what());
					}

					future_environment.reset();
				}
				else
				{
					ImGui::Text("%s", _S("Loading environment"));
					ImGui::ProgressBar(future_environment.get_progress());
				}
			}

			if (load_environment_status != "")
			{
				ImGui::Text("%s", load_environment_status.c_str());
				if (ImGui::Button(_S("Close")))
				{
					load_environment_status = "";
					ImGui::CloseCurrentPopup();
					popup_load_environment_display_time = 0;
				}
				imgui_ctx->vibrate_on_hover();
			}

			ImGui::EndPopup();
		}
		ImGui::PopStyleVar(3); // ImGuiStyleVar_WindowPadding, ImGuiStyleVar_WindowRounding, ImGuiStyleVar_WindowBorderSize
	}
}

libcurl::curl_handle * scenes::lobby::parse_environment_list()
{
	auto index_transfer = try_get_download_handle(WIVRN_ENVIRONMENTS_URL);
	if (index_transfer)
		switch (index_transfer->get_state())
		{
			case libcurl::state::cancelling:
			case libcurl::state::done:
			case libcurl::state::reset:
			case libcurl::state::transferring:
				break;

			case libcurl::state::cancelled:
				index_transfer->reset();
				break;

			case libcurl::state::error:
				if (index_transfer->get_curl_code() == CURLE_HTTP_RETURNED_ERROR)
				{
					spdlog::error("HTTP error {} when downloading index.json", index_transfer->get_response_code());
					downloadable_environment_list_status = fmt::format(
					        _F("HTTP error {} when downloading {}"),
					        index_transfer->get_response_code(),
					        "index.json");
				}
				else
				{
					spdlog::error("Curl error when downloading index.json: {}", curl_easy_strerror(index_transfer->get_curl_code()));
					downloadable_environment_list_status = fmt::format(
					        _F("Curl error when downloading {}\n{}: {}"),
					        "index.json",
					        magic_enum::enum_name(index_transfer->get_curl_code()),
					        curl_easy_strerror(index_transfer->get_curl_code()));
				}

				index_transfer->reset();
				break;
		}

	return index_transfer;
}

void scenes::lobby::gui_customize(XrTime predicted_display_time)
{
	auto & config = application::get_config();

	namespace ui = wivrn::ui;

	ui::page_header(_S("Environment"), _S("Choose the environment your panels float in."));

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {12, 10});

	ui::begin_card("##environments");
	environment_list(local_environments, false);
	ui::end_card();

	// Get more environments
	ui::begin_card("##get_more");
	{
		const float bh = ImGui::GetFrameHeight() * ui::metrics::control_height;
		const std::string browse = std::string(ICON_FA_UP_RIGHT_FROM_SQUARE "  ") + _("Browse");
		const float bw = ImGui::CalcTextSize(browse.c_str()).x + ui::metrics::button_padding.x * 2;
		const auto row = ui::begin_list_row("##getmore", ICON_FA_IMAGES, 0, _S("Get more environments"), _S("Download community-made spaces from the WiVRn dashboard on your PC."), false, bw + ui::metrics::list_row_pad);
		const float row_h = row.max.y - row.min.y;
		ImGui::SetCursorScreenPos({row.max.x - bw, row.min.y + (row_h - bh) * 0.5f});
		if (ui::button(browse, ui::button_style::secondary, {bw, 0}))
		{
			download_environment_list();
			ImGui::OpenPopup("download environment model");
		}
		ui::end_list_row();
		ui::end_card();
	}

	if (ui::button(_S("Open local glTF model"), ui::button_style::secondary))
		lobby_file_picker_future = lobby_file_picker.open();

	const auto & popup_layer = imgui_ctx->layers()[1];
	const glm::vec2 popup_layer_center = popup_layer.vp_origin + popup_layer.vp_size / 2;
	ImGui::SetNextWindowPos({popup_layer_center.x, popup_layer_center.y}, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize({1200, 900});
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, constants::style::window_rounding);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, constants::style::window_border_size);
	if (ImGui::BeginPopupModal("download environment model", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize))
	{
		auto close = _("Close");
		auto close_size = ImGui::CalcTextSize(close.c_str());

		ImGui::BeginChild("Main", ImGui::GetWindowSize() - ImGui::GetCursorPos() - ImVec2(0, close_size.y + 80) /*, ImGuiChildFlags_AlwaysUseWindowPadding*/);

		auto index_transfer = parse_environment_list();
		switch (index_transfer ? index_transfer->get_state() : libcurl::state::reset)
		{
			case libcurl::state::cancelling:
			case libcurl::state::transferring:
			case libcurl::state::cancelled:
				ImGui::SetCursorPos(ImGui::GetWindowSize() / 2 - ImVec2{200, 200} - ImGui::GetStyle().FramePadding);
				ImSpinner::SpinnerAng("index download spinner",
				                      200,                         // Radius
				                      40,                          // Thickness
				                      ImColor{1.f, 1.f, 1.f, 1.f}, // Colour
				                      ImColor{1.f, 1.f, 1.f, 0.f}, // Background
				                      6,                           // Velocity
				                      0.75f * 2 * M_PI             // Angle
				);

				break;

			case libcurl::state::error: // Should not happen, parse_environment_list resets the status in case of error
			case libcurl::state::done:  // Should not happen, parse_environment_list resets the status when done
				assert(false);
				break;

			case libcurl::state::reset:
				if (downloadable_environment_list_status == "")
				{
					environment_list(downloadable_environments, true);
				}
				else
				{
					CenterTextHV(downloadable_environment_list_status);

					ImGui::Dummy({0, constants::gui::font_size_large});
					ImGui::PushFont(nullptr, constants::gui::font_size_large);
					ImGui::PushStyleColor(ImGuiCol_Button, 0);
					ImGui::SetCursorPosX((ImGui::GetWindowSize().x - ImGui::CalcTextSize(ICON_FA_ROTATE).x) / 2 - ImGui::GetStyle().FramePadding.x);
					if (ImGui::Button(ICON_FA_ROTATE))
						download_environment_list();
					imgui_ctx->vibrate_on_hover();
					ImGui::PopStyleColor(); // ImGuiCol_Button
					ImGui::PopFont();
				}
		}

		ScrollWhenDragging();
		ImGui::EndChild();

		ImGui::SetCursorPos(ImGui::GetWindowSize() - close_size - ImVec2{50, 50});
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 0.40f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 1.00f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0.1f, 0.1f, 1.00f));
		if (ImGui::Button(_S("Close")))
			ImGui::CloseCurrentPopup();
		imgui_ctx->vibrate_on_hover();
		ImGui::PopStyleColor(3); // ImGuiCol_Button, ImGuiCol_ButtonHovered, ImGuiCol_ButtonActive

		popup_load_environment(predicted_display_time);

		ImGui::EndPopup();
	}
	else
	{
		popup_load_environment(predicted_display_time);
	}
	ImGui::PopStyleVar(2); // ImGuiStyleVar_WindowRounding, ImGuiStyleVar_WindowBorderSize

	// if (ImGui::RadioButton(_S("Custom environment"), not config.passthrough_enabled and config.environment_model_uri != ""))
	// 	lobby_file_picker_future = lobby_file_picker.open();
	// imgui_ctx->vibrate_on_hover();
	//
	// try
	// {
	// 	lobby_file_picker.display();
	//
	// 	if (lobby_file_picker_future.valid() and lobby_file_picker_future.wait_for(0s) == std::future_status::ready)
	// 	{
	// 		auto environment = lobby_file_picker_future.get();
	//
	// 		// TODO load asynchronously
	// 		// TODO allow caching
	// 		spdlog::info("Loading new environment from {}", environment.path.native());
	// 		auto t0 = std::chrono::steady_clock::now();
	// 		auto new_lobby = load_gltf(environment.file);
	// 		auto dt = std::chrono::steady_clock::now() - t0;
	// 		spdlog::info("Loaded successfully in {}", dt);
	//
	// 		remove(lobby_entity);
	// 		lobby_entity = add_gltf(new_lobby, layer_lobby).first;
	//
	// 		config.passthrough_enabled = false;
	// 		setup_passthrough();
	// 	}
	// }
	// catch (std::exception & e)
	// {
	// 	spdlog::warn("Cannot load environment: {}", e.what());
	// }

	// auto & lobby_node = world.get<components::node>(lobby_entity);
	// float scale = lobby_node.scale.x;
	// if (ImGui::SliderFloat("Environment scale", &scale, 0.1, 10, "%.3f", ImGuiSliderFlags_Logarithmic))
	// 	lobby_node.scale = glm::vec3{scale, scale, scale};
	// imgui_ctx->vibrate_on_hover();
	//
	// ImGui::SliderFloat("Environment elevation", &lobby_node.position.y, -3, 3, "%.3f");
	// imgui_ctx->vibrate_on_hover();

	ImGui::PopStyleVar(); // ImGuiStyleVar_ItemSpacing
}
