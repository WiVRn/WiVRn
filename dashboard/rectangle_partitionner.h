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

#pragma once

#include <KLocalization>
#include <PlatformTheme>
#include <QApplication>
#include <QCursor>
#include <QObject>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QQuickPaintedItem>
#include <platformtheme.h>
#include <qqmlengine.h>

#include "settings.h"
#include <ranges>

enum class side
{
	top,
	bottom,
	left,
	right
};

struct edge
{
	side edge_side;
	int rectangle_index;

	bool operator==(const edge & other) const noexcept
	{
		return edge_side == other.edge_side and rectangle_index == other.rectangle_index;
	}

	double position = 0;
	double min = std::numeric_limits<double>::max();
	double max = std::numeric_limits<double>::lowest();
};

namespace
{
const double split_edge_threshold = 0.05;
const double move_edge_threshold = 0.02;
const double magnetism_threshold = 0.05;
const int num_subdiv = 20;
const double epsilon = 1.0 / (2 * num_subdiv);

QRectF rectangle_from_encoder(const encoder & enc)
{
	QRectF r;
	r.setX(enc.offset_x);
	r.setY(enc.offset_y);
	r.setWidth(enc.width);
	r.setHeight(enc.height);
	return r;
}

int hovered_rectangle(QPointF position, const std::vector<encoder> & encoders)
{
	for (const auto [i, enc]: std::views::enumerate(encoders))
	{
		if (rectangle_from_encoder(enc).contains(position))
			return i;
	}

	return -1;
}

Qt::CursorShape get_cursor_shape(QPointF position, const std::vector<encoder> & rectangles)
{
	for (const encoder & i: rectangles)
	{
		QRectF r = rectangle_from_encoder(i);
		if (r.contains(position))
		{
			QPointF rel_pos = position - r.topLeft();
			double dx = std::min(rel_pos.x(), r.width() - rel_pos.x());
			double dy = std::min(rel_pos.y(), r.height() - rel_pos.y());

			if (dx < move_edge_threshold and position.x() >= epsilon and position.x() <= 1 - epsilon)
				return Qt::SizeHorCursor;
			else if (dy < move_edge_threshold and position.y() >= epsilon and position.y() <= 1 - epsilon)
				return Qt::SizeVerCursor;
			else if (dx < split_edge_threshold and position.y() >= epsilon and position.y() <= 1 - epsilon)
				return Qt::SplitVCursor;
			else if (dy < split_edge_threshold and position.x() >= epsilon and position.x() <= 1 - epsilon)
				return Qt::SplitHCursor;
			else
				return Qt::ArrowCursor;

			break;
		}
	}

	return Qt::ArrowCursor;
}

std::vector<edge> horizontal_edges(const std::vector<encoder> & rectangles)
{
	std::vector<edge> edges;

	for (const auto & [i, enc]: std::views::enumerate(rectangles))
	{
		if (enc.top() > epsilon)
		{
			edges.emplace_back(side::top, i, enc.top(), enc.left(), enc.right());
		}

		if (rectangles[i].bottom() < 1 - epsilon)
		{
			edges.emplace_back(side::bottom, i, enc.bottom(), enc.left(), enc.right());
		}
	}

	return edges;
}

std::vector<edge> vertical_edges(const std::vector<encoder> & rectangles)
{
	std::vector<edge> edges;

	for (const auto & [i, enc]: std::views::enumerate(rectangles))
	{
		if (enc.left() > epsilon)
		{
			edges.emplace_back(side::left, i, enc.left(), enc.top(), enc.bottom());
		}

		if (enc.right() < 1 - epsilon)
		{
			edges.emplace_back(side::right, i, enc.right(), enc.top(), enc.bottom());
		}
	}

	return edges;
}

std::vector<std::vector<edge>> partition_edges(std::vector<edge> edges)
{
	auto comp = [](const edge & a, const edge & b) {
		if (a.position < b.position)
			return true;
		if (a.position > b.position)
			return false;

		if (a.min < b.min)
			return true;
		if (a.min > b.min)
			return false;

		if (a.max < b.max)
			return true;
		return false;
	};

	std::ranges::sort(edges, comp);

	std::vector<std::vector<edge>> partitionned;

	double current_position = std::numeric_limits<double>::lowest();
	double current_max = 0;

	for (auto & e: edges)
	{
		if (e.position != current_position)
		{
			current_position = e.position;
			current_max = e.max;
			partitionned.push_back({e});
		}
		else
		{
			if (e.min >= current_max)
			{
				// All the following edges will be after the last partition
				current_max = e.max;
				partitionned.push_back({e});
			}
			else
			{
				// Add the edge to the current partition
				current_max = std::max(current_max, e.max);
				partitionned.back().push_back(e);
			}
		}
	}

	return partitionned;
}

std::vector<edge> hovered_edges(QPointF position, const std::vector<std::vector<edge>> & partitionned_edges, bool horizontal)
{
	if (horizontal)
		position = position.transposed();

	for (const auto & edge_list: partitionned_edges)
	{
		for (const auto & edge: edge_list)
		{
			if (std::abs(position.x() - edge.position) < move_edge_threshold and position.y() >= edge.min and position.y() <= edge.max)
			{
				return edge_list;
			}
		}
	}

	return {};
}

bool assert_rectangle_list_is_partition(const std::vector<encoder> & rectangles)
{
	return true;

	bool ok = true;

	QRectF bounding_box{0, 0, 1, 1};

	for (const auto & i: rectangles)
	{
		if (i.width < 0 or i.height < 0)
		{
			qDebug() << "Invalid rectangle" /*<< i*/;
			ok = false;
		}
	}

	std::vector<double> rectangle_y_boundaries;
	for (auto & i: rectangles)
	{
		rectangle_y_boundaries.push_back(i.top());
		rectangle_y_boundaries.push_back(i.bottom());
	}

	for (double y: rectangle_y_boundaries)
	{
		if (y < bounding_box.top())
			continue;
		if (y >= bounding_box.bottom())
			continue;

		qDebug() << "Checking line" << y;
		std::vector<std::pair<double, double>> horizontal_segments;
		for (const auto & i: rectangles)
		{
			if (y >= i.top() and y < i.bottom())
			{
				horizontal_segments.emplace_back(i.left(), i.right());
				qDebug() << "    Adding segment" << i.left() << "-" << i.right();
			}
		}

		std::ranges::sort(horizontal_segments);

		if (horizontal_segments.empty())
		{
			qDebug() << "Line" << y << "empty";
			ok = false;
		}

		if (horizontal_segments.front().first != bounding_box.left())
		{
			qDebug() << "Line" << y << "does not start at the left of the bounding box" << (horizontal_segments.front().first - bounding_box.left());
			ok = false;
		}

		if (horizontal_segments.back().second != bounding_box.right())
		{
			qDebug() << "Last rectangle of line" << y << "does not end at the right of the bounding box" << (horizontal_segments.back().second - bounding_box.right());
			ok = false;
		}

		for (int i = 1; i < horizontal_segments.size(); ++i)
		{
			if (horizontal_segments[i - 1].second < horizontal_segments[i].first)
			{
				qDebug() << "Overlap in line" << y << (horizontal_segments[i - 1].second - horizontal_segments[i].first);
				ok = false;
			}
			else if (horizontal_segments[i - 1].second > horizontal_segments[i].first)
			{
				qDebug() << "Gap in line" << y << (horizontal_segments[i - 1].second - horizontal_segments[i].first);
				ok = false;
			}
		}
	}

	return ok;
}
} // namespace

