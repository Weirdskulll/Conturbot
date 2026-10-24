#include "previewview.h"

#include <QFont>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace {

constexpr int kMaxUndo = 60;
const QColor kAccent(75, 83, 200);

QPointF eventPos(QMouseEvent* e)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return e->position();
#else
    return e->localPos();
#endif
}

// Расстояние от точки до отрезка
double distanceToSegment(const QPointF& p, const QPointF& a, const QPointF& b)
{
    const QPointF ab = b - a;
    const double len2 = ab.x() * ab.x() + ab.y() * ab.y();
    double t = len2 > 0 ? ((p.x() - a.x()) * ab.x() + (p.y() - a.y()) * ab.y()) / len2 : 0;
    t = std::clamp(t, 0.0, 1.0);
    return QLineF(p, a + ab * t).length();
}

Annotation::Kind kindFor(PreviewView::Tool tool)
{
    switch (tool) {
    case PreviewView::Tool::Arrow: return Annotation::Arrow;
    case PreviewView::Tool::Rect: return Annotation::Rect;
    case PreviewView::Tool::Ellipse: return Annotation::Ellipse;
    case PreviewView::Tool::Text: return Annotation::Text;
    default: return Annotation::Line;
    }
}

} // namespace

PreviewView::PreviewView(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(360, 280);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setFocusPolicy(Qt::ClickFocus);
    setMouseTracking(true);
}

void PreviewView::setImage(const QImage& img)
{
    m_img = img;
    update();
}

void PreviewView::setEditable(bool on)
{
    m_editable = on;
    if (!on) {
        m_drag = Drag::None;
        setSelected(-1);
    }
    setCursor(on && m_tool != Tool::Select ? Qt::CrossCursor : Qt::ArrowCursor);
    update();
}

void PreviewView::setTool(Tool tool)
{
    m_tool = tool;
    m_drag = Drag::None;
    if (tool != Tool::Select)
        setSelected(-1);
    setCursor(m_editable && tool != Tool::Select ? Qt::CrossCursor : Qt::ArrowCursor);
    update();
}

// ------------------------------------------------------------ оформление

void PreviewView::changeSelected(const std::function<void(Annotation&)>& change)
{
    if (m_selected < 0 || m_selected >= m_items.size())
        return;
    pushUndo();
    change(m_items[m_selected]);
    commit();
}

void PreviewView::setColor(const QColor& c)
{
    m_color = c;
    changeSelected([c](Annotation& a) { a.color = c; });
}

void PreviewView::setStrokePx(double px)
{
    m_strokePx = px;
    const Annotation sample = styled(Annotation::Line);
    changeSelected([&sample](Annotation& a) {
        if (a.kind != Annotation::Text)
            a.width = sample.width;
    });
}

void PreviewView::setFontPx(double px)
{
    m_fontPx = px;
    const Annotation sample = styled(Annotation::Text);
    changeSelected([&sample](Annotation& a) {
        if (a.kind == Annotation::Text)
            a.fontSize = sample.fontSize;
    });
}

void PreviewView::setFilled(bool on)
{
    m_filled = on;
    changeSelected([on](Annotation& a) { a.filled = on; });
}

void PreviewView::setDrawSize(QSize size)
{
    m_drawSize = size;
}

Annotation PreviewView::styled(Annotation::Kind kind) const
{
    Annotation a;
    a.kind = kind;
    a.color = m_color;
    a.filled = m_filled;
    // толщину и шрифт задают в пикселях итогового рисунка, а храним в долях картинки
    const QSizeF size = (m_drawSize.isValid() && !m_drawSize.isEmpty()) ? QSizeF(m_drawSize) : QSizeF(m_img.size());
    const double longSide = std::max(1.0, std::max(size.width(), size.height()));
    a.width = m_strokePx / longSide;
    a.fontSize = m_fontPx / std::max(1.0, size.height());
    return a;
}

// ------------------------------------------------------------ список фигур

void PreviewView::clearAnnotations()
{
    m_items.clear();
    m_undo.clear();
    m_drag = Drag::None;
    setSelected(-1);
    update();
}

void PreviewView::removeAll()
{
    if (m_items.isEmpty())
        return;
    pushUndo();
    m_items.clear();
    setSelected(-1);
    commit();
}

void PreviewView::undo()
{
    if (m_undo.isEmpty())
        return;
    m_items = m_undo.takeLast();
    m_drag = Drag::None;
    setSelected(-1);
    commit();
}

void PreviewView::deleteSelected()
{
    if (m_selected < 0 || m_selected >= m_items.size())
        return;
    pushUndo();
    m_items.removeAt(m_selected);
    setSelected(-1);
    commit();
}

void PreviewView::pushUndo()
{
    m_undo.push_back(m_items);
    if (m_undo.size() > kMaxUndo)
        m_undo.removeFirst();
}

void PreviewView::commit()
{
    update();
    emit annotationsChanged();
}

void PreviewView::setSelected(int index)
{
    if (index == m_selected)
        return;
    m_selected = index;
    emit selectionChanged(index >= 0);
    update();
}

// ------------------------------------------------------------ геометрия

