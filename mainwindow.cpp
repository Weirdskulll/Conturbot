#include "mainwindow.h"

#include "areaselector.h"
#include "drawer.h"
#include "winutil.h"
#include "colorpanel.h"

#include <functional>

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QCursor>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QStyle>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QSlider>
#include <QStandardPaths>
#include <QStatusBar>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QToolButton>
#include <QtConcurrent/QtConcurrentRun>

#ifdef Q_OS_WIN
#include <windows.h>
#ifndef MOD_NOREPEAT
#define MOD_NOREPEAT 0x4000
#endif
#endif

// ============================================================ маленькие виджеты

// Ползунок, который не перехватывает колёсико, пока на нём нет фокуса:
// иначе прокрутка панели случайно меняет значения под курсором
class WheelSafeSlider : public QSlider
{
public:
    using QSlider::QSlider;

protected:
    void wheelEvent(QWheelEvent* e) override
    {
        if (hasFocus())
            QSlider::wheelEvent(e);
        else
            e->ignore(); // колёсико уходит панели, и она прокручивается
    }
};

// Ползунок с подписью и текущим значением
class LabeledSlider : public QWidget
{
public:
    LabeledSlider(const QString& title, double lo, double hi, double value,
                  double scale = 1, const QString& suffix = QString(), QWidget* parent = nullptr)
        : QWidget(parent), m_scale(scale), m_suffix(suffix)
    {
        auto* grid = new QGridLayout(this);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setVerticalSpacing(2);
        auto* name = new QLabel(title);
        name->setWordWrap(true); // длинная подпись переносится, а не расширяет панель
        m_value = new QLabel;
        m_value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_slider = new WheelSafeSlider(Qt::Horizontal);
        m_slider->setFocusPolicy(Qt::StrongFocus);
        grid->setColumnStretch(0, 1);
        m_slider->setRange(qRound(lo * scale), qRound(hi * scale));
        grid->addWidget(name, 0, 0);
        grid->addWidget(m_value, 0, 1);
        grid->addWidget(m_slider, 1, 0, 1, 2);
        QObject::connect(m_slider, &QSlider::valueChanged, this, [this] { updateLabel(); });
        setValue(value);
        updateLabel();
    }
    double value() const { return m_slider->value() / m_scale; }
    void setValue(double v) { m_slider->setValue(qRound(v * m_scale)); }
    QSlider* slider() const { return m_slider; }

private:
    void updateLabel()
    {
        const double v = value();
        m_value->setText((m_scale == 1 ? QString::number(qRound(v)) : QString::number(v, 'f', 1)) + m_suffix);
    }
    QSlider* m_slider;
    QLabel* m_value;
    double m_scale;
    QString m_suffix;
};

// Превью: картинка с найденными контурами, вписанная в окно
class PreviewView : public QWidget
{
public:
    explicit PreviewView(QWidget* parent = nullptr) : QWidget(parent)
    {
        setMinimumSize(360, 280);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }
    void setImage(const QImage& img)
    {
        m_img = img;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        const QColor bg = palette().color(QPalette::Window).darker(112);
        p.fillRect(rect(), bg);
        if (m_img.isNull()) {
            p.setPen(palette().color(QPalette::WindowText));
            QFont f = font();
            f.setPointSize(f.pointSize() + 2);
            p.setFont(f);
            p.drawText(rect().adjusted(24, 24, -24, -24), Qt::AlignCenter | Qt::TextWordWrap,
                       QStringLiteral("Перетащи сюда картинку,\nнажми «Открыть…» или Ctrl+V"));
            return;
        }
        const QRect area = rect().adjusted(16, 16, -16, -16);
        QSize s = m_img.size();
        s.scale(area.size(), Qt::KeepAspectRatio);
        QRect target(QPoint(0, 0), s);
        target.moveCenter(area.center());
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.fillRect(target.translated(3, 3), QColor(0, 0, 0, 40)); // лёгкая тень
        p.drawImage(target, m_img);
        p.setPen(QColor(0, 0, 0, 60));
        p.drawRect(target.adjusted(0, 0, -1, -1));
    }

private:
    QImage m_img;
};

// Выпадающий список, который обновляет пункты прямо перед открытием
class RefreshingCombo : public QComboBox
{
public:
    using QComboBox::QComboBox;
    std::function<void()> beforePopup;

    void showPopup() override
    {
        if (beforePopup)
            beforePopup();
        QComboBox::showPopup();
    }
};

// ============================================================ константы

namespace {

// Готовые наборы пауз. Программы и компьютеры успевают по-разному: Paint на мощном ПК
// переварит что угодно, а игра или онлайн-рисовалка на слабом ноутбуке не заметит
// отпускание кнопки и протянет «шлейф» к следующему штриху.
struct SpeedProfile {
    const char* name;
    double step, delay, beforePress, afterPress, beforeRelease, afterRelease, clickPause;
    bool smoothTravel;
};
const SpeedProfile kSpeedProfiles[] = {
    {"Быстро — Paint на мощном ПК",            3,  1,  5,  5,  2,  10,  40, false},
    {"Обычно",                                 3,  2, 15, 10,  5,  30,  80, false},
    {"Надёжно — игры и онлайн-рисовалки",      2,  5, 40, 30, 20, 120, 200, true},
    {"Медленный ПК — всё с большим запасом",   2, 10, 80, 60, 40, 250, 400, true},
};
constexpr int kProfileCount = int(sizeof(kSpeedProfiles) / sizeof(kSpeedProfiles[0]));
constexpr int kDefaultProfile = 1;                 // «Обычно»
constexpr int kCustomProfile = kProfileCount;     // «Свои настройки» — последний пункт

constexpr int kHotkeyId = 1;
constexpr int kStopHotkeyId = 2; // клавиша остановки: регистрируется только на время рисования

QString formatTime(double sec)
{
    if (sec < 60)
        return QStringLiteral("%1 сек").arg(qMax(1, qRound(sec)));
    const int s = qRound(sec);
    return QStringLiteral("%1 мин %2 сек").arg(s / 60).arg(s % 60);
}
} // namespace

// ============================================================ окно

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_drawer(new Drawer(this))
{
    setWindowTitle(QStringLiteral("ContourBot %1 — обводка и раскраска").arg(QStringLiteral(APP_VERSION)));
    resize(1140, 760);
    setAcceptDrops(true);

    m_recalcTimer.setSingleShot(true);
    m_recalcTimer.setInterval(150);
    connect(&m_recalcTimer, &QTimer::timeout, this, &MainWindow::recalc);
    connect(&m_recalcWatcher, &QFutureWatcherBase::finished, this, &MainWindow::onRecalcFinished);

    m_countTimer.setInterval(1000);
    connect(&m_countTimer, &QTimer::timeout, this, &MainWindow::countdownTick);

    buildUi();
    QMenu* help = menuBar()->addMenu(QStringLiteral("Справка"));
    help->addAction(QStringLiteral("О программе ContourBot"), this, [this] { showAbout(); });
    help->addAction(QStringLiteral("О Qt"), this, [] { QApplication::aboutQt(); });
    loadSettings();
    syncKeyCombos();
    updateModeWidgets();
    refreshWindows();

    connect(m_drawer, &Drawer::progress, this, [this](int done, int total) {
        m_progress->setMaximum(total);
        m_progress->setValue(done);
    });
    connect(m_drawer, &Drawer::report, this, &MainWindow::onDrawerReport);
    connect(m_drawer, &Drawer::completed, this, &MainWindow::onDrawerCompleted);

    // без родителя: иначе панель спрячется вместе со свёрнутым главным окном
    m_colorPanel = new ColorPanel(nullptr);
    connect(m_colorPanel, &ColorPanel::fillRequested, this, [this] {
        if (m_phase == Phase::WaitColor)
            fillCurrentColor();
    });
    connect(m_colorPanel, &ColorPanel::skipRequested, this, [this] {
        if (m_phase == Phase::WaitColor)
            nextColor();
    });
    connect(m_colorPanel, &ColorPanel::finishRequested, this, [this] {
        if (m_phase == Phase::Filling)
            m_drawer->requestStop();
        if (m_phase == Phase::Idle)
            m_colorPanel->hide();
        else
            finishColoring(false);
    });

    QTimer::singleShot(0, this, &MainWindow::registerHotkey);
    statusBar()->showMessage(QStringLiteral("Открой картинку, выдели область на экране, затем в Paint нажми горячую клавишу."));
}

