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

#include "escape_string.h"

QString escape_string(const QStringList & app)
{
	QStringView escaped_chars = uR"( '"\)";
	QString app_string;
	for (const QString & i: app)
	{
		for (QChar c: i)
		{
			if (not escaped_chars.contains(c))
			{
				app_string += c;
			}
			else
			{
				app_string += '\\';
				app_string += c;
			}
		}

		app_string += ' ';
	}
	app_string.resize(app_string.size() - 1);

	return app_string;
}

QStringList unescape_string(const QString & app_string)
{
	QStringList app;
	app.emplace_back();

	bool seen_backslash = false;
	bool seen_single_quote = false;
	bool seen_double_quote = false;
	for (auto c: app_string)
	{
		if (seen_backslash)
		{
			app.back() += c;
			seen_backslash = false;
		}
		else if (seen_single_quote)
		{
			if (c == '\'')
				seen_single_quote = false;
			else if (c == '\\')
				seen_backslash = true;
			else
				app.back() += c;
		}
		else if (seen_double_quote)
		{
			if (c == '"')
				seen_double_quote = false;
			else if (c == '\\')
				seen_backslash = true;
			else
				app.back() += c;
		}
		else
		{
			switch (c.unicode())
			{
				case '\\':
					seen_backslash = true;
					break;
				case '\'':
					seen_single_quote = true;
					break;
				case '"':
					seen_double_quote = true;
					break;
				case ' ':
					if (app.back() != "")
						app.emplace_back();
					break;
				default:
					app.back() += c;
			}
		}
	}

	if (app.back() == "")
		app.pop_back();

	return app;
}
