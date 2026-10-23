#include <QApplication>
#include <QIcon>
#include <QPalette>
#include <QStyleFactory>

#include "mainwindow.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <mmsystem.h>
#endif

int main(int argc, char* argv[])
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif
#ifdef Q_OS_WIN
    timeBeginPeriod(1); // точные паузы в миллисекунды при рисовании
#endif

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ContourBot"));
    QApplication::setOrganizationName(QStringLiteral("ContourBot")); // не менять: тут лежат настройки
    QApplication::setApplicationVersion(QStringLiteral(APP_VERSION));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/contourbot.png")));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    // Акцентный цвет — индиго из иконки. Остальная палитра остаётся системной,
    // поэтому светлая и тёмная тема Windows работают как раньше.
    const QColor accent(75, 83, 200);
    QPalette pal = QApplication::palette();
    pal.setColor(QPalette::Highlight, accent);
    pal.setColor(QPalette::HighlightedText, Qt::white);
    pal.setColor(QPalette::Link, accent.lighter(130));
    QApplication::setPalette(pal);

    // в тёмной теме индиго на чёрном почти не видно — берём более светлый оттенок
    const bool dark = pal.color(QPalette::Window).lightness() < 128;
    const QString titleColor = dark ? QStringLiteral("#A9AEFF") : QStringLiteral("#3A41A8");
    const QString borderColor = dark ? QStringLiteral("#3A3D4C") : QStringLiteral("#C9CBD8");
    app.setStyleSheet(QStringLiteral(R"(
        QGroupBox {
            border: 1px solid %1;
            border-radius: 8px;
            margin-top: 14px;
            padding: 12px 8px 8px 8px;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            subcontrol-position: top left;
            left: 10px;
            padding: 0 4px;
            color: %2;
        }
        QPushButton[primary="true"] {
            background: #4B53C8;
            color: white;
            border: 1px solid #3A41A8;
            border-radius: 6px;
            padding: 7px 14px;
            font-weight: 600;
        }
        QPushButton[primary="true"]:hover { background: #5A62D8; }
        QPushButton[primary="true"]:pressed { background: #3A41A8; }
        QPushButton[primary="true"]:disabled {
            background: palette(mid);
            border-color: palette(mid);
            color: palette(window);
        }
        QProgressBar {
            border: 1px solid %1;
            border-radius: 5px;
            text-align: center;
            min-height: 16px;
        }
        QProgressBar::chunk { background: #4B53C8; border-radius: 4px; }
    )").arg(borderColor, titleColor));

    MainWindow w;
    w.show();
    const int code = app.exec();

#ifdef Q_OS_WIN
    timeEndPeriod(1);
#endif
    return code;
}