MainWindow::~MainWindow()
{
    m_drawer->requestStop();
    m_drawer->wait(3000);
    unregisterHotkey();
    delete m_colorPanel;
}

void MainWindow::buildUi()
{
    auto* panel = new QWidget;
    auto* col = new QVBoxLayout(panel);
    col->setSpacing(12);

    // ---- 1. Картинка
    auto* gImage = new QGroupBox(QStringLiteral("1. Картинка"));
    auto* li = new QVBoxLayout(gImage);
    auto* rowImg = new QHBoxLayout;
    auto* openBtn = new QPushButton(QStringLiteral("Открыть…"));
    auto* pasteBtn = new QPushButton(QStringLiteral("Вставить из буфера"));
    openBtn->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
    pasteBtn->setIcon(style()->standardIcon(QStyle::SP_FileDialogContentsView));
    rowImg->addWidget(openBtn);
    rowImg->addWidget(pasteBtn);
    m_fileLabel = new QLabel(QStringLiteral("Картинка не выбрана"));
    m_fileLabel->setWordWrap(true);
    li->addLayout(rowImg);
    li->addWidget(m_fileLabel);
    connect(openBtn, &QPushButton::clicked, this, &MainWindow::openFile);
    connect(pasteBtn, &QPushButton::clicked, this, &MainWindow::pasteImage);
    connect(new QShortcut(QKeySequence::Paste, this), &QShortcut::activated, this, &MainWindow::pasteImage);
    connect(new QShortcut(QKeySequence::Open, this), &QShortcut::activated, this, &MainWindow::openFile);

    // ---- 2. Что рисовать
    auto* gEdges = new QGroupBox(QStringLiteral("2. Что рисовать"));
    auto* le = new QVBoxLayout(gEdges);
    le->setSpacing(8);
    m_drawMode = new QComboBox;
    m_drawMode->addItem(QStringLiteral("Контуры"));
    m_drawMode->addItem(QStringLiteral("Штриховка (тени)"));
    m_drawMode->addItem(QStringLiteral("Контуры + штриховка"));
    m_drawMode->addItem(QStringLiteral("Раскраска (контуры + цвета)"));
    le->addWidget(m_drawMode);

    QFont headerFont = font();
    headerFont.setBold(true);
    m_contourHeader = new QLabel(QStringLiteral("Контуры"));
    m_contourHeader->setFont(headerFont);
    m_hatchHeader = new QLabel(QStringLiteral("Штриховка"));
    m_hatchHeader->setFont(headerFont);
    m_hatchStep = new LabeledSlider(QStringLiteral("Расстояние между штрихами"), 2, 20, 6, 1, QStringLiteral(" px"));
    m_hatchLayers = new LabeledSlider(QStringLiteral("Слоёв для самых тёмных мест"), 1, 4, 3);
    m_hatchLight = new LabeledSlider(QStringLiteral("Штриховать начиная с темноты"), 0, 90, 20, 1, QStringLiteral(" %"));

    le->addWidget(m_contourHeader);
    m_mode = new QComboBox;
    m_mode->addItem(QStringLiteral("Линии (резкие перепады яркости)"));
    m_mode->addItem(QStringLiteral("Границы цветов (замкнутые области)"));
    m_low = new LabeledSlider(QStringLiteral("Нижний порог"), 1, 400, 60);
    m_high = new LabeledSlider(QStringLiteral("Верхний порог"), 1, 600, 160);
    m_colors = new LabeledSlider(QStringLiteral("Количество цветов"), 2, 16, 6);
    m_blur = new LabeledSlider(QStringLiteral("Подавление шума"), 0, 5, 1);
    m_minLen = new LabeledSlider(QStringLiteral("Мин. длина линии"), 0, 150, 8, 1, QStringLiteral(" px"));
    m_simplify = new LabeledSlider(QStringLiteral("Упрощение линий"), 0, 5, 0.8, 10, QStringLiteral(" px"));
    m_showImage = new QCheckBox(QStringLiteral("Показывать картинку под контурами"));
    m_showImage->setChecked(true);
    le->addWidget(m_mode);
    for (QWidget* w : {static_cast<QWidget*>(m_low), static_cast<QWidget*>(m_high),
                       static_cast<QWidget*>(m_colors), static_cast<QWidget*>(m_blur),
                       static_cast<QWidget*>(m_minLen), static_cast<QWidget*>(m_simplify)})
        le->addWidget(w);
    le->addWidget(m_hatchHeader);
    le->addWidget(m_hatchStep);
    le->addWidget(m_hatchLayers);
    le->addWidget(m_hatchLight);

    // раскраска
    m_coloringHeader = new QLabel(QStringLiteral("Заливка цветами"));
    m_coloringHeader->setFont(headerFont);
    m_fillMethod = new QComboBox;
    m_fillMethod->addItem(QStringLiteral("Ведром (клик в каждую область)"));
    m_fillMethod->addItem(QStringLiteral("Штрихами (без ведра)"));
    m_fillStep = new LabeledSlider(QStringLiteral("Шаг штрихов заливки"), 1, 10, 3, 1, QStringLiteral(" px"));
    m_minArea = new LabeledSlider(QStringLiteral("Не заливать области меньше"), 0, 400, 40, 1, QStringLiteral(" px²"));
    m_usePalette = new QCheckBox(QStringLiteral("Только цвета палитры Paint"));
    m_usePalette->setToolTip(QStringLiteral("Каждый цвет будет одним из 20 стандартных цветов Paint: его не надо подбирать вручную."));
    m_usePalette->setChecked(true);
    m_skipWhite = new QCheckBox(QStringLiteral("Не заливать белый фон"));
    m_skipWhite->setChecked(true);
    le->addWidget(m_coloringHeader);
    le->addWidget(m_usePalette);
    le->addWidget(m_skipWhite);
    le->addWidget(m_fillMethod);
    le->addWidget(m_fillStep);
    le->addWidget(m_minArea);
    connect(m_fillMethod, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
        updateModeWidgets();
        scheduleRecalc();
    });
    connect(m_usePalette, &QCheckBox::toggled, this, &MainWindow::scheduleRecalc);
    connect(m_skipWhite, &QCheckBox::toggled, this, &MainWindow::scheduleRecalc);

    le->addWidget(m_showImage);
    for (LabeledSlider* s : {m_low, m_high, m_colors, m_blur, m_minLen, m_simplify,
                             m_hatchStep, m_hatchLayers, m_hatchLight, m_fillStep, m_minArea})
        connect(s->slider(), &QSlider::valueChanged, this, &MainWindow::scheduleRecalc);
    connect(m_drawMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
        updateModeWidgets();
        scheduleRecalc();
    });
    connect(m_mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
        updateModeWidgets();
        scheduleRecalc();
    });
    connect(m_showImage, &QCheckBox::toggled, this, &MainWindow::scheduleRecalc);

    // ---- 3. Окно для рисования
    auto* gWin = new QGroupBox(QStringLiteral("3. В каком окне рисовать"));
    auto* lw = new QVBoxLayout(gWin);
    auto* combo = new RefreshingCombo;
    combo->beforePopup = [this] { refreshWindows(); };
    combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    combo->setMinimumContentsLength(20);
    m_window = combo;
    auto* winHint = new QLabel(QStringLiteral(
        "Перед рисованием бот сам переключится на это окно и вернёт его, если что-то перехватит фокус."));
    winHint->setWordWrap(true);
    m_windowWarn = new QLabel;
    m_windowWarn->setWordWrap(true);
    m_windowWarn->setStyleSheet(QStringLiteral("color:#d9534f;"));
    m_windowWarn->hide();
    m_adminBtn = new QPushButton(QStringLiteral("Перезапустить ContourBot от администратора"));
    m_adminBtn->setIcon(style()->standardIcon(QStyle::SP_VistaShield));
    m_adminBtn->hide();
    lw->addWidget(m_window);
    lw->addWidget(winHint);
    lw->addWidget(m_windowWarn);
    lw->addWidget(m_adminBtn);
    connect(m_window, QOverload<int>::of(&QComboBox::activated), this, [this] { onWindowChanged(); });
    connect(m_adminBtn, &QPushButton::clicked, this, [this] {
        saveSettings();
        if (WinUtil::relaunchAsAdmin())
            qApp->quit();
        else
            statusBar()->showMessage(QStringLiteral("Перезапуск отменён."));
    });

    // ---- 4. Где рисовать
    auto* gArea = new QGroupBox(QStringLiteral("4. Где рисовать"));
    auto* la = new QVBoxLayout(gArea);
    m_areaBtn = new QPushButton(QStringLiteral("Выделить область на экране"));
    m_areaLabel = new QLabel;
    m_areaLabel->setWordWrap(true);
    m_checkBtn = new QPushButton(QStringLiteral("Проверить: обвести рамку курсором"));
    m_testBtn = new QPushButton(QStringLiteral("Пробный штрих: нарисовать квадратик"));
    m_areaBtn->setIcon(style()->standardIcon(QStyle::SP_DesktopIcon));
    m_checkBtn->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    m_testBtn->setIcon(style()->standardIcon(QStyle::SP_DialogApplyButton));
    la->addWidget(m_areaBtn);
    la->addWidget(m_areaLabel);
    la->addWidget(m_checkBtn);
    la->addWidget(m_testBtn);
    connect(m_areaBtn, &QPushButton::clicked, this, &MainWindow::selectArea);
    connect(m_checkBtn, &QPushButton::clicked, this, &MainWindow::checkArea);
    connect(m_testBtn, &QPushButton::clicked, this, &MainWindow::testStroke);

    // ---- 5. Рисование
    auto* gDraw = new QGroupBox(QStringLiteral("5. Рисование"));
    auto* ld = new QVBoxLayout(gDraw);
    ld->setSpacing(8);
    m_speedProfile = new QComboBox;
    for (const SpeedProfile& p : kSpeedProfiles)
        m_speedProfile->addItem(QString::fromUtf8(p.name));
    m_speedProfile->addItem(QStringLiteral("Свои настройки"));
    // длинные названия профилей не должны растягивать левую панель
    m_speedProfile->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_speedProfile->setMinimumContentsLength(12);
    m_speedProfile->setToolTip(QStringLiteral(
        "Если программа не успевает за ботом (пропускает линии, тянет след между штрихами) — "
        "выбери профиль помедленнее или подкрути паузы в тонкой настройке."));
    connect(m_speedProfile, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int i) { applySpeedProfile(i); });

    m_step = new LabeledSlider(QStringLiteral("Шаг курсора"), 1, 10, 3, 1, QStringLiteral(" px"));
    m_delay = new LabeledSlider(QStringLiteral("Пауза на каждую точку"), 0, 50, 2, 10, QStringLiteral(" мс"));
    m_beforePress = new LabeledSlider(QStringLiteral("Пауза перед нажатием"), 0, 500, 15, 1, QStringLiteral(" мс"));
    m_afterPress = new LabeledSlider(QStringLiteral("Пауза после нажатия"), 0, 500, 10, 1, QStringLiteral(" мс"));
    m_beforeRelease = new LabeledSlider(QStringLiteral("Пауза перед отпусканием"), 0, 500, 5, 1, QStringLiteral(" мс"));
    m_afterRelease = new LabeledSlider(QStringLiteral("Пауза после отпускания"), 0, 1000, 30, 1, QStringLiteral(" мс"));
    m_afterRelease->setToolTip(QStringLiteral(
        "Главное средство от «шлейфа»: курсор не уедет к следующему штриху, пока программа не заметит отпускание."));
    m_clickPause = new LabeledSlider(QStringLiteral("Пауза после клика ведром"), 0, 2000, 80, 1, QStringLiteral(" мс"));
    m_smoothTravel = new QCheckBox(QStringLiteral("Плавный переход между штрихами"));
    m_smoothTravel->setToolTip(QStringLiteral(
        "Курсор едет к следующему штриху по шагам, а не прыжком. Помогает в играх и программах, "
        "которые рисуют линию за «телепортом» курсора."));
    m_guard = new QCheckBox(QStringLiteral("Остановить, если я сдвину мышь"));
    m_guard->setChecked(true);

    // тонкая настройка спрятана под кнопкой, чтобы не пугать с порога
    m_fineToggle = new QToolButton;
    m_fineToggle->setText(QStringLiteral("Тонкая настройка пауз"));
    m_fineToggle->setCheckable(true);
    m_fineToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_fineToggle->setArrowType(Qt::RightArrow);
    m_fineToggle->setAutoRaise(true);
    m_fineBox = new QWidget;
    auto* lf = new QVBoxLayout(m_fineBox);
    lf->setContentsMargins(0, 0, 0, 0);
    lf->setSpacing(8);
    for (QWidget* w : {static_cast<QWidget*>(m_beforePress), static_cast<QWidget*>(m_afterPress),
                       static_cast<QWidget*>(m_beforeRelease), static_cast<QWidget*>(m_afterRelease),
                       static_cast<QWidget*>(m_clickPause), static_cast<QWidget*>(m_smoothTravel)})
        lf->addWidget(w);
    m_fineBox->setVisible(false);
    connect(m_fineToggle, &QToolButton::toggled, this, [this](bool on) {
        m_fineBox->setVisible(on);
        m_fineToggle->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
    });

    // любое ручное изменение превращает профиль в «Свои настройки»
    for (LabeledSlider* sl : {m_step, m_delay, m_beforePress, m_afterPress, m_beforeRelease, m_afterRelease,
                              m_clickPause}) {
        connect(sl->slider(), &QSlider::valueChanged, this, &MainWindow::markCustomProfile);
        connect(sl->slider(), &QSlider::valueChanged, this, &MainWindow::scheduleRecalc); // оценка времени
    }
    connect(m_smoothTravel, &QCheckBox::toggled, this, &MainWindow::markCustomProfile);

    m_startKey = new QComboBox;
    m_stopKey = new QComboBox;
    m_stopKey->addItem(QStringLiteral("Esc"), 0x1B);
    for (int i = 0; i < 12; ++i) {
        m_startKey->addItem(QStringLiteral("F%1").arg(i + 1), 0x70 + i); // VK_F1 = 0x70
        m_stopKey->addItem(QStringLiteral("F%1").arg(i + 1), 0x70 + i);
    }
    m_startKey->setCurrentIndex(7); // F8
    auto* keys = new QFormLayout;
    keys->addRow(QStringLiteral("Начать рисовать:"), m_startKey);
    keys->addRow(QStringLiteral("Остановить:"), m_stopKey);
    connect(m_startKey, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::registerHotkey);
    connect(m_startKey, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::syncKeyCombos);
    connect(m_stopKey, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::syncKeyCombos);

    auto* hint = new QLabel(QStringLiteral(
        "Клавиши работают в любом окне: открой Paint, возьми карандаш, выбери цвет и нажми клавишу старта."));
    hint->setWordWrap(true);
    QPalette hp = hint->palette();
    hp.setColor(QPalette::WindowText, hp.color(QPalette::PlaceholderText));
    hint->setPalette(hp);

    m_drawBtn = new QPushButton(QStringLiteral("Рисовать через 3 секунды"));
    m_drawBtn->setDefault(true);
    m_stopBtn = new QPushButton(QStringLiteral("Стоп"));
    m_drawBtn->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    m_drawBtn->setProperty("primary", true);
    m_stopBtn->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
    m_stopBtn->setEnabled(false);
    auto* rowDraw = new QHBoxLayout;
    rowDraw->addWidget(m_drawBtn, 1);
    rowDraw->addWidget(m_stopBtn);
    connect(m_drawBtn, &QPushButton::clicked, this, &MainWindow::startCountdown);
    connect(m_stopBtn, &QPushButton::clicked, this, &MainWindow::stopDrawing);

    m_progress = new QProgressBar;
    m_progress->setRange(0, 1);
    m_progress->setValue(0);
    m_progress->setFormat(QStringLiteral("%v из %m линий"));
    m_info = new QLabel;
    m_info->setWordWrap(true);

    auto* profileRow = new QFormLayout;
    profileRow->addRow(QStringLiteral("Скорость:"), m_speedProfile);
    ld->addLayout(profileRow);
    ld->addWidget(m_step);
    ld->addWidget(m_delay);
    ld->addWidget(m_fineToggle);
    ld->addWidget(m_fineBox);
    ld->addWidget(m_guard);
    ld->addLayout(keys);
    ld->addWidget(hint);
    ld->addLayout(rowDraw);
    ld->addWidget(m_progress);
    ld->addWidget(m_info);

    col->addWidget(gImage);
    col->addWidget(gEdges);
    col->addWidget(gWin);
    col->addWidget(gArea);
    col->addWidget(gDraw);
    col->addStretch();

    m_lockWhileBusy = {gImage, gEdges, gWin, gArea, m_speedProfile, m_step, m_delay, m_fineBox, m_guard,
                       m_startKey, m_stopKey, m_drawBtn};

    auto* scroll = new QScrollArea;
    scroll->setWidget(panel);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFixedWidth(360);

    m_preview = new PreviewView;

    auto* central = new QWidget;
    auto* row = new QHBoxLayout(central);
    row->setContentsMargins(8, 8, 8, 8);
    row->addWidget(scroll);
    row->addWidget(m_preview, 1);
    setCentralWidget(central);

    updateAreaLabel();
}

