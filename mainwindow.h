#pragma once

#include <QFutureWatcher>
#include <QImage>
#include <QMainWindow>
#include <QTimer>

#include "contours.h"
#include "drawer.h"

// Всё, что нужно для пересчёта контуров в фоне, и что из него получается.
// Расчёт идёт в отдельном потоке, чтобы окно не замирало на больших картинках.
struct RecalcInput {
    quint64 gen = 0;        // номер запроса: по нему отбрасываем устаревшие результаты
    QImage source;
    QSize target;
    int drawMode = 0;
    EdgeParams edges;
    HatchParams hatch;
    ColoringParams coloring;
    double step = 3;
    bool showImage = true;
};

struct RecalcResult {
    quint64 gen = 0;
    int drawMode = 0;
    QImage fitted;
    QPoint offset;
    QVector<Stroke> strokes;
    Coloring coloring;
    QImage preview;
    qint64 points = 0;      // сколько точек пройдёт курсор (для оценки времени)
};

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QToolButton;
class ColorPanel;
class Drawer;
class LabeledSlider;
class PreviewView;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    bool nativeEvent(const QByteArray& type, void* message, qintptr* result) override;
#else
    bool nativeEvent(const QByteArray& type, void* message, long* result) override;
#endif

private:
    void buildUi();
    void showAbout();
    void loadSettings();
    void saveSettings();

    void openFile();
    void loadFile(const QString& path);
    void pasteImage();
    void setImage(const QImage& img, const QString& name);

    void scheduleRecalc();
    void recalc();                        // запросить пересчёт (результат придёт асинхронно)
    void launchRecalc();                  // запустить фоновый расчёт с текущими настройками
    void onRecalcFinished();
    void applyRecalc(const RecalcResult& r);
    void ensureRecalcDone();              // дождаться актуального результата (перед рисованием)
    static RecalcResult computeRecalc(const RecalcInput& in);
    void syncKeyCombos();                 // нельзя выбрать одну клавишу и для старта, и для остановки
    void applySpeedProfile(int index);    // подставить паузы из готового профиля скорости
    void markCustomProfile();             // ручная правка пауз = «Свои настройки»
    void updateModeWidgets();
    EdgeParams params() const;
    HatchParams hatchParams() const;

    void selectArea();
    void setArea(const QRect& r);
    void updateAreaLabel();
    void checkArea();
    void testStroke();

    void refreshWindows();
    void onWindowChanged();
    void updateWindowWarning();
    bool targetAlive() const;
    QString targetName() const;
    bool prepareTarget(QString* problem);   // проверяет окно, true = можно рисовать
    QPoint drawOrigin() const;               // угол картинки: на экране или относительно окна
    Drawer::Job baseJob() const;             // общие настройки рисования

    void startCountdown();
    void countdownTick();
    void startDrawing();
    void stopDrawing();
    void onDrawerReport(const QString& message);
    void setBusy(bool busy);
    void restoreWindow();

    void registerHotkey();
    void unregisterHotkey();

    // раскраска
    enum class Phase { Idle, Contours, WaitColor, Filling };
    bool coloringMode() const;
    ColoringParams coloringParams() const;
    void onDrawerCompleted(bool ok);
    void beginColoring();
    void showCurrentColor();
    void fillCurrentColor();
    void nextColor();
    void finishColoring(bool allDone);
    QRect screenArea() const;          // область рисования на экране прямо сейчас

    Coloring m_coloring;
    QVector<int> m_colorOrder;         // какие слои заливать и в каком порядке
    int m_colorPos = 0;
    QString m_lastReport;              // последнее сообщение бота (почему остановился)
    Phase m_phase = Phase::Idle;
    ColorPanel* m_colorPanel = nullptr;
    QComboBox* m_fillMethod = nullptr;
    LabeledSlider* m_fillStep = nullptr;
    LabeledSlider* m_minArea = nullptr;
    QCheckBox* m_usePalette = nullptr;
    QCheckBox* m_skipWhite = nullptr;
    QLabel* m_coloringHeader = nullptr;

    // данные
    QImage m_source;
    QImage m_fitted;
    QPoint m_offset;
    QVector<Stroke> m_strokes;
    QRect m_area;          // область на экране в настоящих пикселях
    QRect m_areaRel;       // та же область относительно рабочей части выбранного окна
    QString m_lastDir;

    quintptr m_target = 0; // выбранное окно (0 = рисовать в активном окне)
    quint32 m_targetPid = 0;
    QString m_savedExe;    // программа, выбранная в прошлый раз

    QTimer m_recalcTimer;
    QFutureWatcher<RecalcResult> m_recalcWatcher;
    quint64 m_recalcGen = 0;      // последний запрошенный пересчёт
    quint64 m_launchedGen = 0;    // какой сейчас считается в фоне (0 = ничего)
    quint64 m_appliedGen = 0;     // какой уже показан
    QTimer m_countTimer;
    int m_count = 0;
    bool m_restoreAfter = false;
    Drawer* m_drawer = nullptr;

    // интерфейс
    PreviewView* m_preview = nullptr;
    QLabel* m_fileLabel = nullptr;
    QComboBox* m_drawMode = nullptr;   // контуры / штриховка / оба
    QLabel* m_contourHeader = nullptr;
    QLabel* m_hatchHeader = nullptr;
    LabeledSlider* m_hatchStep = nullptr;
    LabeledSlider* m_hatchLayers = nullptr;
    LabeledSlider* m_hatchLight = nullptr;
    QComboBox* m_mode = nullptr;
    LabeledSlider* m_low = nullptr;
    LabeledSlider* m_high = nullptr;
    LabeledSlider* m_colors = nullptr;
    LabeledSlider* m_blur = nullptr;
    LabeledSlider* m_minLen = nullptr;
    LabeledSlider* m_simplify = nullptr;
    QCheckBox* m_showImage = nullptr;
    QComboBox* m_window = nullptr;
    QLabel* m_windowWarn = nullptr;
    QPushButton* m_adminBtn = nullptr;
    QLabel* m_areaLabel = nullptr;
    QPushButton* m_areaBtn = nullptr;
    QPushButton* m_checkBtn = nullptr;
    QPushButton* m_testBtn = nullptr;
    QComboBox* m_speedProfile = nullptr;
    LabeledSlider* m_step = nullptr;
    LabeledSlider* m_delay = nullptr;
    LabeledSlider* m_beforePress = nullptr;
    LabeledSlider* m_afterPress = nullptr;
    LabeledSlider* m_beforeRelease = nullptr;
    LabeledSlider* m_afterRelease = nullptr;
    LabeledSlider* m_clickPause = nullptr;
    QCheckBox* m_smoothTravel = nullptr;
    QToolButton* m_fineToggle = nullptr;
    QWidget* m_fineBox = nullptr;
    bool m_applyingProfile = false;   // идёт подстановка профиля — это не ручная правка
    QCheckBox* m_guard = nullptr;
    QComboBox* m_startKey = nullptr;
    QComboBox* m_stopKey = nullptr;
    QPushButton* m_drawBtn = nullptr;
    QPushButton* m_stopBtn = nullptr;
    QProgressBar* m_progress = nullptr;
    QLabel* m_info = nullptr;
    QList<QWidget*> m_lockWhileBusy;
};
