#include "areaselector.h"

#include <QCloseEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QRegion>
#include <QScreen>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
QPoint localPos(QMouseEvent* e)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return e->position().toPoint();
#else
    return e->pos();
#endif
}
} // namespace

AreaSelector::AreaSelector(QScreen* screen)
    : QWidget(nullptr, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool)
    , m_screen(screen)
{
    setAttribute(Qt::WA_DeleteOnClose);
    m_shot = screen->grabWindow(0);
    setGeometry(screen->geometry());
    setCursor(Qt::CrossCursor);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

QPoint AreaSelector::physicalCursor(QMouseEvent* e) const
{
#ifdef Q_OS_WIN
    Q_UNUSED(e)
    // берём координаты прямо у Windows — так не будет ошибок при масштабе 125%/150%
    POINT p{};
    GetCursorPos(&p);
    return QPoint(p.x, p.y);
#else
    const QRect g = m_screen->geometry();
    const qreal dpr = m_screen->devicePixelRatio();
    return g.topLeft() + (localPos(e)) * dpr;
#endif
}

void AreaSelector::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.drawPixmap(rect(), m_shot);

    const QRect sel = QRect(m_a, m_b).normalized();
    const bool hasSel = m_dragging && sel.width() > 1 && sel.height() > 1;

    // затемняем всё, кроме выделения
    QRegion dim(rect());
    if (hasSel)
        dim = dim.subtracted(QRegion(sel));
    p.setClipRegion(dim);
    p.fillRect(rect(), QColor(0, 0, 0, 120));
    p.setClipping(false);

    QFont f = font();
    f.setPointSize(11);
    p.setFont(f);

    if (hasSel) {
        p.setPen(QPen(QColor(0, 170, 255), 2));
        p.setBrush(Qt::NoBrush);
        p.drawRect(sel.adjusted(0, 0, -1, -1));

        const qreal dpr = m_screen->devicePixelRatio();
        const QString size = QStringLiteral("%1 × %2").arg(qRound(sel.width() * dpr)).arg(qRound(sel.height() * dpr));
        QRect box = p.fontMetrics().boundingRect(size).adjusted(-8, -4, 8, 4);
        box.moveTopLeft(sel.bottomLeft() + QPoint(0, 6));
        if (box.bottom() > height())
            box.moveBottomLeft(sel.topLeft() - QPoint(0, 6));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 120, 200));
        p.drawRoundedRect(box, 4, 4);
        p.setPen(Qt::white);
        p.drawText(box, Qt::AlignCenter, size);
    }

    const QString hint = QStringLiteral("Зажми левую кнопку и выдели область, где бот будет рисовать.    Esc — отмена");
    QRect hb = p.fontMetrics().boundingRect(hint).adjusted(-16, -9, 16, 9);
    hb.moveCenter(QPoint(width() / 2, 40));
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(20, 20, 24, 230));
    p.drawRoundedRect(hb, 8, 8);
    p.setPen(Qt::white);
    p.drawText(hb, Qt::AlignCenter, hint);
}

void AreaSelector::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::RightButton) {
        close();
        return;
    }
    if (e->button() != Qt::LeftButton)
        return;
    m_dragging = true;
    m_a = m_b = localPos(e);
    m_physA = physicalCursor(e);
    update();
}

void AreaSelector::mouseMoveEvent(QMouseEvent* e)
{
    if (!m_dragging)
        return;
    m_b = localPos(e);
    update();
}

void AreaSelector::mouseReleaseEvent(QMouseEvent* e)
{
    if (!m_dragging || e->button() != Qt::LeftButton)
        return;
    m_dragging = false;
    const QRect r = QRect(m_physA, physicalCursor(e)).normalized();
    if (r.width() >= 10 && r.height() >= 10) {
        m_done = true;
        emit selected(r);
        close();
    } else {
        update(); // слишком маленькая — пусть выделит ещё раз
    }
}

void AreaSelector::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape)
        close();
}

void AreaSelector::closeEvent(QCloseEvent* e)
{
    if (!m_done)
        emit canceled();
    QWidget::closeEvent(e);
}