// ------------------------------------------------------------ настройки

void MainWindow::loadSettings()
{
    QSettings s;
    m_drawMode->setCurrentIndex(s.value("drawMode", 0).toInt());
    m_hatchStep->setValue(s.value("hatchStep", 6).toDouble());
    m_hatchLayers->setValue(s.value("hatchLayers", 3).toDouble());
    m_hatchLight->setValue(s.value("hatchLight", 20).toDouble());
    m_fillMethod->setCurrentIndex(s.value("fillMethod", 0).toInt());
    m_fillStep->setValue(s.value("fillStep", 3).toDouble());
    m_minArea->setValue(s.value("minArea", 40).toDouble());
    m_usePalette->setChecked(s.value("usePalette", true).toBool());
    m_skipWhite->setChecked(s.value("skipWhite", true).toBool());
    m_mode->setCurrentIndex(s.value("mode", 0).toInt());
    m_low->setValue(s.value("low", 60).toDouble());
    m_high->setValue(s.value("high", 160).toDouble());
    m_colors->setValue(s.value("colors", 6).toDouble());
    m_blur->setValue(s.value("blur", 1).toDouble());
    m_minLen->setValue(s.value("minLen", 8).toDouble());
    m_simplify->setValue(s.value("simplify", 0.8).toDouble());
    {
        // В версиях до 1.2.0 профилей не было: у таких пользователей сохраняем их шаг и паузу
        // как «Свои настройки», а новичкам ставим «Обычно».
        const SpeedProfile& d = kSpeedProfiles[kDefaultProfile];
        const int savedProfile = s.value("speedProfile", s.contains("step") ? kCustomProfile : kDefaultProfile).toInt();
        m_applyingProfile = true;
        m_step->setValue(s.value("step", d.step).toDouble());
        m_delay->setValue(s.value("delay", d.delay).toDouble());
        m_beforePress->setValue(s.value("beforePress", d.beforePress).toDouble());
        m_afterPress->setValue(s.value("afterPress", d.afterPress).toDouble());
        m_beforeRelease->setValue(s.value("beforeRelease", d.beforeRelease).toDouble());
        m_afterRelease->setValue(s.value("afterRelease", d.afterRelease).toDouble());
        m_clickPause->setValue(s.value("clickPause", d.clickPause).toDouble());
        m_smoothTravel->setChecked(s.value("smoothTravel", d.smoothTravel).toBool());
        m_applyingProfile = false;
        const QSignalBlocker block(m_speedProfile);
        m_speedProfile->setCurrentIndex(qBound(0, savedProfile, kCustomProfile));
        applySpeedProfile(m_speedProfile->currentIndex());
    }
    m_showImage->setChecked(s.value("showImage", true).toBool());
    m_guard->setChecked(s.value("guard", true).toBool());
    m_startKey->setCurrentIndex(s.value("startKey", 7).toInt());
    m_stopKey->setCurrentIndex(s.value("stopKey", 0).toInt());
    m_lastDir = s.value("lastDir", QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)).toString();
    m_savedExe = s.value("targetExe").toString();
    m_areaRel = s.value("areaRel").toRect();
    m_area = s.value("area").toRect();
    updateAreaLabel();
}

