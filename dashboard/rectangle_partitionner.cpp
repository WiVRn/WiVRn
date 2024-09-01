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

#include "rectangle_partitionner.h"

#include <QColor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QStyleOptionFocusRect>

#include <chrono>
#include <cmath>
#include <limits>

namespace
{
const double split_edge_threshold = 0.05;
const double move_edge_threshold = 0.02;
const double magnetism_threshold = 0.05;
const int num_subdiv = 20;
const double epsilon = 1.0 / (2 * num_subdiv);

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
} // namespace

struct rectangle_partitionner_private
{
	std::vector<edge> selection;
	std::vector<edge> hovered;

	double min_drag_position;
	double max_drag_position;

	QLine split_line;

	enum class mouse_event_type
	{
		move,
		press,
		release
	};
};

namespace
{
int hovered_rectangle(QPointF position, std::vector<QRectF> rectangles)
{
	int n = 0;
	for (const QRectF & i: rectangles)
	{
		if (i.contains(position))
			return n;
		n++;
	}

	return -1;
}

Qt::CursorShape get_cursor_shape(QPointF position, std::vector<QRectF> rectangles)
{
	for (QRectF & i: rectangles)
	{
		if (i.contains(position))
		{
			QPointF rel_pos = position - i.topLeft();
			double dx = std::min(rel_pos.x(), i.width() - rel_pos.x());
			double dy = std::min(rel_pos.y(), i.height() - rel_pos.y());

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

std::vector<edge> horizontal_edges(std::vector<QRectF> rectangles)
{
	std::vector<edge> edges;

	for (int i = 0, n = rectangles.size(); i < n; i++)
	{
		if (rectangles[i].top() > epsilon)
		{
			edges.emplace_back(side::top, i, rectangles[i].top(), rectangles[i].left(), rectangles[i].right());
		}

		if (rectangles[i].bottom() < 1 - epsilon)
		{
			edges.emplace_back(side::bottom, i, rectangles[i].bottom(), rectangles[i].left(), rectangles[i].right());
		}
	}

	return edges;
}

std::vector<edge> vertical_edges(std::vector<QRectF> rectangles)
{
	std::vector<edge> edges;

	for (int i = 0, n = rectangles.size(); i < n; i++)
	{
		if (rectangles[i].left() > epsilon)
		{
			edges.emplace_back(side::left, i, rectangles[i].left(), rectangles[i].top(), rectangles[i].bottom());
		}

		if (rectangles[i].right() < 1 - epsilon)
		{
			edges.emplace_back(side::right, i, rectangles[i].right(), rectangles[i].top(), rectangles[i].bottom());
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

bool assert_rectangle_list_is_partition(const std::vector<QRectF> & rectangles)
{
	return true;

	bool ok = true;

	QRectF bounding_box{0, 0, 1, 1};

	for (const auto & i: rectangles)
	{
		if (i.width() < 0 or i.height() < 0)
		{
			qDebug() << "Invalid rectangle" << i;
			ok = false;
		}
	}

	std::vector<double> rectangle_y_boundaries;
	for (auto & i: rectangles)
	{
		rectangle_y_boundaries.push_back(i.top());
		rectangle_y_boundaries.push_back(i.bottom());
	}

	// for (int y = bounding_box.top(); y < bounding_box.bottom(); ++y)
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

	if (!ok)
	{
		qDebug() << "Bounding box:" << bounding_box;
		int n = 1;
		for (auto & i: rectangles)
		{
			qDebug() << "Rectangle" << (n++) << ":" << i.topLeft() << "-" << i.bottomRight();
		}
	}

	return ok;
}

} // namespace

rectangle_partitionner::rectangle_partitionner(QWidget * parent) :
        QFrame(parent)
{
	p = new rectangle_partitionner_private;

	setMouseTracking(true);

	std::vector<QRectF> encoders;

	encoders.push_back(QRectF{0, 0, 1, 1});
	set_rectangles(encoders);

	m_paint = [](QPainter & painter, QRect rect, const QVariant & data, int index, bool selected) {
		if (selected)
		{
			painter.fillRect(rect.adjusted(1, 1, 0, 0), QColorConstants::Cyan);
		}

		QString text = QString("%1").arg(index + 1);
		painter.drawText(rect, Qt::AlignCenter, text);
	};
}

rectangle_partitionner::~rectangle_partitionner()
{
	delete p;
}

void rectangle_partitionner::paintEvent(QPaintEvent * event)
{
	QFrame::paintEvent(event);

	QPainter painter(this);

	auto positions = rectangles_position();

	{
		QList<QLine> lines;
		for (int i = 1; i < num_subdiv; i++)
		{
			QPoint p1 = to_position({0, double(i) / num_subdiv});
			QPoint p2 = to_position({1, double(i) / num_subdiv});
			lines.emplace_back(p1, p2);

			QPoint p3 = to_position({double(i) / num_subdiv, 0});
			QPoint p4 = to_position({double(i) / num_subdiv, 1});
			lines.emplace_back(p3, p4);
		}

		QPen pen = painter.pen();
		painter.setPen(QPen(Qt::lightGray, 1, Qt::SolidLine));
		painter.drawLines(lines);
		painter.setPen(pen);
	}

	if (m_paint)
	{
		int n = 0;
		for (QRect & i: positions)
		{
			painter.drawRect(i);
			bool is_selected = n == m_selected_index and isEnabled();
			m_paint(painter, i, m_rectangles_data[n], n, is_selected);

			n++;
		}
	}

	if (not p->hovered.empty() and p->selection.empty())
	{
		painter.setPen(QPen(Qt::blue, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));

		auto r = frameRect();
		r.adjust(frameWidth(), frameWidth(), -2 * frameWidth(), -2 * frameWidth());

		QList<QLine> lines;
		lines.reserve(p->hovered.size());
		for (const edge & i: p->hovered)
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

		painter.drawLines(lines);

		auto horz_partitionned = partition_edges(horizontal_edges(m_rectangles));
		auto vert_partitionned = partition_edges(vertical_edges(m_rectangles));
	}
	else if ((cursor() == Qt::SplitHCursor or cursor() == Qt::SplitVCursor) and not p->split_line.isNull())
	{
		auto now = std::chrono::duration_cast<std::chrono::duration<double>>(std::chrono::steady_clock::now().time_since_epoch()).count();

		QPen pen(Qt::black, 2, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin);
		pen.setDashPattern({5, 5});
		pen.setDashOffset(-now * 15);
		painter.setPen(pen);

		painter.drawLine(p->split_line);
		update();
	}
}

void rectangle_partitionner::mouseMoveEvent(QMouseEvent * event)
{
	assert(m_rectangles.size() == m_rectangles_data.size());

	auto r = frameRect();
	r.adjust(frameWidth(), frameWidth(), -2 * frameWidth(), -2 * frameWidth());

	if (not p->selection.empty())
	{
		double position;

		if (p->selection.front().edge_side == side::top or p->selection.front().edge_side == side::bottom)
			position = from_position(event->pos()).y();
		else
			position = from_position(event->pos()).x();

		position = std::clamp(position, p->min_drag_position, p->max_drag_position);
		position = std::round(position * num_subdiv) / num_subdiv; // magnetism

		for (edge & e: p->selection)
		{
			switch (e.edge_side)
			{
				case side::top:
					m_rectangles[e.rectangle_index].setTop(position);
					break;

				case side::bottom:
					m_rectangles[e.rectangle_index].setBottom(position);
					break;

				case side::left:
					m_rectangles[e.rectangle_index].setLeft(position);
					break;

				case side::right:
					m_rectangles[e.rectangle_index].setRight(position);
					break;
			}
		}

		// if (not assert_rectangle_list_is_partition(m_rectangles))
		// {
		// 	abort();
		// }

		update();
		rectangles_change(m_rectangles);
	}
	else
	{
		p->hovered.clear();

		if (auto horz_edges = hovered_edges(from_position(event->pos()), partition_edges(horizontal_edges(m_rectangles)), true); not horz_edges.empty())
		{
			p->hovered = std::move(horz_edges);
			update();
		}
		else if (auto vert_edges = hovered_edges(from_position(event->pos()), partition_edges(vertical_edges(m_rectangles)), false); not vert_edges.empty())
		{
			p->hovered = std::move(vert_edges);
			update();
		}
		else
		{
			int n = hovered_rectangle(from_position(event->pos()), m_rectangles);
		}

		auto new_cursor = get_cursor_shape(from_position(event->pos()), m_rectangles);
		if (new_cursor != cursor())
		{
			update();
			setCursor(new_cursor);
		}
	}

	if (cursor() == Qt::SplitHCursor)
	{
		int n = hovered_rectangle(from_position(event->pos()), m_rectangles);

		double x = std::round(from_position(event->pos()).x() * num_subdiv) / num_subdiv; // magnetism
		int px = to_position({x, 0}).x();

		p->split_line = {px, rectangles_position()[n].top(), px, rectangles_position()[n].bottom()};
		update();
	}
	else if (cursor() == Qt::SplitVCursor)
	{
		int n = hovered_rectangle(from_position(event->pos()), m_rectangles);

		double y = std::round(from_position(event->pos()).y() * num_subdiv) / num_subdiv; // magnetism
		int py = to_position({0, y}).y();

		p->split_line = {rectangles_position()[n].left(), py, rectangles_position()[n].right(), py};
		update();
	}
	else
	{
		p->split_line = {0, 0, 0, 0};
	}
}

void rectangle_partitionner::mousePressEvent(QMouseEvent * event)
{
	assert(m_rectangles.size() == m_rectangles_data.size());

	auto r = frameRect();
	r.adjust(frameWidth(), frameWidth(), -2 * frameWidth(), -2 * frameWidth());

	p->selection.clear();
	p->split_line = {0, 0, 0, 0};

	if (auto horz_edges = hovered_edges(from_position(event->pos()), partition_edges(horizontal_edges(m_rectangles)), true); not horz_edges.empty())
	{
		p->selection = std::move(horz_edges);
	}
	else if (auto vert_edges = hovered_edges(from_position(event->pos()), partition_edges(vertical_edges(m_rectangles)), false); not vert_edges.empty())
	{
		p->selection = std::move(vert_edges);
	}
	else if (int n = hovered_rectangle(from_position(event->pos()), m_rectangles); n >= 0)
	{
		QRectF & hovered = m_rectangles[n];

		// Compute distance to edges
		auto pos = from_position(event->pos());

		double dx = std::min(pos.x() - hovered.left(), hovered.right() - pos.x());
		double dy = std::min(pos.y() - hovered.top(), hovered.bottom() - pos.y());

		if (dx < split_edge_threshold)
		{
			double y = std::round(pos.y() * num_subdiv) / num_subdiv; // magnetism

			// Split vertically
			QRectF new_rectangle = hovered;

			hovered.setBottom(y);
			new_rectangle.setTop(hovered.bottom());

			m_rectangles.push_back(new_rectangle);
			m_rectangles_data.push_back(m_rectangles_data[n]);
			update();
			rectangles_change(m_rectangles);

			if (not assert_rectangle_list_is_partition(m_rectangles))
				abort();
		}
		else if (dy < split_edge_threshold)
		{
			double x = std::round(pos.x() * num_subdiv) / num_subdiv; // magnetism

			// Split horizontally
			QRectF new_rectangle = hovered;

			hovered.setRight(x);
			new_rectangle.setLeft(hovered.right());

			m_rectangles.push_back(new_rectangle);
			m_rectangles_data.push_back(m_rectangles_data[n]);
			update();
			rectangles_change(m_rectangles);

			if (not assert_rectangle_list_is_partition(m_rectangles))
				abort();
		}
		else
		{
			set_selected_index(n);
		}
	}

	if (not p->selection.empty())
	{
		p->hovered.clear();

		p->min_drag_position = std::numeric_limits<double>::lowest();
		p->max_drag_position = std::numeric_limits<double>::max();

		for (auto & e: p->selection)
		{
			switch (e.edge_side)
			{
				case side::top:
					p->max_drag_position = std::min(p->max_drag_position, m_rectangles[e.rectangle_index].bottom());
					break;
				case side::bottom:
					p->min_drag_position = std::max(p->min_drag_position, m_rectangles[e.rectangle_index].top());
					break;
				case side::left:
					p->max_drag_position = std::min(p->max_drag_position, m_rectangles[e.rectangle_index].right());
					break;
				case side::right:
					p->min_drag_position = std::max(p->min_drag_position, m_rectangles[e.rectangle_index].left());
					break;
			}
		}
	}
	assert(m_rectangles.size() == m_rectangles_data.size());
}

void rectangle_partitionner::mouseReleaseEvent(QMouseEvent * event)
{
	assert(m_rectangles.size() == m_rectangles_data.size());

	p->selection.clear();

	// Delete empty rectangles
	auto i1 = m_rectangles.begin();
	auto i3 = m_rectangles_data.begin();

	assert(m_rectangles.size() == m_rectangles_data.size());

	bool changed = false;
	for (; i1 != m_rectangles.end();)
	{
		// if (i2->width() <= 1 or i2->height() <= 1)
		if (i1->width() <= 0.0001 or i1->height() <= 0.0001) // TODO configurable thresholds
		{
			i1 = m_rectangles.erase(i1);
			i3 = m_rectangles_data.erase(i3);
			changed = true;
		}
		else
		{
			++i1;
			++i3;
		}
	}
	assert(m_rectangles.size() == m_rectangles_data.size());

	if (changed)
	{
		if (not assert_rectangle_list_is_partition(m_rectangles))
			abort();

		rectangles_change(m_rectangles);
		update();
	}
}

void rectangle_partitionner::leaveEvent(QEvent * event)
{
	p->split_line = {0, 0, 0, 0};
}

void rectangle_partitionner::keyPressEvent(QKeyEvent * event)
{
	auto r = frameRect();
	r.adjust(frameWidth(), frameWidth(), -2 * frameWidth(), -2 * frameWidth());
	qDebug() << "Bounding box:" << r;
	int n = 1;
	for (auto & i: m_rectangles)
	{
		qDebug() << "Rectangle" << (n++) << ":" << i.topLeft() << "-" << i.bottomRight();
	}
}

void rectangle_partitionner::set_rectangles(rectangle_list value)
{
	assert(m_rectangles.size() == m_rectangles_data.size());
	auto old_value = std::move(m_rectangles);

	m_rectangles = std::move(value);

	for (auto & i: m_rectangles)
	{
		i.setBottom(std::round(i.bottom() * num_subdiv) / num_subdiv);
		i.setTop(std::round(i.top() * num_subdiv) / num_subdiv);
		i.setLeft(std::round(i.left() * num_subdiv) / num_subdiv);
		i.setRight(std::round(i.right() * num_subdiv) / num_subdiv);
	}

	if (not assert_rectangle_list_is_partition(m_rectangles))
	{
		m_rectangles = std::move(old_value);

		throw std::runtime_error("Invalid partitionning");
	}

	m_rectangles_data.resize(m_rectangles.size());

	if (m_selected_index >= m_rectangles.size())
		set_selected_index(m_rectangles.size() - 1);

	update();
	rectangles_change(m_rectangles);
	assert(m_rectangles.size() == m_rectangles_data.size());
}

void rectangle_partitionner::set_selected_index(int new_index)
{
	new_index = std::clamp<int>(new_index, 0, m_rectangles.size());

	if (new_index != m_selected_index)
	{
		m_selected_index = new_index;
		update();

		selected_index_change(new_index);
	}
}

void rectangle_partitionner::set_rectangles_data(const data_list & value)
{
	assert(m_rectangles.size() == m_rectangles_data.size());
	m_rectangles_data = value;
	m_rectangles_data.resize(m_rectangles.size());
	assert(m_rectangles.size() == m_rectangles_data.size());

	update();
}

void rectangle_partitionner::set_rectangles_data(int index, QVariant value)
{
	assert(m_rectangles.size() == m_rectangles_data.size());
	assert(index < m_rectangles_data.size());
	m_rectangles_data[index] = value;
	assert(m_rectangles.size() == m_rectangles_data.size());

	update();
}

void rectangle_partitionner::set_paint(paint_function paint)
{
	m_paint = paint;
	update();
}

std::vector<QRect> rectangle_partitionner::rectangles_position()
{
	std::vector<QRect> positions;
	positions.reserve(m_rectangles.size());

	auto r = frameRect();
	r.adjust(frameWidth(), frameWidth(), -2 * frameWidth(), -2 * frameWidth());

	for (QRectF & i: m_rectangles)
	{
		int x1 = std::round(r.x() + i.x() * (r.width() - 1));
		int x2 = std::round(r.x() + (i.x() + i.width()) * (r.width() - 1));

		int y1 = std::round(r.y() + i.y() * (r.height() - 1));
		int y2 = std::round(r.y() + (i.y() + i.height()) * (r.height() - 1));
		positions.emplace_back(x1, y1, x2 - x1 + 1, y2 - y1 + 1);
	}
	assert(m_rectangles.size() == positions.size());

	return positions;
}

QPointF rectangle_partitionner::from_position(QPoint position)
{
	auto r = frameRect();
	r.adjust(frameWidth(), frameWidth(), -2 * frameWidth(), -2 * frameWidth());

	return QPointF{
	        std::clamp(double(position.x() - r.x()) / (r.width() - 1), 0., 1.),
	        std::clamp(double(position.y() - r.y()) / (r.height() - 1), 0., 1.),
	};
}

QPoint rectangle_partitionner::to_position(QPointF position)
{
	auto r = frameRect();
	r.adjust(frameWidth(), frameWidth(), -2 * frameWidth(), -2 * frameWidth());

	return QPoint(
	        std::round(r.x() + position.x() * (r.width() - 1)),
	        std::round(r.y() + position.y() * (r.height() - 1)));
}

#include "moc_rectangle_partitionner.cpp"
