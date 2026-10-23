#include "colorpanel.h"

#include <QApplication>
#include <QClipboard>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QStyle>
#include <QVBoxLayout>

namespace {

QString swatchStyle(const QColor& c, int radius)
{
    return QStringLiteral("background:%1; border:1px solid rgba(127,127,127,160); border-radius:%2px;")
        .arg(c.name(), QString::number(radius));
}

} // namespace

ColorPanel::ColorPanel(QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus)
{
    setAttribute(Qt::WA_ShowWithoutActivating);
    setWindowTitle(QStringLiteral("ContourBot — раскраска"));

    m_title = new QLabel;
    QFont tf = font();
    tf.setPointSize(tf.pointSize() + 2);
    tf.setBold(true);
    m_title->setFont(tf);

    m_swatch = new QLabel;
    m_swatch->setFixedSize(72, 72);

    m_codes = new QLabel;
    m_codes->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFont mono(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSize(font().pointSize() + 1);
    m_codes->setFont(mono);

    m_paletteSwatch = new QLabel;
    m_paletteSwatch->setFixedSize(18, 18);
    m_paletteText = new QLabel;
    m_paletteText->setWordWrap(true);

    m_copy = new QPushButton(QStringLiteral("Скопировать HEX"));
    connect(m_copy, &QPushButton::clicked, this, [this] {
        QGuiApplication::clipboard()->setText(m_hex);
        showStatus(QStringLiteral("Скопировано: %1").arg(m_hex));
    });

    auto* codesCol = new QVBoxLayout;
    codesCol->setSpacing(6);
    codesCol->addWidget(m_codes);
    auto* palRow = new QHBoxLayout;
    palRow->setSpacing(6);
    palRow->addWidget(m_paletteSwatch, 0, Qt::AlignTop);
    palRow->addWidget(m_paletteText, 1);
    codesCol->addLayout(palRow);
    codesCol->addWidget(m_copy, 0, Qt::AlignLeft);

    auto* top = new QHBoxLayout;
    top->setSpacing(12);
    top->addWidget(m_swatch, 0, Qt::AlignTop);
    top->addLayout(codesCol, 1);

    m_steps = new QLabel;
    m_steps->setWordWrap(true);

    m_status = new QLabel;
    m_status->setWordWrap(true);
    QPalette sp = m_status->palette();
    sp.setColor(QPalette::WindowText, sp.color(QPalette::PlaceholderText));
    m_status->setPalette(sp);

    m_fill = new QPushButton;
    m_fill->setDefault(true);
    m_fill->setProperty("primary", true);
    m_skip = new QPushButton(QStringLiteral("Пропустить цвет"));
    m_finish = new QPushButton(QStringLiteral("Закончить"));
    m_fill->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    m_skip->setIcon(style()->standardIcon(QStyle::SP_MediaSkipForward));
    connect(m_fill, &QPushButton::clicked, this, &ColorPanel::fillRequested);
    connect(m_skip, &QPushButton::clicked, this, &ColorPanel::skipRequested);
    connect(m_finish, &QPushButton::clicked, this, &ColorPanel::finishRequested);
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(m_fill, 1);
    buttons->addWidget(m_skip);
    buttons->addWidget(m_finish);

    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(16, 14, 16, 14);
    col->setSpacing(10);
    col->addWidget(m_title);
    col->addLayout(top);
    col->addWidget(m_steps);
    col->addLayout(buttons);
    col->addWidget(m_status);

    setFixedWidth(400);
}

void ColorPanel::showColor(int number, int total, const ColorLayer& layer, bool exactPalette,
                           int fillMethod, const QString& startKey)
{
    const QColor c = layer.color;
    m_hex = c.name().toUpper();
    m_title->setText(QStringLiteral("Цвет %1 из %2").arg(number).arg(total));
    m_swatch->setStyleSheet(swatchStyle(c, 8));
    m_codes->setText(QStringLiteral("%1\nR %2   G %3   B %4").arg(m_hex).arg(c.red()).arg(c.green()).arg(c.blue()));

    const QVector<PaletteColor>& pal = Contours::paintPalette();
    if (layer.paletteIndex >= 0 && layer.paletteIndex < pal.size()) {
        const PaletteColor& p = pal[layer.paletteIndex];
        m_paletteSwatch->setStyleSheet(swatchStyle(p.color, 3));
        m_paletteSwatch->show();
        const QString where = QStringLiteral("%1 ряд, %2-й слева")
                                  .arg(p.row == 1 ? QStringLiteral("верхний") : QStringLiteral("нижний"))
                                  .arg(p.column);
        m_paletteText->setText(exactPalette
            ? QStringLiteral("В палитре Paint: «%1» — %2.").arg(p.name, where)
            : QStringLiteral("Ближе всего в палитре Paint: «%1» (%2). Точный цвет: «Изменить цвета» → HEX.")
                  .arg(p.name, where));
    } else {
        m_paletteSwatch->hide();
        m_paletteText->clear();
    }

    const QString tool = fillMethod == 0 ? QStringLiteral("«Заливка» (ведро)") : QStringLiteral("карандаш или кисть");
    QString what = fillMethod == 0
        ? QStringLiteral("Бот кликнет ведром в %1 обл.").arg(layer.seeds.size())
        : QStringLiteral("Бот закрасит штрихами: %1 линий.").arg(layer.fill.size());
    if (layer.skippedRegions > 0)
        what += QStringLiteral(" Мелких областей пропущено: %1.").arg(layer.skippedRegions);
    m_steps->setText(QStringLiteral("1. Выбери этот цвет в Paint.\n2. Возьми %1.\n3. Нажми %2.\n%3")
                         .arg(tool, startKey, what));

    for (QWidget* w : {static_cast<QWidget*>(m_swatch), static_cast<QWidget*>(m_codes),
                       static_cast<QWidget*>(m_paletteText), static_cast<QWidget*>(m_copy)})
        w->show();
    m_fill->setText(QStringLiteral("Залить (%1)").arg(startKey));
    m_fill->setEnabled(true);
    m_fill->show();
    m_skip->show();
    m_skip->setEnabled(true);
    m_finish->setText(QStringLiteral("Закончить"));
    m_status->clear();
    fitHeight();
    show();
    raise();
}

void ColorPanel::fitHeight()
{
    // строки с переносом при фиксированной ширине занимают больше высоты,
    // поэтому считаем высоту под текущую ширину, а не берём обычный размер
    layout()->activate();
    const int h = layout()->hasHeightForWidth() ? layout()->totalHeightForWidth(width())
                                                : layout()->totalSizeHint().height();
    setFixedHeight(h);
}

void ColorPanel::showBusy(const QString& text)
{
    m_fill->setEnabled(false);
    m_skip->setEnabled(false);
    showStatus(text);
}

void ColorPanel::showStatus(const QString& text)
{
    m_status->setText(text);
    // высота окна должна подстроиться под новую строку, иначе текст наезжает
    fitHeight();
}

void ColorPanel::showDone(const QString& text)
{
    m_title->setText(QStringLiteral("Раскраска закончена"));
    for (QWidget* w : {static_cast<QWidget*>(m_swatch), static_cast<QWidget*>(m_codes),
                       static_cast<QWidget*>(m_paletteSwatch), static_cast<QWidget*>(m_paletteText),
                       static_cast<QWidget*>(m_copy)})
        w->hide();
    m_fill->hide();
    m_skip->hide();
    m_finish->setText(QStringLiteral("Закрыть"));
    m_steps->setText(text);
    m_status->clear();
    fitHeight();
    show();
    raise();
}

void ColorPanel::placeAwayFrom(const QRect& physicalArea)
{
    QScreen* screen = QGuiApplication::primaryScreen();
    for (QScreen* s : QGuiApplication::screens()) {
        const QRect g = s->geometry();
        const qreal dpr = s->devicePixelRatio();
        const QRect phys(g.topLeft(), g.size() * dpr);
        if (phys.contains(physicalArea.center())) {
            screen = s;
            break;
        }
    }
    if (!screen)
        return;
    fitHeight();
    const qreal dpr = screen->devicePixelRatio();
    const QRect g = screen->geometry();
    // область рисования в логических координатах этого экрана
    const QRect area(g.topLeft() + (physicalArea.topLeft() - g.topLeft()) / dpr, physicalArea.size() / dpr);
    const QRect avail = screen->availableGeometry().adjusted(16, 16, -16, -16);
    const QSize sz = frameGeometry().size();
    const QPoint corners[4] = {
        QPoint(avail.right() - sz.width(), avail.top()),                       // справа сверху
        QPoint(avail.right() - sz.width(), avail.bottom() - sz.height()),      // справа снизу
        QPoint(avail.left(), avail.top()),                                     // слева сверху
        QPoint(avail.left(), avail.bottom() - sz.height()),                    // слева снизу
    };
    int best = 0;
    qint64 bestOverlap = -1;
    for (int i = 0; i < 4; ++i) {
        const QRect r(corners[i], sz);
        const QRect inter = r.intersected(area);
        const qint64 overlap = inter.isValid() ? qint64(inter.width()) * inter.height() : 0;
        if (bestOverlap < 0 || overlap < bestOverlap) {
            bestOverlap = overlap;
            best = i;
        }
        if (overlap == 0)
            break;
    }
    move(corners[best]);
}