void MainWindow::saveSettings()
{
    QSettings s;
    s.setValue("drawMode", m_drawMode->currentIndex());
    s.setValue("hatchStep", m_hatchStep->value());
    s.setValue("hatchLayers", m_hatchLayers->value());
    s.setValue("hatchLight", m_hatchLight->value());
    s.setValue("fillMethod", m_fillMethod->currentIndex());
    s.setValue("fillStep", m_fillStep->value());
    s.setValue("minArea", m_minArea->value());
    s.setValue("usePalette", m_usePalette->isChecked());
    s.setValue("skipWhite", m_skipWhite->isChecked());
    s.setValue("mode", m_mode->currentIndex());
    s.setValue("low", m_low->value());
    s.setValue("high", m_high->value());
    s.setValue("colors", m_colors->value());
    s.setValue("blur", m_blur->value());
    s.setValue("minLen", m_minLen->value());
    s.setValue("simplify", m_simplify->value());
    s.setValue("speedProfile", m_speedProfile->currentIndex());
    s.setValue("step", m_step->value());
    s.setValue("delay", m_delay->value());
    s.setValue("beforePress", m_beforePress->value());
    s.setValue("afterPress", m_afterPress->value());
    s.setValue("beforeRelease", m_beforeRelease->value());
    s.setValue("afterRelease", m_afterRelease->value());
    s.setValue("clickPause", m_clickPause->value());
    s.setValue("smoothTravel", m_smoothTravel->isChecked());
    s.setValue("showImage", m_showImage->isChecked());
    s.setValue("guard", m_guard->isChecked());
    s.setValue("startKey", m_startKey->currentIndex());
    s.setValue("stopKey", m_stopKey->currentIndex());
    s.setValue("lastDir", m_lastDir);
    s.setValue("area", m_area);
    s.setValue("areaRel", m_areaRel);
    s.setValue("targetExe", m_target ? m_savedExe : QString());
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    m_countTimer.stop();
    m_drawer->requestStop();
    // панель раскраски живёт без родителя: прячем её, чтобы она не удержала программу после закрытия окна
    m_colorPanel->hide();
    saveSettings();
    e->accept();
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, QStringLiteral("О программе ContourBot"), QStringLiteral(
        "<h3>ContourBot %1</h3>"
        "<p>Бот, который сам обводит картинку курсором мыши в Paint и других рисовалках, "
        "штрихует тени и раскрашивает рисунок по цветам.</p>"
        "<p>Автор: <b>%3</b>.<br>"
        "Навайбкожено вместе с Claude (Anthropic) — это просто игрушка, "
        "чтобы поиграться с программами для рисования.</p>"
        "<p>Лицензия GNU GPL 3.0. Сделано на Qt %2.</p>")
        .arg(QStringLiteral(APP_VERSION), QString::fromLatin1(qVersion()), QStringLiteral(APP_AUTHOR)));
}

