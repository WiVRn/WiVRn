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

#include "secrets.h"
#include <cstring>
#include <type_traits>

secrets::secrets(crypto::key & my_key, crypto::key & peer_key, const std::string & pin)
{
	std::vector<uint8_t> dh = crypto::key::diffie_hellman(my_key, peer_key);
	std::vector<uint8_t> secret = crypto::pbkdf2(pin, "saltsalt", dh, sizeof(*this));

	static_assert(std::is_standard_layout_v<secrets>);
	static_assert(std::has_unique_object_representations_v<secrets>);
	memcpy(this, secret.data(), secret.size());
}
