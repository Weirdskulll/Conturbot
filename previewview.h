#pragma once

#include <QColor>
#include <QImage>
#include <QSize>
#include <QVector>
#include <QWidget>
#include <functional>

#include "annotations.h"

// Превью картинки с найденными контурами и мини-редактор поверх него:
// можно нарисовать линию, стрелку, прямоугольник, эллипс и подписать текст.
// Всё нарисованное впечатывается в картинку, и бот обрабатывает это вместе с ней.
class PreviewView : public QWidget
{
    Q_OBJECT
public:
    enum class Tool { Select, Line, Arrow, Rect, Ellipse, Text };

    explicit PreviewView(QWidget* parent = nullptr);

    void setImage(const QImage& img);
    void setEditable(bool on);        // рисовать можно, только когда есть картинка и бот не занят
    void setTool(Tool tool);
    Tool tool() const { return m_tool; }

    // Оформление новых фигур. Если что-то выделено, меняется и оно.
    void setColor(const QColor& c);
    QColor color() const { return m_color; }
    void setStrokePx(double px);      // толщина в пикселях итогового рисунка
    void setFontPx(double px);        // высота шрифта в пикселях итогового рисунка
    void setFilled(bool on);
    void setDrawSize(QSize size);     // в каком размере картинка будет нарисована

    const QVector<Annotation>& annotations() const { return m_items; }
    bool hasSelection() const { return m_selected >= 0; }
    void clearAnnotations();          // новая картинка: всё стираем без истории
    void removeAll();                 // «Очистить» (можно отменить)
    void undo();
    void deleteSelected();

signals:
    void annotationsChanged();        // фигуры поменялись — пора пересчитать контуры
    void selectionChanged(bool hasSelection);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

private:
    enum class Drag { None, Create, Move, MoveA, MoveB };

    QRectF imageRect() const;                   // где картинка нарисована на виджете
    QPointF toNorm(const QPointF& widgetPos) const;
    QPointF toWidget(const QPointF& norm) const;
    QPointF snapped(Annotation::Kind kind, const QPointF& fromNorm, const QPointF& toNorm) const;
    int hitTest(const QPointF& widgetPos) const;
    Annotation styled(Annotation::Kind kind) const;
    bool editText(Annotation& a);               // false — передумали
    void setSelected(int index);
    void pushUndo();
    void commit();                              // перерисовать и сообщить об изменении
    void changeSelected(const std::function<void(Annotation&)>& change);

    QImage m_img;
    bool m_editable = false;
    Tool m_tool = Tool::Select;
    QVector<Annotation> m_items;
    QVector<QVector<Annotation>> m_undo;
    int m_selected = -1;

    QColor m_color = Qt::black;
    double m_strokePx = 3;
    double m_fontPx = 40;
    bool m_filled = false;
    QSize m_drawSize;

    Drag m_drag = Drag::None;
    QPointF m_pressNorm;
    Annotation m_draft;     // фигура, которую сейчас рисуют
    Annotation m_before;    // фигура до начала перетаскивания
    bool m_moved = false;
};