// ------------------------------------------------------------ картинка

void MainWindow::openFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Выбери картинку"), m_lastDir,
        QStringLiteral("Картинки (*.png *.jpg *.jpeg *.bmp *.gif *.webp);;Все файлы (*)"));
    if (!path.isEmpty())
        loadFile(path);
}

void MainWindow::loadFile(const QString& path)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QImage img = reader.read();
    if (img.isNull()) {
        statusBar()->showMessage(QStringLiteral("Не получилось открыть картинку: %1").arg(reader.errorString()));
        return;
    }
    m_lastDir = QFileInfo(path).absolutePath();
    setImage(img, QFileInfo(path).fileName());
}

void MainWindow::pasteImage()
{
    const QImage img = QGuiApplication::clipboard()->image();
    if (img.isNull()) {
        statusBar()->showMessage(QStringLiteral("В буфере обмена нет картинки."));
        return;
    }
    setImage(img, QStringLiteral("из буфера обмена"));
}

void MainWindow::setImage(const QImage& img, const QString& name)
{
    m_source = Contours::flattenOnWhite(img);
    m_fileLabel->setText(QStringLiteral("%1 · %2 × %3").arg(name).arg(img.width()).arg(img.height()));
    recalc();
    statusBar()->showMessage(QStringLiteral("Картинка загружена. Подкрути ползунки, пока контуры не понравятся."));
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e)
{
    if (e->mimeData()->hasUrls() || e->mimeData()->hasImage())
        e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* e)
{
    const QMimeData* md = e->mimeData();
    if (md->hasImage()) {
        setImage(qvariant_cast<QImage>(md->imageData()), QStringLiteral("перетащенная картинка"));
    } else if (md->hasUrls() && !md->urls().isEmpty()) {
        loadFile(md->urls().first().toLocalFile());
    }
}

// ------------------------------------------------------------ контуры

EdgeParams MainWindow::params() const
{
    EdgeParams p;
    p.mode = m_mode->currentIndex();
    p.low = qRound(m_low->value());
    p.high = qRound(m_high->value());
    p.colors = qRound(m_colors->value());
    p.blur = qRound(m_blur->value());
    p.minLen = m_minLen->value();
    p.simplify = m_simplify->value();
    return p;
}

HatchParams MainWindow::hatchParams() const
{
    HatchParams h;
    h.spacing = m_hatchStep->value();
    h.layers = qRound(m_hatchLayers->value());
    h.light = m_hatchLight->value() / 100.0;
    return h;
}

bool MainWindow::coloringMode() const
{
    return m_drawMode->currentIndex() == 3;
}

ColoringParams MainWindow::coloringParams() const
{
    ColoringParams c;
    c.colors = qRound(m_colors->value());
    c.smooth = qRound(m_blur->value());
    c.usePalette = m_usePalette->isChecked();
    c.skipWhite = m_skipWhite->isChecked();
    c.minArea = qRound(m_minArea->value());
    c.fillSpacing = m_fillStep->value();
    return c;
}

void MainWindow::updateModeWidgets()
{
    const int dm = m_drawMode->currentIndex();
    const bool coloring = dm == 3;
    const bool contours = dm == 0 || dm == 2;
    const bool hatch = dm == 1 || dm == 2;
    const bool lines = m_mode->currentIndex() == 0;

    m_contourHeader->setVisible(contours && hatch); // заголовки нужны, только когда видны оба блока
    m_hatchHeader->setVisible(contours && hatch);
    m_mode->setVisible(contours);
    m_low->setVisible(contours && lines);
    m_high->setVisible(contours && lines);
    m_colors->setVisible((contours && !lines) || coloring);
    m_blur->setVisible(contours || coloring);
    m_minLen->setVisible(contours);
    m_simplify->setVisible(contours);
    m_hatchStep->setVisible(hatch);
    m_hatchLayers->setVisible(hatch);
    m_hatchLight->setVisible(hatch);

    m_coloringHeader->setVisible(coloring);
    m_usePalette->setVisible(coloring);
    m_skipWhite->setVisible(coloring);
    m_fillMethod->setVisible(coloring);
    m_fillStep->setVisible(coloring && m_fillMethod->currentIndex() == 1);
    m_minArea->setVisible(coloring && m_fillMethod->currentIndex() == 0);
    m_showImage->setText(coloring ? QStringLiteral("Показывать, как будет раскрашено")
                                  : QStringLiteral("Показывать картинку под контурами"));
}

void MainWindow::scheduleRecalc()
{
    m_recalcTimer.start();
}

void MainWindow::recalc()
{
    m_recalcTimer.stop();
    // картинка или настройки поменялись — начатая раскраска больше не совпадает
    if (m_phase == Phase::WaitColor)
        finishColoring(false);
    ++m_recalcGen;
    // если что-то уже считается, новый расчёт запустится, когда старый закончится
    if (m_launchedGen == 0)
        launchRecalc();
}

void MainWindow::launchRecalc()
{
    RecalcInput in;
    in.gen = m_recalcGen;
    in.source = m_source;
    // контуры ищем сразу в том размере, в котором будем рисовать
    in.target = m_area.isValid() ? m_area.size() : m_source.size();
    if (!m_area.isValid() && qMax(in.target.width(), in.target.height()) > 900)
        in.target.scale(900, 900, Qt::KeepAspectRatio);
    in.drawMode = m_drawMode->currentIndex();
    in.edges = params();
    in.hatch = hatchParams();
    in.coloring = coloringParams();
    in.step = m_step->value();
    in.showImage = m_showImage->isChecked();

    m_launchedGen = in.gen;
    if (!m_source.isNull())
        m_info->setText(QStringLiteral("Считаю контуры…"));
    m_recalcWatcher.setFuture(QtConcurrent::run([in] { return computeRecalc(in); }));
}

