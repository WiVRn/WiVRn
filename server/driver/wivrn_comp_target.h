// Copyright 2022, Guillaume Meunier
// Copyright 2022, Patrick Nicolas
// SPDX-License-Identifier: BSL-1.0

#pragma once

#include "main/comp_target.h"
#include "wivrn_session.h"

comp_target *
comp_target_wivrn_create(std::shared_ptr<xrt::drivers::wivrn::wivrn_session> cnx, float fps);