QRectF PreviewView::imageRect() const
{
    if (m_img.isNull())
        return QRectF();
    const QRect area = rect().adjusted(16, 16, -16, -16);
    QSize s = m_img.size();
    s.scale(area.size(), Qt::KeepAspectRatio);
    QRect target(QPoint(0, 0), s);
    target.moveCenter(area.center());
    return QRectF(target);
}

QPointF PreviewView::toNorm(const QPointF& widgetPos) const
{
    const QRectF r = imageRect();
    if (r.isEmpty())
        return QPointF();
    const QPointF n((widgetPos.x() - r.left()) / r.width(), (widgetPos.y() - r.top()) / r.height());
    return QPointF(std::clamp(n.x(), 0.0, 1.0), std::clamp(n.y(), 0.0, 1.0));
}

QPointF PreviewView::toWidget(const QPointF& norm) const
{
    const QRectF r = imageRect();
    return QPointF(r.left() + norm.x() * r.width(), r.top() + norm.y() * r.height());
}

// С зажатым Shift линии ложатся под углом, кратным 45°, а фигуры становятся квадратом или кругом
QPointF PreviewView::snapped(Annotation::Kind kind, const QPointF& fromNorm, const QPointF& toNormPos) const
{
    const QRectF r = imageRect();
    const QPointF a(fromNorm.x() * r.width(), fromNorm.y() * r.height());
    const QPointF b(toNormPos.x() * r.width(), toNormPos.y() * r.height());
    QPointF d = b - a;
    if (kind == Annotation::Line || kind == Annotation::Arrow) {
        const double len = std::hypot(d.x(), d.y());
        const double step = M_PI / 4;
        const double angle = std::round(std::atan2(d.y(), d.x()) / step) * step;
        d = QPointF(std::cos(angle), std::sin(angle)) * len;
    } else {
        const double side = std::max(std::abs(d.x()), std::abs(d.y()));
        d = QPointF(d.x() < 0 ? -side : side, d.y() < 0 ? -side : side);
    }
    const QPointF res = a + d;
    return QPointF(std::clamp(res.x() / r.width(), 0.0, 1.0), std::clamp(res.y() / r.height(), 0.0, 1.0));
}

int PreviewView::hitTest(const QPointF& widgetPos) const
{
    const QRectF r = imageRect();
    const QSizeF size = r.size();
    const QPointF local = widgetPos - r.topLeft();
    for (int i = m_items.size() - 1; i >= 0; --i) { // сверху вниз: последняя нарисованная — сверху
        const Annotation& a = m_items[i];
        if (a.kind == Annotation::Line || a.kind == Annotation::Arrow) {
            const QPointF pa(a.a.x() * size.width(), a.a.y() * size.height());
            const QPointF pb(a.b.x() * size.width(), a.b.y() * size.height());
            const double tolerance = std::max(6.0, Annotations::strokeWidth(a, size) / 2 + 4);
            if (distanceToSegment(local, pa, pb) <= tolerance)
                return i;
        } else if (Annotations::bounds(a, size).adjusted(-4, -4, 4, 4).contains(local)) {
            return i;
        }
    }
    return -1;
}

bool PreviewView::editText(Annotation& a)
{
    bool ok = false;
    const QString text = QInputDialog::getMultiLineText(
        this, QStringLiteral("Текст"), QStringLiteral("Что написать (можно в несколько строк):"), a.text, &ok);
    if (!ok || text.trimmed().isEmpty())
        return false;
    a.text = text;
    return true;
}

// ------------------------------------------------------------ рисование

void PreviewView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), palette().color(QPalette::Window).darker(112));
    if (m_img.isNull()) {
        p.setPen(palette().color(QPalette::WindowText));
        QFont f = font();
        f.setPointSize(f.pointSize() + 2);
        p.setFont(f);
        p.drawText(rect().adjusted(24, 24, -24, -24), Qt::AlignCenter | Qt::TextWordWrap,
                   QStringLiteral("Перетащи сюда картинку,\nнажми «Открыть…» или Ctrl+V\n\n"
                                  "или создай «Чистый холст» и рисуй сам"));
        return;
    }
    const QRectF target = imageRect();
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.fillRect(target.translated(3, 3), QColor(0, 0, 0, 40)); // лёгкая тень
    p.drawImage(target, m_img);
    p.setPen(QColor(0, 0, 0, 60));
    p.drawRect(target.adjusted(0, 0, -1, -1));

    // Пока контуры пересчитываются, рисуемую или перетаскиваемую фигуру показываем сразу
    p.save();
    p.translate(target.topLeft());
    p.setClipRect(QRectF(QPointF(0, 0), target.size()));
    if (m_drag == Drag::Create)
        Annotations::paint(p, m_draft, target.size());
    else if (m_drag != Drag::None && m_selected >= 0 && m_selected < m_items.size())
        Annotations::paint(p, m_items[m_selected], target.size());
    p.setClipping(false);

    if (m_selected >= 0 && m_selected < m_items.size()) {
        const Annotation& a = m_items[m_selected];
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(kAccent, 1.5, Qt::DashLine));
        if (a.kind == Annotation::Line || a.kind == Annotation::Arrow) {
            p.drawLine(toWidget(a.a) - target.topLeft(), toWidget(a.b) - target.topLeft());
        } else {
            p.drawRect(Annotations::bounds(a, target.size()).adjusted(-3, -3, 3, 3));
        }
        // ручки: за концы линии или угол фигуры можно потянуть
        if (a.kind != Annotation::Text) {
            p.setPen(QPen(Qt::white, 1));
            p.setBrush(kAccent);
            for (const QPointF& n : {a.a, a.b})
                p.drawRect(QRectF(toWidget(n) - target.topLeft() - QPointF(4, 4), QSizeF(8, 8)));
        }
    }
    p.restore();
}