RecalcResult MainWindow::computeRecalc(const RecalcInput& in)
{
    // Работает в фоновом потоке: только картинки и вычисления, никаких виджетов
    RecalcResult r;
    r.gen = in.gen;
    r.drawMode = in.drawMode;
    if (in.source.isNull())
        return r;

    const EdgeParams& p = in.edges;
    r.fitted = Contours::fitInto(in.source, in.target, &r.offset);
    const int dm = in.drawMode;
    if (dm == 3) {
        r.coloring = Contours::buildColoring(r.fitted, in.coloring, p.minLen, p.simplify);
        r.strokes = r.coloring.outline;
    } else {
        if (dm != 1) // сначала контуры, потом тени
            r.strokes = Contours::traceStrokes(Contours::detectEdges(r.fitted, p),
                                               r.fitted.width(), r.fitted.height(), p.minLen, p.simplify);
        if (dm != 0)
            r.strokes += Contours::hatchStrokes(r.fitted, in.hatch);
    }
    if (dm == 3 && in.showImage)
        r.preview = r.coloring.posterized;
    else
        r.preview = Contours::renderPreview(r.fitted, r.strokes, in.showImage);

    for (const Stroke& s : r.strokes)
        r.points += Contours::densify(s, in.step).size();
    return r;
}

void MainWindow::onRecalcFinished()
{
    if (m_launchedGen == 0)
        return; // этот результат уже забрали в ensureRecalcDone()
    const RecalcResult r = m_recalcWatcher.result();
    m_launchedGen = 0;
    if (r.gen != m_recalcGen) {
        // пока считали, настройки снова поменялись — считаем заново с новыми
        launchRecalc();
        return;
    }
    applyRecalc(r);
    m_appliedGen = r.gen;
}

void MainWindow::ensureRecalcDone()
{
    if (m_recalcTimer.isActive())
        recalc();
    while (m_appliedGen != m_recalcGen) {
        if (m_launchedGen == 0)
            launchRecalc();
        QApplication::setOverrideCursor(Qt::WaitCursor);
        m_recalcWatcher.waitForFinished();
        QApplication::restoreOverrideCursor();
        onRecalcFinished();
    }
}

void MainWindow::applyRecalc(const RecalcResult& r)
{
    m_fitted = r.fitted;
    m_offset = r.offset;
    m_strokes = r.strokes;
    m_coloring = r.coloring;
    if (m_fitted.isNull()) {
        m_preview->setImage(QImage());
        m_info->setText(QString());
        return;
    }
    m_preview->setImage(r.preview);

    const int dm = r.drawMode;
    const double delay = m_delay->value();
    const double perStroke = m_beforePress->value() + m_afterPress->value() + m_beforeRelease->value()
                           + m_afterRelease->value();
    const double sec = (r.points * (delay + 0.05) + m_strokes.size() * perStroke) / 1000.0;

    QString text = QStringLiteral("Линий: %1 · рисовать примерно %2").arg(m_strokes.size()).arg(formatTime(sec));
    if (dm == 3) {
        int colors = 0, regions = 0;
        for (const ColorLayer& l : m_coloring.layers)
            if (!l.background && (m_fillMethod->currentIndex() == 0 ? !l.seeds.isEmpty() : !l.fill.isEmpty())) {
                ++colors;
                regions += l.seeds.size();
            }
        text = QStringLiteral("Контур: %1 линий, примерно %2\nЦветов для заливки: %3")
                   .arg(m_strokes.size()).arg(formatTime(sec)).arg(colors);
        if (m_fillMethod->currentIndex() == 0)
            text += QStringLiteral(" · областей: %1").arg(regions);
    }
    if (!m_area.isValid())
        text += QStringLiteral("\nПревью уменьшено. Выдели область, чтобы увидеть точный результат.");
    m_info->setText(text);
}

// ------------------------------------------------------------ область

void MainWindow::selectArea()
{
    hide();
    auto openSelector = [this] {
        QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        auto* sel = new AreaSelector(screen);
        connect(sel, &AreaSelector::selected, this, [this](const QRect& r) {
            setArea(r);
            restoreWindow();
            recalc();
            statusBar()->showMessage(QStringLiteral("Область выбрана. Жми %1, когда будешь готов.").arg(m_startKey->currentText()));
        });
        connect(sel, &AreaSelector::canceled, this, &MainWindow::restoreWindow);
        sel->show();
        sel->raise();
        sel->activateWindow();
        sel->setFocus();
    };
    // ждём, пока окно спрячется, чтобы оно не попало на снимок экрана.
    // Если выбрано окно для рисования — сначала выводим его на передний план.
    QTimer::singleShot(300, this, [this, openSelector] {
        if (targetAlive()) {
            WinUtil::bringToFront(m_target);
            QTimer::singleShot(400, this, openSelector);
        } else {
            openSelector();
        }
    });
}

void MainWindow::setArea(const QRect& r)
{
    m_area = r;
    if (r.isValid() && targetAlive() && !WinUtil::isMinimized(m_target))
        m_areaRel = r.translated(-WinUtil::clientOrigin(m_target));
    else
        m_areaRel = QRect();
    updateAreaLabel();
}

void MainWindow::updateAreaLabel()
{
    bool ready = m_area.isValid();
    if (!m_area.isValid()) {
        m_areaLabel->setText(QStringLiteral("Область пока не выбрана."));
    } else if (m_target && m_areaRel.isValid()) {
        m_areaLabel->setText(QStringLiteral("Внутри окна: слева %1, сверху %2 · размер %3 × %4 px.\n"
                                            "Если окно сдвинуть, область сдвинется вместе с ним.")
                                 .arg(m_areaRel.x()).arg(m_areaRel.y())
                                 .arg(m_areaRel.width()).arg(m_areaRel.height()));
    } else if (m_target) {
        m_areaLabel->setText(QStringLiteral("Выдели область заново, чтобы привязать её к выбранному окну."));
        ready = false;
    } else {
        m_areaLabel->setText(QStringLiteral("На экране: слева %1, сверху %2 · размер %3 × %4 px")
                                 .arg(m_area.x()).arg(m_area.y()).arg(m_area.width()).arg(m_area.height()));
    }
    m_checkBtn->setEnabled(ready);
    m_testBtn->setEnabled(ready);
}

// ------------------------------------------------------------ выбор окна

void MainWindow::refreshWindows()
{
    const quintptr keep = m_target;
    m_window->clear();
    m_window->addItem(QStringLiteral("Не переключаться — рисовать в активном окне"), qulonglong(0));
    int sel = 0;
    for (const WinUtil::WindowInfo& w : WinUtil::listWindows()) {
        QString title = w.title;
        if (title.size() > 60)
            title = title.left(57) + QStringLiteral("…");
        m_window->addItem(w.exe.isEmpty() ? title : QStringLiteral("%1 — %2").arg(title, w.exe),
                          qulonglong(w.hwnd));
        const int i = m_window->count() - 1;
        m_window->setItemData(i, w.pid, Qt::UserRole + 1);
        m_window->setItemData(i, w.exe, Qt::UserRole + 2);
        m_window->setItemData(i, w.title, Qt::ToolTipRole);
        if (keep ? w.hwnd == keep
                 : (sel == 0 && !m_savedExe.isEmpty() && w.exe.compare(m_savedExe, Qt::CaseInsensitive) == 0))
            sel = i;
    }
    m_window->setCurrentIndex(sel);

    const quintptr now = quintptr(m_window->currentData().toULongLong());
    if (now != m_target) {
        m_target = now;
        m_targetPid = now ? m_window->currentData(Qt::UserRole + 1).toUInt() : 0;
        if (keep) {
            m_areaRel = QRect();
            statusBar()->showMessage(QStringLiteral("Выбранное окно закрылось. Выбери его заново."));
        }
    }
    updateWindowWarning();
    updateAreaLabel();
}

void MainWindow::onWindowChanged()
{
    m_target = quintptr(m_window->currentData().toULongLong());
    m_targetPid = m_target ? m_window->currentData(Qt::UserRole + 1).toUInt() : 0;
    m_savedExe = m_window->currentData(Qt::UserRole + 2).toString();
    setArea(m_area); // пересчитываем область относительно нового окна
    updateWindowWarning();
    if (m_target)
        statusBar()->showMessage(QStringLiteral("Бот будет рисовать в окне «%1».").arg(targetName()));
}

