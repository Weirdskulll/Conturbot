#pragma once

#include <QPoint>
#include <QString>
#include <QVector>
#include <QtGlobal>

// Работа с окнами Windows: список открытых окон, принудительное переключение,
// проверка прав администратора
namespace WinUtil {

struct WindowInfo {
    quintptr hwnd = 0;
    quint32 pid = 0;
    QString title;
    QString exe;
};

// Видимые окна приложений (без окон самого ContourBot)
QVector<WindowInfo> listWindows();

bool isAlive(quintptr hwnd);
bool isMinimized(quintptr hwnd);
quint32 windowPid(quintptr hwnd);

// Левый верхний угол рабочей области окна на экране (в настоящих пикселях)
QPoint clientOrigin(quintptr hwnd);

// Разворачивает окно и насильно делает его активным
bool bringToFront(quintptr hwnd);

// Активно ли сейчас какое-нибудь окно этого процесса (диалоги тоже считаются)
bool isForegroundProcess(quint32 pid);

// Активно ли именно это окно или его собственное окошко (палитра, диалог).
// Важно, когда у программы несколько окон в одном процессе, как у Paint.
bool isForegroundWindow(quintptr hwnd);

bool isProcessElevated(quint32 pid);
bool isSelfElevated();

// Перезапускает ContourBot от имени администратора
bool relaunchAsAdmin();

} // namespace WinUtil
