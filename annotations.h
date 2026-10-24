#pragma once

#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QVector>

class QImage;
class QPainter;

// Фигура или надпись, добавленная поверх картинки в мини-редакторе.
// Координаты нормированы (0..1 от ширины и высоты картинки), поэтому фигуры
// не съезжают, когда картинка вписывается в область другого размера.
struct Annotation {
    enum Kind { Line, Arrow, Rect, Ellipse, Text };

    Kind kind = Line;
    QPointF a;              // начало (для текста — левый верхний угол)
    QPointF b;              // конец или противоположный угол
    QColor color = Qt::black;
    double width = 0.004;   // толщина линии в долях большей стороны картинки
    bool filled = false;    // закрасить прямоугольник или эллипс
    QString text;
    double fontSize = 0.06; // высота шрифта в долях высоты картинки
};

namespace Annotations {

// Рисует фигуру, считая, что картинка занимает прямоугольник (0, 0, size)
void paint(QPainter& p, const Annotation& a, const QSizeF& size);

// Впечатывает все фигуры в картинку (после этого их обрабатывают как обычный рисунок)
void render(QImage& img, const QVector<Annotation>& items);

// Сколько места занимает фигура при данном размере картинки, в пикселях
QRectF bounds(const Annotation& a, const QSizeF& size);

// Толщина линии в пикселях при данном размере картинки
double strokeWidth(const Annotation& a, const QSizeF& size);

} // namespace Annotations
