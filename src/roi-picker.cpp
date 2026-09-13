#include "roi-picker.hpp"

#include <QDialogButtonBox>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

RoiPickerDialog::RoiPickerDialog(const QImage &frame, const QRect &roi, QWidget *parent)
    : QDialog(parent), image_(frame), roi_(roi)
{
    setWindowTitle("Select clock ROI");
    resize(1100, 700);
    center_ = QPointF(image_.width() / 2.0, image_.height() / 2.0);
    if (roi_.isValid() && !roi_.isEmpty())
        center_ = QPointF(roi_.center().x(), roi_.center().y());

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *hint = new QLabel(
        "Drag to select the clock area. Scroll to zoom, right-drag to pan, double-click to reset.",
        this);
    hint->setMargin(6);
    layout->addWidget(hint);
    layout->addStretch(1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    setMouseTracking(true);
}

double RoiPickerDialog::scale() const
{
    double fit = std::min((double)width() / image_.width(), (double)height() / image_.height());
    return fit * zoom_;
}

QPointF RoiPickerDialog::frame_to_widget(const QPointF &pt) const
{
    double s = scale();
    return QPointF((pt.x() - center_.x()) * s + width() / 2.0,
                   (pt.y() - center_.y()) * s + height() / 2.0);
}

QPointF RoiPickerDialog::widget_to_frame(const QPointF &pt) const
{
    double s = scale();
    if (s <= 0)
        return QPointF();
    return QPointF((pt.x() - width() / 2.0) / s + center_.x(),
                   (pt.y() - height() / 2.0) / s + center_.y());
}

void RoiPickerDialog::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.fillRect(rect(), QColor(30, 30, 30));

    double s = scale();
    QPointF top_left = frame_to_widget(QPointF(0, 0));
    QRectF target(top_left, QSizeF(image_.width() * s, image_.height() * s));
    p.setRenderHint(QPainter::SmoothPixmapTransform, zoom_ < 4.0);
    p.drawImage(target, image_);

    if (roi_.isValid() && !roi_.isEmpty()) {
        QRectF r(frame_to_widget(QPointF(roi_.x(), roi_.y())),
                 QSizeF(roi_.width() * s, roi_.height() * s));
        p.setPen(QPen(QColor(0, 255, 0), 2));
        p.drawRect(r);
    }
    if (selecting_) {
        QPointF a = anchor_frame_;
        QPointF b = current_frame_;
        QRectF r(QPointF(std::min(a.x(), b.x()), std::min(a.y(), b.y())),
                 QPointF(std::max(a.x(), b.x()), std::max(a.y(), b.y())));
        QRectF w(frame_to_widget(r.topLeft()), QSizeF(r.width() * s, r.height() * s));
        p.setPen(QPen(QColor(255, 220, 0), 2));
        p.drawRect(w);
    }
    p.setPen(QPen(QColor(210, 210, 210), 1));
    p.drawText(10, height() - 10,
               QString("ROI %1,%2 %3x%4   zoom %5x")
                   .arg(roi_.x())
                   .arg(roi_.y())
                   .arg(roi_.width())
                   .arg(roi_.height())
                   .arg(zoom_, 0, 'f', 1));
}

void RoiPickerDialog::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        selecting_ = true;
        anchor_frame_ = widget_to_frame(event->position());
        current_frame_ = anchor_frame_;
        update();
    } else if (event->button() == Qt::RightButton) {
        panning_ = true;
        anchor_widget_ = event->position();
        anchor_center_ = center_;
    }
}

void RoiPickerDialog::mouseMoveEvent(QMouseEvent *event)
{
    if (selecting_) {
        current_frame_ = widget_to_frame(event->position());
        update();
    } else if (panning_) {
        double s = scale();
        QPointF delta = event->position() - anchor_widget_;
        center_ = anchor_center_ - delta / s;
        update();
    }
}

void RoiPickerDialog::mouseDoubleClickEvent(QMouseEvent *event)
{
    Q_UNUSED(event);
    zoom_ = 1.0;
    center_ = QPointF(image_.width() / 2.0, image_.height() / 2.0);
    update();
}

void RoiPickerDialog::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && selecting_) {
        selecting_ = false;
        QPointF a = anchor_frame_;
        QPointF b = widget_to_frame(event->position());
        int x0 = (int)std::floor(std::min(a.x(), b.x()));
        int y0 = (int)std::floor(std::min(a.y(), b.y()));
        int x1 = (int)std::ceil(std::max(a.x(), b.x()));
        int y1 = (int)std::ceil(std::max(a.y(), b.y()));
        x0 = std::clamp(x0, 0, std::max(0, image_.width() - 1));
        y0 = std::clamp(y0, 0, std::max(0, image_.height() - 1));
        x1 = std::clamp(x1, 1, image_.width());
        y1 = std::clamp(y1, 1, image_.height());
        if (x1 - x0 >= 8 && y1 - y0 >= 8)
            roi_ = QRect(x0, y0, x1 - x0, y1 - y0);
        update();
    } else if (event->button() == Qt::RightButton) {
        panning_ = false;
    }
}

void RoiPickerDialog::wheelEvent(QWheelEvent *event)
{
    QPointF cursor = event->position();
    QPointF before = widget_to_frame(cursor);
    double factor = event->angleDelta().y() > 0 ? 1.25 : 1.0 / 1.25;
    zoom_ = std::clamp(zoom_ * factor, 1.0, 16.0);
    QPointF after = widget_to_frame(cursor);
    center_ += before - after;
    update();
}
