#pragma once

#include <QDialog>
#include <QImage>
#include <QRect>
#include <QPointF>
#include <QRectF>

class RoiPickerDialog : public QDialog {
public:
    explicit RoiPickerDialog(const QImage &frame, const QRect &roi, QWidget *parent = nullptr);

    QRect selected() const { return roi_; }

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    double scale() const;
    QPointF widget_to_frame(const QPointF &pt) const;
    QPointF frame_to_widget(const QPointF &pt) const;

    QImage image_;
    QRect roi_;
    double zoom_ = 1.0;
    QPointF center_;
    bool selecting_ = false;
    bool panning_ = false;
    QPointF anchor_frame_;
    QPointF current_frame_;
    QPointF anchor_widget_;
    QPointF anchor_center_;
};
