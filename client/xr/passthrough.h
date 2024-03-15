/*
 * WiVRn VR streaming
 * Copyright (C) 2022  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "openxr/openxr.h"
#include "xr.h"
#include <variant>

namespace xr
{
class session;

class passthrough_fb : public utils::handle<XrPassthroughFB>
{
	PFN_xrDestroyPassthroughFB xrDestroyPassthroughFB{};
	PFN_xrDestroyPassthroughLayerFB xrDestroyPassthroughLayerFB{};
	PFN_xrPassthroughStartFB xrPassthroughStartFB{};
	PFN_xrPassthroughPauseFB xrPassthroughPauseFB{};
	PFN_xrPassthroughLayerPauseFB xrPassthroughLayerPauseFB{};
	PFN_xrPassthroughLayerResumeFB xrPassthroughLayerResumeFB{};

	XrPassthroughLayerFB passthrough_layer{};
	XrCompositionLayerPassthroughFB composition_layer;

public:
	passthrough_fb() = default;
	passthrough_fb(instance &, session &);
	passthrough_fb(passthrough_fb &&) = default;
	passthrough_fb(const passthrough_fb &) = delete;
	passthrough_fb & operator=(passthrough_fb &&) = default;
	passthrough_fb & operator=(const passthrough_fb &) = delete;
	~passthrough_fb();

	void start();
	void pause();
	XrCompositionLayerBaseHeader * layer() { return (XrCompositionLayerBaseHeader*)&composition_layer; }
};


class passthrough_htc : public utils::handle<XrPassthroughHTC>
{
	PFN_xrDestroyPassthroughHTC xrDestroyPassthroughHTC{};

	XrCompositionLayerPassthroughHTC composition_layer;
public:
	passthrough_htc() = default;
	passthrough_htc(instance &, session &);
	passthrough_htc(passthrough_htc &&) = default;
	passthrough_htc(const passthrough_htc &) = delete;
	passthrough_htc & operator=(passthrough_htc &&) = default;
	passthrough_htc & operator=(const passthrough_htc &) = delete;
	~passthrough_htc();

	void start() {}
	void pause() {}
	XrCompositionLayerBaseHeader * layer() { return (XrCompositionLayerBaseHeader*)&composition_layer; }
};

using passthrough = std::variant<passthrough_fb, passthrough_htc>;

} // namespace xr