void MainWindow::updateWindowWarning()
{
    const bool blocked = targetAlive() && !WinUtil::isSelfElevated() && WinUtil::isProcessElevated(m_targetPid);
    m_windowWarn->setText(QStringLiteral(
        "Это окно запущено от имени администратора. Windows не пропускает в него клики от обычных программ: "
        "курсор будет двигаться, но рисовать не будет. Перезапусти ContourBot от администратора."));
    m_windowWarn->setVisible(blocked);
    m_adminBtn->setVisible(blocked);
}

bool MainWindow::targetAlive() const
{
    return m_target && WinUtil::isAlive(m_target);
}

QString MainWindow::targetName() const
{
    const QString t = m_window->currentData(Qt::ToolTipRole).toString();
    return t.isEmpty() ? m_window->currentText() : t;
}

bool MainWindow::prepareTarget(QString* problem)
{
    if (!m_target)
        return true;
    if (!targetAlive()) {
        refreshWindows();
        *problem = QStringLiteral("Выбранное окно закрыто. Выбери окно заново.");
        return false;
    }
    if (!m_areaRel.isValid()) {
        *problem = QStringLiteral("Выдели область заново, чтобы привязать её к выбранному окну.");
        return false;
    }
    return true;
}

QPoint MainWindow::drawOrigin() const
{
    return m_target ? m_areaRel.topLeft() : m_area.topLeft();
}

Drawer::Job MainWindow::baseJob() const
{
    Drawer::Job job;
    job.step = m_step->value();
    job.delayMs = m_delay->value();
    job.beforePressMs = m_beforePress->value();
    job.afterPressMs = m_afterPress->value();
    job.beforeReleaseMs = m_beforeRelease->value();
    job.afterReleaseMs = m_afterRelease->value();
    job.clickPauseMs = m_clickPause->value();
    job.smoothTravel = m_smoothTravel->isChecked();
    job.guard = m_guard->isChecked();
    job.stopKey = m_stopKey->currentData().toInt();
    job.target = m_target;
    job.targetPid = m_targetPid;
    job.targetName = m_target ? targetName() : QString();
    return job;
}

// ------------------------------------------------------------ проверки

void MainWindow::checkArea()
{
    if (!m_area.isValid() || m_drawer->isRunning())
        return;
    QString problem;
    if (!prepareTarget(&problem)) {
        statusBar()->showMessage(problem);
        return;
    }
    Drawer::Job job = baseJob();
    job.outline = m_target ? m_areaRel : m_area;
    job.guard = false;
    m_drawer->setJob(job);
    m_restoreAfter = true;
    showMinimized();
    setBusy(true);
    QTimer::singleShot(400, this, [this] { m_drawer->start(); });
}

void MainWindow::testStroke()
{
    if (!m_area.isValid() || m_drawer->isRunning())
        return;
    QString problem;
    if (!prepareTarget(&problem)) {
        statusBar()->showMessage(problem);
        return;
    }
    const QRect a = m_target ? m_areaRel : m_area;
    const int s = qMax(4, qMin(40, qMin(a.width(), a.height()) / 2));
    Drawer::Job job = baseJob();
    job.strokes = {Stroke{QPoint(0, 0), QPoint(s, 0), QPoint(s, s), QPoint(0, s), QPoint(0, 0)}};
    job.origin = a.center() - QPoint(s / 2, s / 2);
    m_drawer->setJob(job);
    m_progress->setRange(0, 1);
    m_progress->setValue(0);
    m_restoreAfter = true;
    showMinimized();
    setBusy(true);
    statusBar()->showMessage(QStringLiteral("Пробный штрих: если в центре области появился квадратик, клики доходят."));
    QTimer::singleShot(400, this, [this] { m_drawer->start(); });
}

// ------------------------------------------------------------ рисование

void MainWindow::startCountdown()
{
    if (m_drawer->isRunning() || m_countTimer.isActive())
        return;
    if (m_source.isNull() || !m_area.isValid()) {
        statusBar()->showMessage(QStringLiteral("Сначала открой картинку и выдели область на экране."));
        return;
    }
    m_restoreAfter = false;
    setBusy(true);
    showMinimized();
    m_count = 3;
    m_countTimer.start();
}

void MainWindow::countdownTick()
{
    if (--m_count > 0)
        return;
    m_countTimer.stop();
    startDrawing();
}

void MainWindow::startDrawing()
{
    if (m_drawer->isRunning() || m_countTimer.isActive())
        return;
    // идёт раскраска и бот ждёт выбора цвета — клавиша старта заливает этот цвет
    if (m_phase == Phase::WaitColor) {
        fillCurrentColor();
        return;
    }
    ensureRecalcDone(); // рисуем только по актуальным контурам

    QString problem;
    if (m_source.isNull())
        problem = QStringLiteral("Сначала открой картинку.");
    else if (!m_area.isValid())
        problem = QStringLiteral("Сначала выдели область на экране.");
    else if (m_strokes.isEmpty())
        problem = QStringLiteral("Рисовать нечего. Уменьши пороги или порог штриховки.");
    else
        prepareTarget(&problem);
    if (!problem.isEmpty()) {
        setBusy(false);
        restoreWindow();
        statusBar()->showMessage(problem);
        return;
    }

    Drawer::Job job = baseJob();
    job.strokes = m_strokes;
    job.origin = drawOrigin() + m_offset;
    m_drawer->setJob(job);

    m_progress->setRange(0, m_strokes.size());
    m_progress->setValue(0);
    setBusy(true);
    statusBar()->showMessage(QStringLiteral("Рисую… Остановить: %1").arg(m_stopKey->currentText()));
    if (coloringMode()) {
        m_phase = Phase::Contours;
        m_colorPanel->hide();
    }
    m_drawer->start();
}

// ------------------------------------------------------------ раскраска

QRect MainWindow::screenArea() const
{
    if (m_target && m_areaRel.isValid() && WinUtil::isAlive(m_target) && !WinUtil::isMinimized(m_target))
        return m_areaRel.translated(WinUtil::clientOrigin(m_target));
    return m_area;
}

void MainWindow::onDrawerCompleted(bool ok)
{
    if (m_phase == Phase::Contours) {
        if (ok) {
            beginColoring();
        } else {
            m_phase = Phase::Idle;
            statusBar()->showMessage(QStringLiteral("%1 Раскраска не начата, нажми %2, чтобы начать заново.")
                                         .arg(m_lastReport, m_startKey->currentText()));
        }
    } else if (m_phase == Phase::Filling) {
        if (ok) {
            nextColor();
        } else {
            // остановили посреди заливки — даём залить этот цвет ещё раз
            m_phase = Phase::WaitColor;
            showCurrentColor();
            m_colorPanel->showStatus(QStringLiteral("%1 Нажми %2, чтобы залить этот цвет ещё раз.")
                                         .arg(m_lastReport, m_startKey->currentText()));
        }
    }
}

void MainWindow::beginColoring()
{
    const bool bucket = m_fillMethod->currentIndex() == 0;
    m_colorOrder.clear();
    for (int i = 0; i < m_coloring.layers.size(); ++i) {
        const ColorLayer& l = m_coloring.layers[i];
        if (!l.background && (bucket ? !l.seeds.isEmpty() : !l.fill.isEmpty()))
            m_colorOrder.push_back(i);
    }
    m_colorPos = 0;
    if (m_colorOrder.isEmpty()) {
        finishColoring(true);
        return;
    }
    m_phase = Phase::WaitColor;
    showCurrentColor();
    statusBar()->showMessage(QStringLiteral("Контур готов. Выбирай цвета по подсказке и жми %1.").arg(m_startKey->currentText()));
}

