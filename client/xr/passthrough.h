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
#include "utils/handle.h"
#include <variant>

namespace xr
{
class instance;
class session;

class passthrough_layer_fb : public utils::handle<XrPassthroughLayerFB>
{
public:
	passthrough_layer_fb(instance &, session &, const XrPassthroughLayerCreateInfoFB &);
};

class passthrough_fb : public utils::handle<XrPassthroughFB>
{
	PFN_xrPassthroughStartFB xrPassthroughStartFB{};
	PFN_xrPassthroughPauseFB xrPassthroughPauseFB{};
	PFN_xrPassthroughLayerPauseFB xrPassthroughLayerPauseFB{};
	PFN_xrPassthroughLayerResumeFB xrPassthroughLayerResumeFB{};

	passthrough_layer_fb passthrough_layer;
	XrCompositionLayerPassthroughFB composition_layer;

public:
	passthrough_fb(instance &, session &);

	void start();
	void pause();
	XrCompositionLayerBaseHeader * layer()
	{
		return (XrCompositionLayerBaseHeader *)&composition_layer;
	}
};

class passthrough_htc : public utils::handle<XrPassthroughHTC>
{
	XrCompositionLayerPassthroughHTC composition_layer;

public:
	passthrough_htc(instance &, session &);
	XrCompositionLayerBaseHeader * layer()
	{
		return (XrCompositionLayerBaseHeader *)&composition_layer;
	}
};

class passthrough_alpha_blend
{
};

using passthrough = std::variant<std::monostate, passthrough_fb, passthrough_htc, passthrough_alpha_blend>;

} // namespace xr
