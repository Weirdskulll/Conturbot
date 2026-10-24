#include "annotations.h"

#include <QFont>
#include <QFontMetricsF>
#include <QImage>
#include <QLineF>
#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace {

QPointF toPixels(const QPointF& n, const QSizeF& s)
{
    return QPointF(n.x() * s.width(), n.y() * s.height());
}

QFont textFont(const Annotation& a, const QSizeF& s)
{
    QFont f(QStringLiteral("Segoe UI"));
    f.setStyleHint(QFont::SansSerif);
    f.setPixelSize(std::max(4, int(std::lround(a.fontSize * s.height()))));
    f.setWeight(QFont::DemiBold);
    return f;
}

constexpr int kTextFlags = Qt::AlignLeft | Qt::AlignTop | Qt::TextExpandTabs;

} // namespace

namespace Annotations {

double strokeWidth(const Annotation& a, const QSizeF& size)
{
    return std::max(1.0, a.width * std::max(size.width(), size.height()));
}

void paint(QPainter& p, const Annotation& a, const QSizeF& size)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    const double w = strokeWidth(a, size);
    p.setPen(QPen(a.color, w, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    const QPointF pa = toPixels(a.a, size);
    const QPointF pb = toPixels(a.b, size);

    switch (a.kind) {
    case Annotation::Line:
        p.drawLine(pa, pb);
        break;

    case Annotation::Arrow: {
        const double len = QLineF(pa, pb).length();
        if (len < 0.5)
            break;
        // наконечник растёт и с толщиной, и с длиной стрелки, иначе после обводки
        // контуром он превращается в незаметную точку
        const double head = std::min(len * 0.5, std::max({18.0, w * 5.0, len * 0.08}));
        const double angle = std::atan2(pb.y() - pa.y(), pb.x() - pa.x());
        const double spread = qDegreesToRadians(26.0);
        const QPointF left = pb - QPointF(std::cos(angle - spread), std::sin(angle - spread)) * head;
        const QPointF right = pb - QPointF(std::cos(angle + spread), std::sin(angle + spread)) * head;
        // древко заканчивается у основания наконечника, чтобы остриё оставалось острым
        p.drawLine(pa, (left + right) / 2.0);
        QPolygonF tip;
        tip << pb << left << right;
        p.setPen(QPen(a.color, std::max(1.0, w * 0.5), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(a.color);
        p.drawPolygon(tip);
        break;
    }

    case Annotation::Rect:
    case Annotation::Ellipse: {
        const QRectF r = QRectF(pa, pb).normalized();
        if (a.filled)
            p.setBrush(a.color);
        if (a.kind == Annotation::Rect)
            p.drawRect(r);
        else
            p.drawEllipse(r);
        break;
    }

    case Annotation::Text:
        p.setFont(textFont(a, size));
        p.setPen(a.color);
        p.drawText(QRectF(pa, QSizeF(1e6, 1e6)), kTextFlags, a.text);
        break;
    }
    p.restore();
}

void render(QImage& img, const QVector<Annotation>& items)
{
    if (items.isEmpty() || img.isNull())
        return;
    if (img.format() != QImage::Format_RGB32 && img.format() != QImage::Format_ARGB32_Premultiplied)
        img = img.convertToFormat(QImage::Format_RGB32);
    QPainter p(&img);
    const QSizeF size(img.size());
    for (const Annotation& a : items)
        paint(p, a, size);
}

QRectF bounds(const Annotation& a, const QSizeF& size)
{
    if (a.kind == Annotation::Text) {
        const QFontMetricsF fm(textFont(a, size));
        const QRectF r = fm.boundingRect(QRectF(0, 0, 1e6, 1e6), kTextFlags, a.text);
        return QRectF(toPixels(a.a, size), r.size());
    }
    const double w = strokeWidth(a, size);
    return QRectF(toPixels(a.a, size), toPixels(a.b, size)).normalized().adjusted(-w, -w, w, w);
}

} // namespace Annotations