void MainWindow::showCurrentColor()
{
    const ColorLayer& l = m_coloring.layers[m_colorOrder[m_colorPos]];
    m_colorPanel->showColor(m_colorPos + 1, m_colorOrder.size(), l, m_usePalette->isChecked(),
                            m_fillMethod->currentIndex(), m_startKey->currentText());
    m_colorPanel->placeAwayFrom(screenArea());
}

void MainWindow::fillCurrentColor()
{
    if (m_phase != Phase::WaitColor || m_drawer->isRunning())
        return;
    QString problem;
    if (!prepareTarget(&problem)) {
        m_colorPanel->showStatus(problem);
        return;
    }
    const ColorLayer& l = m_coloring.layers[m_colorOrder[m_colorPos]];
    Drawer::Job job = baseJob();
    job.origin = drawOrigin() + m_offset;
    if (m_fillMethod->currentIndex() == 0) {
        job.clicks = l.seeds;
        m_progress->setRange(0, l.seeds.size());
    } else {
        job.strokes = l.fill;
        m_progress->setRange(0, l.fill.size());
    }
    m_progress->setValue(0);
    m_drawer->setJob(job);
    m_phase = Phase::Filling;
    m_restoreAfter = false;
    setBusy(true);
    m_colorPanel->showBusy(QStringLiteral("Заливаю %1… Остановить: %2").arg(l.color.name().toUpper(), m_stopKey->currentText()));
    m_drawer->start();
}

void MainWindow::nextColor()
{
    ++m_colorPos;
    if (m_colorPos >= m_colorOrder.size()) {
        finishColoring(true);
        return;
    }
    m_phase = Phase::WaitColor;
    showCurrentColor();
}

void MainWindow::finishColoring(bool allDone)
{
    m_phase = Phase::Idle;
    if (allDone) {
        m_colorPanel->showDone(QStringLiteral("Все цвета залиты. Если где-то осталась белая дырка, это область, "
                                              "слишком мелкая для ведра: её можно докрасить вручную."));
        m_colorPanel->placeAwayFrom(screenArea());
        statusBar()->showMessage(QStringLiteral("Раскраска закончена."));
    } else {
        m_colorPanel->hide();
        statusBar()->showMessage(QStringLiteral("Раскраска прервана."));
    }
}

void MainWindow::stopDrawing()
{
    if (m_countTimer.isActive()) {
        m_countTimer.stop();
        setBusy(false);
        statusBar()->showMessage(QStringLiteral("Отменено."));
        return;
    }
    m_drawer->requestStop();
}

void MainWindow::onDrawerReport(const QString& message)
{
    m_lastReport = message;
    setBusy(false);
    statusBar()->showMessage(message);
    if (m_restoreAfter) {
        m_restoreAfter = false;
        restoreWindow();
    }
}

void MainWindow::setBusy(bool busy)
{
    for (QWidget* w : m_lockWhileBusy)
        w->setEnabled(!busy);
    m_stopBtn->setEnabled(busy);
    if (!busy)
        updateAreaLabel();

#ifdef Q_OS_WIN
    // Короткое нажатие клавиши остановки бот мог пропустить между проверками.
    // Пока он рисует, клавиша зарегистрирована в Windows и ловится всегда.
    const HWND hwnd = reinterpret_cast<HWND>(winId());
    UnregisterHotKey(hwnd, kStopHotkeyId);
    if (busy)
        RegisterHotKey(hwnd, kStopHotkeyId, MOD_NOREPEAT, UINT(m_stopKey->currentData().toInt()));
#endif
}

void MainWindow::restoreWindow()
{
    showNormal();
    raise();
    activateWindow();
    // Windows не всегда пускает фоновое окно вперёд — помогаем
    WinUtil::bringToFront(quintptr(winId()));
}

// ------------------------------------------------------------ профили скорости

void MainWindow::applySpeedProfile(int index)
{
    if (index < 0 || index >= kProfileCount)
        return; // «Свои настройки»: ничего не трогаем
    const SpeedProfile& p = kSpeedProfiles[index];
    m_applyingProfile = true;
    m_step->setValue(p.step);
    m_delay->setValue(p.delay);
    m_beforePress->setValue(p.beforePress);
    m_afterPress->setValue(p.afterPress);
    m_beforeRelease->setValue(p.beforeRelease);
    m_afterRelease->setValue(p.afterRelease);
    m_clickPause->setValue(p.clickPause);
    m_smoothTravel->setChecked(p.smoothTravel);
    m_applyingProfile = false;
}

void MainWindow::markCustomProfile()
{
    if (m_applyingProfile || m_speedProfile->currentIndex() == kCustomProfile)
        return;
    const QSignalBlocker block(m_speedProfile);
    m_speedProfile->setCurrentIndex(kCustomProfile);
}

// ------------------------------------------------------------ горячая клавиша

void MainWindow::syncKeyCombos()
{
    // Одна клавиша на старт и на остановку ломает глобальный хоткей остановки,
    // поэтому в каждом списке блокируем клавишу, уже занятую в другом.
    const int startVk = m_startKey->currentData().toInt();
    if (m_stopKey->currentData().toInt() == startVk)
        m_stopKey->setCurrentIndex(0); // Esc: в списке старта его нет, так что конфликт исчезает
    const int stopVk = m_stopKey->currentData().toInt();

    if (auto* model = qobject_cast<QStandardItemModel*>(m_startKey->model()))
        for (int i = 0; i < m_startKey->count(); ++i)
            model->item(i)->setEnabled(m_startKey->itemData(i).toInt() != stopVk);
    if (auto* model = qobject_cast<QStandardItemModel*>(m_stopKey->model()))
        for (int i = 0; i < m_stopKey->count(); ++i)
            model->item(i)->setEnabled(m_stopKey->itemData(i).toInt() != startVk);
}

void MainWindow::registerHotkey()
{
#ifdef Q_OS_WIN
    const HWND hwnd = reinterpret_cast<HWND>(winId());
    UnregisterHotKey(hwnd, kHotkeyId);
    const UINT vk = UINT(m_startKey->currentData().toInt());
    if (!RegisterHotKey(hwnd, kHotkeyId, MOD_NOREPEAT, vk))
        statusBar()->showMessage(QStringLiteral("Клавиша %1 занята другой программой, выбери другую.")
                                     .arg(m_startKey->currentText()));
#endif
}

void MainWindow::unregisterHotkey()
{
#ifdef Q_OS_WIN
    UnregisterHotKey(reinterpret_cast<HWND>(winId()), kHotkeyId);
    UnregisterHotKey(reinterpret_cast<HWND>(winId()), kStopHotkeyId);
#endif
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
bool MainWindow::nativeEvent(const QByteArray& type, void* message, qintptr* result)
#else
bool MainWindow::nativeEvent(const QByteArray& type, void* message, long* result)
#endif
{
#ifdef Q_OS_WIN
    const MSG* msg = static_cast<const MSG*>(message);
    if (msg->message == WM_HOTKEY && msg->wParam == WPARAM(kHotkeyId)) {
        m_restoreAfter = false;
        startDrawing();
        if (result)
            *result = 0;
        return true;
    }
    if (msg->message == WM_HOTKEY && msg->wParam == WPARAM(kStopHotkeyId)) {
        stopDrawing();
        if (result)
            *result = 0;
        return true;
    }
#endif
    return QMainWindow::nativeEvent(type, message, result);
}
