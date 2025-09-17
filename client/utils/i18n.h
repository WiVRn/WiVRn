/*
 * WiVRn VR streaming
 * Copyright (C) 2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2024  Patrick Nicolas <patricknicolas@laposte.net>
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

#include <boost/locale/gnu_gettext.hpp>

#define _(x) boost::locale::gettext(x)
#define _F(x) fmt::runtime(boost::locale::gettext(x))
#define _S(x) boost::locale::gettext(x).c_str()
#define _cS(c, x) boost::locale::pgettext(c, x).c_str()

std::vector<boost::locale::gnu_gettext::messages_info> get_locales();
std::vector<char> open_locale_file(const std::string & file_name, const std::string & encoding);
