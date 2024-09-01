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

#include <QFrame>
#include <QRectF>
#include <QVariant>
#include <vector>

struct rectangle_partitionner_private;

class rectangle_partitionner : public QFrame
{
	Q_OBJECT

	rectangle_partitionner_private * p;

public:
	explicit rectangle_partitionner(QWidget * parent = nullptr);
	rectangle_partitionner(const rectangle_partitionner &) = delete;
	rectangle_partitionner & operator=(const rectangle_partitionner &) = delete;
	~rectangle_partitionner();

	using rectangle_list = std::vector<QRectF>;
	using data_list = std::vector<QVariant>;
	using paint_function = std::function<void(QPainter & painter, QRect rect, const QVariant & data, int index, bool selected)>;

	Q_PROPERTY(rectangle_list rectangles READ rectangles WRITE set_rectangles NOTIFY rectangles_change)
	Q_PROPERTY(data_list rectangles_data READ rectangles_data WRITE set_rectangles_data)
	Q_PROPERTY(int selected_index READ selected_index WRITE set_selected_index NOTIFY selected_index_change)
	Q_PROPERTY(paint_function paint READ paint WRITE set_paint)

	const rectangle_list & rectangles() const
	{
		return m_rectangles;
	}

	int selected_index() const
	{
		return m_selected_index;
	}

	const data_list & rectangles_data() const
	{
		return m_rectangles_data;
	}

	const QVariant & rectangles_data(int index) const
	{
		return m_rectangles_data.at(index);
	}

	const paint_function & paint() const
	{
		return m_paint;
	}

	void set_rectangles(rectangle_list);
	void set_rectangles_data(const data_list &);
	void set_rectangles_data(int, QVariant);
	void set_selected_index(int);
	void set_paint(paint_function paint);

signals:
	void selected_index_change(int);
	void rectangles_change(const rectangle_list &);

protected:
	rectangle_list m_rectangles;
	int m_selected_index = 0;
	data_list m_rectangles_data;
	paint_function m_paint;

	std::vector<QRect> rectangles_position();
	QPointF from_position(QPoint position);
	QPoint to_position(QPointF position);

	void paintEvent(QPaintEvent * event) override;
	void mouseMoveEvent(QMouseEvent * event) override;
	void mousePressEvent(QMouseEvent * event) override;
	void mouseReleaseEvent(QMouseEvent * event) override;
	void leaveEvent(QEvent * event) override;

	void keyPressEvent(QKeyEvent * event) override;
};