// ------------------------------------------------------------ мышь и клавиатура

void PreviewView::mousePressEvent(QMouseEvent* e)
{
    if (!m_editable || m_img.isNull() || e->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(e);
        return;
    }
    setFocus();
    const QPointF pos = eventPos(e);
    m_pressNorm = toNorm(pos);
    m_moved = false;

    if (m_tool == Tool::Select) {
        // сначала ручки уже выделенной фигуры
        if (m_selected >= 0 && m_items[m_selected].kind != Annotation::Text) {
            if (QLineF(pos, toWidget(m_items[m_selected].a)).length() <= 8)
                m_drag = Drag::MoveA;
            else if (QLineF(pos, toWidget(m_items[m_selected].b)).length() <= 8)
                m_drag = Drag::MoveB;
        }
        if (m_drag == Drag::None) {
            const int hit = hitTest(pos);
            setSelected(hit);
            if (hit >= 0)
                m_drag = Drag::Move;
        }
        if (m_drag != Drag::None)
            m_before = m_items[m_selected];
        update();
        return;
    }

    if (m_tool == Tool::Text) {
        Annotation a = styled(Annotation::Text);
        a.a = a.b = m_pressNorm;
        if (editText(a)) {
            pushUndo();
            m_items.push_back(a);
            setSelected(m_items.size() - 1);
            commit();
        }
        return;
    }

    m_draft = styled(kindFor(m_tool));
    m_draft.a = m_draft.b = m_pressNorm;
    m_drag = Drag::Create;
    update();
}

void PreviewView::mouseMoveEvent(QMouseEvent* e)
{
    if (m_drag == Drag::None) {
        if (m_editable && m_tool == Tool::Select)
            setCursor(hitTest(eventPos(e)) >= 0 ? Qt::SizeAllCursor : Qt::ArrowCursor);
        return;
    }
    const QPointF n = toNorm(eventPos(e));
    const bool shift = e->modifiers().testFlag(Qt::ShiftModifier);
    switch (m_drag) {
    case Drag::Create:
        m_draft.b = shift ? snapped(m_draft.kind, m_draft.a, n) : n;
        break;
    case Drag::Move: {
        const QPointF d = n - m_pressNorm;
        m_items[m_selected].a = m_before.a + d;
        m_items[m_selected].b = m_before.b + d;
        m_moved = true;
        break;
    }
    case Drag::MoveA:
        m_items[m_selected].a = shift ? snapped(m_before.kind, m_before.b, n) : n;
        m_moved = true;
        break;
    case Drag::MoveB:
        m_items[m_selected].b = shift ? snapped(m_before.kind, m_before.a, n) : n;
        m_moved = true;
        break;
    case Drag::None:
        break;
    }
    update();
}

void PreviewView::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton || m_drag == Drag::None) {
        QWidget::mouseReleaseEvent(e);
        return;
    }
    const Drag drag = m_drag;
    m_drag = Drag::None;

    if (drag == Drag::Create) {
        const QLineF diag(toWidget(m_draft.a), toWidget(m_draft.b));
        if (diag.length() >= 4) { // случайный клик без движения фигуру не создаёт
            pushUndo();
            m_items.push_back(m_draft);
            setSelected(m_items.size() - 1);
            commit();
        }
        update();
        return;
    }

    if (m_moved && m_selected >= 0) {
        // в историю кладём состояние до перетаскивания
        QVector<Annotation> before = m_items;
        before[m_selected] = m_before;
        m_undo.push_back(before);
        if (m_undo.size() > kMaxUndo)
            m_undo.removeFirst();
        commit();
    }
    m_moved = false;
    update();
}

void PreviewView::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (!m_editable || m_tool != Tool::Select || e->button() != Qt::LeftButton) {
        QWidget::mouseDoubleClickEvent(e);
        return;
    }
    const int hit = hitTest(eventPos(e));
    if (hit < 0 || m_items[hit].kind != Annotation::Text)
        return;
    m_drag = Drag::None;
    Annotation edited = m_items[hit];
    if (editText(edited) && edited.text != m_items[hit].text) {
        pushUndo();
        m_items[hit] = edited;
        setSelected(hit);
        commit();
    }
}

void PreviewView::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) {
        deleteSelected();
        return;
    }
    if (e->key() == Qt::Key_Escape) {
        if (m_drag == Drag::Create)
            m_drag = Drag::None;
        else
            setSelected(-1);
        update();
        return;
    }
    QWidget::keyPressEvent(e);
}