class RectanglePartitionner : public QQuickPaintedItem
{
	Q_OBJECT
	QML_ELEMENT

	Q_PROPERTY(Settings * settings READ settings WRITE setSettings NOTIFY settingsChanged)
	Q_PROPERTY(bool selected READ selected NOTIFY selectedChanged)
	Q_PROPERTY(QString encoder READ encoder WRITE setEncoder NOTIFY encoderChanged)
	Q_PROPERTY(QString codec READ codec WRITE setCodec NOTIFY codecChanged)
	Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)

	int m_currentIndex = -1;
	Settings * m_settings = nullptr;

	std::vector<edge> selection;
	std::vector<edge> hovered;

	double min_drag_position;
	double max_drag_position;

	QLine split_line;
	QRect split_clip_rect;
	Qt::CursorShape cursor_shape = Qt::ArrowCursor;

	enum class mouse_event_type
	{
		move,
		press,
		release
	};

	Kirigami::Platform::PlatformTheme theme;

public:
	RectanglePartitionner(QQuickItem * parent = nullptr) :
	        QQuickPaintedItem(parent),
	        theme(this)
	{
		setFlag(ItemHasContents);
		setAcceptedMouseButtons(Qt::LeftButton);
		setAcceptHoverEvents(true);

		theme.setColorSet(Kirigami::Platform::PlatformTheme::View);
		setFillColor(theme.backgroundColor());

		connect(&theme, &Kirigami::Platform::PlatformTheme::colorsChanged, this, [&]() {
			setFillColor(theme.backgroundColor());
			update();
		});

		// connect(this, &QQuickItem::enabledChanged, this, [&](){
		// 	if (isEnabled())
		// 		theme.setColorGroup(Kirigami::Platform::PlatformTheme::Active);
		// 	else
		// 		theme.setColorGroup(Kirigami::Platform::PlatformTheme::Disabled);
		// });
	}

	int currentIndex() const
	{
		return m_currentIndex;
	}

	bool selected() const
	{
		return m_currentIndex >= 0 and m_currentIndex < m_settings->encoders().size();
	}

	void setCurrentIndex(int value)
	{
		if (m_currentIndex != value)
		{
			m_currentIndex = value;
			currentIndexChanged(value);
			selectedChanged(selected());
			encoderChanged(encoder());
			codecChanged(codec());

			update();
		}
	}

	Settings * settings() const
	{
		return m_settings;
	}

	void onEncodersChanged()
	{
		update();
	}

	void setSettings(Settings * value)
	{
		if (m_settings != value)
		{
			if (m_settings)
				disconnect(m_settings, &Settings::encodersChanged, this, &RectanglePartitionner::onEncodersChanged);

			settingsChanged(m_settings = value);

			if (m_settings)
				connect(m_settings, &Settings::encodersChanged, this, &RectanglePartitionner::onEncodersChanged);
		}
	}

	QString encoder() const
	{
		if (not m_settings or m_currentIndex < 0 or m_currentIndex >= m_settings->encoders().size())
			return "";

		return QString::fromStdString(Settings::encoder_from_id(m_settings->encoders()[m_currentIndex].name));
	}

	QString codec() const
	{
		if (not m_settings or m_currentIndex < 0 or m_currentIndex >= m_settings->encoders().size())
			return "";

		return QString::fromStdString(Settings::codec_from_id(m_settings->encoders()[m_currentIndex].codec));
	}

	void setEncoder(QString value)
	{
		qDebug() << "Setting encoder to " << value;
		auto encoders = m_settings->encoders();
		auto converted_value = Settings::encoder_id_from_string(value.toStdString());

		if (not m_settings or m_currentIndex < 0 or m_currentIndex >= encoders.size())
			return;

		if (encoders[m_currentIndex].name == converted_value)
			return;

		encoders[m_currentIndex].name = converted_value;
		m_settings->set_encoders(encoders);
		update();
		encoderLayoutChanged();
	}

	void setCodec(QString value)
	{
		qDebug() << "Setting codec to " << value;
		auto encoders = m_settings->encoders();
		auto converted_value = Settings::codec_id_from_string(value.toStdString());

		if (not m_settings or m_currentIndex < 0 or m_currentIndex >= encoders.size())
			return;

		if (encoders[m_currentIndex].codec == converted_value)
			return;

		encoders[m_currentIndex].codec = Settings::codec_id_from_string(value.toStdString());
		m_settings->set_encoders(encoders);
		update();
		encoderLayoutChanged();
	}

	QRect frameRect()
	{
		return {0, 0, (int)width() - 1, (int)height() - 1};
	}

	QPointF from_position(QPoint position)
	{
		auto r = frameRect();

		return QPointF{
		        std::clamp(double(position.x() - r.x()) / (r.width() - 1), 0., 1.),
		        std::clamp(double(position.y() - r.y()) / (r.height() - 1), 0., 1.),
		};
	}

	QPoint to_position(QPointF position)
	{
		auto r = frameRect();

		return QPoint(
		        std::round(r.x() + position.x() * (r.width() - 1)),
		        std::round(r.y() + position.y() * (r.height() - 1)));
	}

	QRect to_position(QRectF position)
	{
		return QRect{to_position(position.topLeft()), to_position(position.bottomRight())};
	}

	QRect rectangle_position(int index)
	{
		return to_position(rectangle_from_encoder(m_settings->encoders().at(index)));
	}

	// Keep it in sync with SettingsPage.qml (codec_combo.model)
	QString codec_name_from_setting(QString value)
	{
		if (value == "h264")
			return i18n("H.264");
		if (value == "h265")
			return i18n("H.265");
		if (value == "av1")
			return i18n("AV1");

		return i18nc("automatic codec setup", "Auto");
	}

	// Keep it in sync with SettingsPage.qml (encoder_combo.model)
	QString encoder_name_from_setting(QString value)
	{
		if (value == "nvenc")
			return i18n("nvenc");
		if (value == "vaapi")
			return i18n("vaapi");
		if (value == "x264")
			return i18n("x264");
		if (value == "vulkan")
			return i18n("Vulkan");

		return i18nc("automatic encoder setup", "Auto");
	}

	// BEGIN Painting functions
	void paint_single_rectangle_border(QPainter * painter, const ::encoder & enc)
	{
		QRectF r = rectangle_from_encoder(enc);
		QRect rect = to_position(r);

		QPen pen = painter->pen();
		painter->setPen(QPen{theme.disabledTextColor()});
		painter->drawRect(rect);
		painter->setPen(pen);
	}

	void paint_single_rectangle(QPainter * painter, const ::encoder & enc, bool selected)
	{
		QRectF r = rectangle_from_encoder(enc);
		QRect rect = to_position(r);

		if (selected)
		{
			painter->fillRect(rect.adjusted(1, 1, 0, 0), theme.focusColor());
		}

		QString codec = QString::fromStdString(Settings::codec_from_id(enc.codec));
		QString encoder = QString::fromStdString(Settings::encoder_from_id(enc.name));

		QFont font = painter->font();
		QFont font2 = font;

		font2.setPixelSize(24);

		QString text = QString("%1\n%2").arg(encoder_name_from_setting(encoder), codec_name_from_setting(codec));

		QFontMetrics metrics{font2};
		QSize size = metrics.size(0, text);

		if (double ratio = std::max((double)size.width() / rect.width(), (double)size.height() / rect.height()); ratio > 1)
		{
			int pixel_size = font2.pixelSize() / ratio;
			if (pixel_size > 0)
				font2.setPixelSize(pixel_size);
		}

		QPen pen = painter->pen();

		painter->setPen(QPen{theme.textColor()});
		painter->setFont(font2);
		painter->drawText(rect, Qt::AlignCenter, text);
		painter->setFont(font);
		painter->setPen(pen);
	}

	void paint(QPainter * painter) override
	{
		if (not m_settings)
			return;

		for (auto [i, enc]: std::views::enumerate(m_settings->encoders()))
		{
			bool is_selected = i == m_currentIndex and isEnabled();
			paint_single_rectangle(painter, enc, is_selected);
		}

		if (not hovered.empty() and selection.empty())
		{
			for (auto & enc: m_settings->encoders())
			{
				paint_single_rectangle_border(painter, enc);
			}

			painter->setPen(QPen(Qt::blue, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));

			auto r = frameRect();

			QList<QLine> lines;
			lines.reserve(hovered.size());
			for (const edge & i: hovered)
			{
				QLineF line;
				switch (i.edge_side)
				{
					case side::top:
					case side::bottom:
						line = QLineF(i.min, i.position, i.max, i.position);
						break;

					case side::left:
					case side::right:
						line = QLineF(i.position, i.min, i.position, i.max);
						break;
				}

				int x1 = std::round(r.x() + line.x1() * (r.width() - 1));
				int x2 = std::round(r.x() + line.x2() * (r.width() - 1));

				int y1 = std::round(r.y() + line.y1() * (r.height() - 1));
				int y2 = std::round(r.y() + line.y2() * (r.height() - 1));

				lines.emplace_back(x1, y1, x2, y2);
			}

			painter->drawLines(lines);

			auto horz_partitionned = partition_edges(horizontal_edges(m_settings->encoders()));
			auto vert_partitionned = partition_edges(vertical_edges(m_settings->encoders()));
		}
		else if ((cursor_shape == Qt::SplitHCursor or cursor_shape == Qt::SplitVCursor) and not split_line.isNull())
		{
			auto now = std::chrono::duration_cast<std::chrono::duration<double>>(std::chrono::steady_clock::now().time_since_epoch()).count();

			QPen pen(theme.disabledTextColor(), 2, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin);
			pen.setDashPattern({5, 5});
			pen.setDashOffset(-now * 15);
			painter->setPen(pen);

			painter->setClipping(true);
			painter->setClipRect(split_clip_rect.adjusted(1, 1, 0, 0));
			painter->drawLine(split_line);
			painter->setClipping(false);
			update();

			for (auto & enc: m_settings->encoders())
			{
				paint_single_rectangle_border(painter, enc);
			}
		}
		else
		{
			for (auto & enc: m_settings->encoders())
			{
				paint_single_rectangle_border(painter, enc);
			}
		}
	}
	// END Painting functions

	// BEGIN Mouse callbacks
	void mouseMoveEvent(QMouseEvent * event) override
	{
		auto pos = event->position().toPoint();

		auto encoders = m_settings->encoders();

		if (not selection.empty())
		{
			double position;

			if (selection.front().edge_side == side::top or selection.front().edge_side == side::bottom)
				position = from_position(pos).y();
			else
				position = from_position(pos).x();

			position = std::clamp(position, min_drag_position, max_drag_position);
			position = std::round(position * num_subdiv) / num_subdiv; // magnetism

			for (edge & e: selection)
			{
				switch (e.edge_side)
				{
					case side::top:
						encoders[e.rectangle_index].set_top(position);
						break;

					case side::bottom:
						encoders[e.rectangle_index].set_bottom(position);
						break;

					case side::left:
						encoders[e.rectangle_index].set_left(position);
						break;

					case side::right:
						encoders[e.rectangle_index].set_right(position);
						break;
				}
			}

			update();
			m_settings->set_encoders(encoders);
			encoderLayoutChanged();
		}
	}

	void hoverMoveEvent(QHoverEvent * event) override
	{
		if (not m_settings)
			return;

		QPoint pos = event->position().toPoint();

		const auto & encoders = m_settings->encoders();

		hovered.clear();

		if (auto horz_edges = hovered_edges(from_position(pos), partition_edges(horizontal_edges(encoders)), true); not horz_edges.empty())
		{
			hovered = std::move(horz_edges);
			update();
		}
		else if (auto vert_edges = hovered_edges(from_position(pos), partition_edges(vertical_edges(encoders)), false); not vert_edges.empty())
		{
			hovered = std::move(vert_edges);
			update();
		}
		else
		{
			int n = hovered_rectangle(from_position(pos), encoders);
		}

		Qt::CursorShape new_cursor = get_cursor_shape(from_position(pos), encoders);
		if (new_cursor != cursor_shape)
		{
			update();
			cursor_shape = new_cursor;
			setCursor(QCursor(new_cursor));
		}

		if (cursor_shape == Qt::SplitHCursor)
		{
			int n = hovered_rectangle(from_position(pos), encoders);

			double x = std::round(from_position(pos).x() * num_subdiv) / num_subdiv; // magnetism
			int px = to_position({x, 0}).x();

			split_line = {px, rectangle_position(n).top(), px, rectangle_position(n).bottom()};
			split_clip_rect = rectangle_position(n);
			update();
		}
		else if (cursor_shape == Qt::SplitVCursor)
		{
			int n = hovered_rectangle(from_position(pos), encoders);

			double y = std::round(from_position(pos).y() * num_subdiv) / num_subdiv; // magnetism
			int py = to_position({0, y}).y();

			split_line = {rectangle_position(n).left(), py, rectangle_position(n).right(), py};
			split_clip_rect = rectangle_position(n);
			update();
		}
		else
		{
			split_line = {0, 0, 0, 0};
			split_clip_rect = {0, 0, 0, 0};
		}
	}

	void mousePressEvent(QMouseEvent * event) override
	{
		if (not m_settings)
			return;

		selection.clear();
		split_line = {0, 0, 0, 0};
		auto encoders = m_settings->encoders();

		if (auto horz_edges = hovered_edges(from_position(event->pos()), partition_edges(horizontal_edges(encoders)), true); not horz_edges.empty())
		{
			selection = std::move(horz_edges);
		}
		else if (auto vert_edges = hovered_edges(from_position(event->pos()), partition_edges(vertical_edges(encoders)), false); not vert_edges.empty())
		{
			selection = std::move(vert_edges);
		}
		else if (int n = hovered_rectangle(from_position(event->pos()), encoders); n >= 0)
		{
			::encoder & hovered = encoders[n];

			// Compute distance to edges
			auto pos = from_position(event->pos());

			double dx = std::min(pos.x() - hovered.left(), hovered.right() - pos.x());
			double dy = std::min(pos.y() - hovered.top(), hovered.bottom() - pos.y());

			if (dx < split_edge_threshold)
			{
				qDebug() << "Splitting rectangle " << n << " vertically";
				double y = std::round(pos.y() * num_subdiv) / num_subdiv; // magnetism

				// Split vertically
				::encoder new_rectangle = hovered;

				hovered.set_bottom(y);
				new_rectangle.set_top(hovered.bottom());

				encoders.push_back(new_rectangle);
				update();
				m_settings->set_encoders(encoders);
				encoderLayoutChanged();

				if (not assert_rectangle_list_is_partition(encoders))
					abort();
			}
			else if (dy < split_edge_threshold)
			{
				qDebug() << "Splitting rectangle " << n << " horizontally";
				double x = std::round(pos.x() * num_subdiv) / num_subdiv; // magnetism

				// Split horizontally
				::encoder new_rectangle = hovered;

				hovered.set_right(x);
				new_rectangle.set_left(hovered.right());

				encoders.push_back(new_rectangle);
				update();
				m_settings->set_encoders(encoders);
				encoderLayoutChanged();

				if (not assert_rectangle_list_is_partition(encoders))
					abort();
			}
			else
			{
				qDebug() << "Selecting rectangle " << n;
				setCurrentIndex(n);
			}
		}

		if (not selection.empty())
		{
			hovered.clear();

			min_drag_position = std::numeric_limits<double>::lowest();
			max_drag_position = std::numeric_limits<double>::max();

			for (auto & e: selection)
			{
				switch (e.edge_side)
				{
					case side::top:
						max_drag_position = std::min(max_drag_position, encoders[e.rectangle_index].bottom());
						break;
					case side::bottom:
						min_drag_position = std::max(min_drag_position, encoders[e.rectangle_index].top());
						break;
					case side::left:
						max_drag_position = std::min(max_drag_position, encoders[e.rectangle_index].right());
						break;
					case side::right:
						min_drag_position = std::max(min_drag_position, encoders[e.rectangle_index].left());
						break;
				}
			}
		}
	}

	void mouseReleaseEvent(QMouseEvent * event) override
	{
		if (not m_settings)
			return;

		selection.clear();
		auto encoders = m_settings->encoders();

		// Delete empty rectangles
		bool changed = false;
		for (int i = 0; i < encoders.size();)
		{
			if (encoders[i].width <= 0.0001 or encoders[i].height <= 0.0001) // TODO configurable thresholds
			{
				qDebug() << "Removing rectangle " << i;
				encoders.erase(encoders.begin() + i);
				changed = true;

				if (m_currentIndex > i)
					m_currentIndex--;
				else if (m_currentIndex == i)
				{
					m_currentIndex = -1;
				}
			}
			else
			{
				++i;
			}
		}

		if (changed)
		{
			if (not assert_rectangle_list_is_partition(encoders))
				abort();

			m_settings->set_encoders(encoders);
			setCurrentIndex(m_currentIndex);
			update();
			encoderLayoutChanged();
		}
	}

	void hoverLeaveEvent(QHoverEvent * event) override
	{
		hovered.clear();
		split_line = {0, 0, 0, 0};
		split_clip_rect = {0, 0, 0, 0};
		update();
	}
	// END Mouse callbacks

Q_SIGNALS:
	void settingsChanged(Settings *);
	void selectedChanged(bool);
	void encoderChanged(QString);
	void codecChanged(QString);
	void currentIndexChanged(int);

	void encoderLayoutChanged();
};
