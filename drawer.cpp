#include "drawer.h"

#include "winutil.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

using Clock = std::chrono::steady_clock;

// Обычный sleep в Windows неточный на миллисекундах, поэтому досыпаем вручную
void preciseWait(double ms)
{
    if (ms <= 0)
        return;
    const auto end = Clock::now()
        + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double, std::milli>(ms));
    if (ms > 3)
        std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(ms - 2));
    while (Clock::now() < end)
        std::this_thread::yield();
}

#ifdef Q_OS_WIN
// Абсолютное событие мыши через SendInput. В отличие от SetCursorPos, его видят
// все программы, включая игры и рисовалки, которые читают «сырой» ввод мыши.
void sendMouse(const QPoint& p, DWORD extraFlags)
{
    const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vw = std::max(2, GetSystemMetrics(SM_CXVIRTUALSCREEN));
    const int vh = std::max(2, GetSystemMetrics(SM_CYVIRTUALSCREEN));
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dx = LONG(std::lround((p.x() - vx) * 65535.0 / (vw - 1)));
    in.mi.dy = LONG(std::lround((p.y() - vy) * 65535.0 / (vh - 1)));
    in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK | extraFlags;
    SendInput(1, &in, sizeof(INPUT));
}

QPoint cursorPos()
{
    POINT p{};
    GetCursorPos(&p);
    return QPoint(p.x, p.y);
}

void moveMouse(const QPoint& p)
{
    sendMouse(p, 0);
    if (cursorPos() != p) // поправка округления, чтобы линия не дрожала
        SetCursorPos(p.x(), p.y());
}

void mouseButton(const QPoint& p, bool down)
{
    sendMouse(p, down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP);
}

bool keyDown(int vk)
{
    return vk && (GetAsyncKeyState(vk) & 0x8000);
}
#endif

} // namespace

void Drawer::run()
{
    const bool ok = runJob();
    emit completed(ok);
}

bool Drawer::runJob()
{
#ifndef Q_OS_WIN
    emit report(QStringLiteral("Рисование мышью работает только в Windows."));
    return false;
#else
    const Job job = m_job;
    QString result;
    QPoint last;
    bool haveLast = false;
    bool pressed = false;
    QPoint base;        // куда прибавлять координаты (экран или угол окна)

    // ---- переключаемся на нужное окно
    auto focusTarget = [&]() -> bool {
        if (!job.target)
            return true;
        if (!WinUtil::isAlive(job.target)) {
            result = QStringLiteral("Окно «%1» закрыто. Выбери окно заново.").arg(job.targetName);
            return false;
        }
        if (WinUtil::isForegroundWindow(job.target))
            return true;
        for (int attempt = 0; attempt < 3; ++attempt) {
            WinUtil::bringToFront(job.target);
            preciseWait(150 + attempt * 150);
            if (WinUtil::isForegroundWindow(job.target))
                return true;
        }
        result = QStringLiteral("Не получилось переключиться на окно «%1». Кликни по нему сам и нажми клавишу старта.")
                     .arg(job.targetName);
        return false;
    };

    if (!focusTarget()) {
        emit report(result);
        return false;
    }
    if (job.target) {
        preciseWait(200); // даём окну развернуться и перерисоваться
        base = WinUtil::clientOrigin(job.target);
    }

    auto shouldStop = [&]() {
        if (m_stop || keyDown(job.stopKey)) {
            result = QStringLiteral("Остановлено.");
            return true;
        }
        if (job.guard && haveLast && (cursorPos() - last).manhattanLength() > 40) {
            result = QStringLiteral("Остановлено: мышь сдвинули рукой.");
            return true;
        }
        return false;
    };
    auto go = [&](const QPoint& p) {
        moveMouse(p);
        last = p;
        haveLast = true;
    };
    // Переход к началу следующего штриха с отпущенной кнопкой. По умолчанию — прыжком,
    // а для программ, которые рисуют «шлейф» за телепортом, — плавно, по шагам.
    auto travelTo = [&](const QPoint& p) -> bool {
        if (job.smoothTravel && haveLast && last != p) {
            const Stroke path = Contours::densify(Stroke{last, p}, std::max(2.0, job.step * 2));
            for (int k = 1; k + 1 < path.size(); ++k) {
                if (shouldStop())
                    return false;
                go(path[k]);
                preciseWait(std::max(1.0, job.delayMs));
            }
        }
        go(p);
        return true;
    };

    // ---- проверка области: курсор обходит рамку, ничего не нажимая
    if (!job.outline.isNull()) {
        const QRect r = job.outline.translated(base);
        const Stroke frame{r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft(), r.topLeft()};
        const Stroke pts = Contours::densify(frame, std::max(4, (r.width() + r.height()) / 200));
        for (const QPoint& p : pts) {
            if (shouldStop())
                break;
            go(p);
            preciseWait(2000.0 / pts.size());
        }
        emit report(result.isEmpty()
            ? QStringLiteral("Курсор обошёл рамку области. Если он прошёл по её краям, всё настроено верно.")
            : result);
        return result.isEmpty();
    }

    // ---- заливка ведром: по одному клику в каждую область
    if (!job.clicks.isEmpty()) {
        const QPoint origin = base + job.origin;
        const int total = job.clicks.size();
        for (int i = 0; i < total; ++i) {
            if (job.target && !WinUtil::isForegroundWindow(job.target)) {
                if (!focusTarget())
                    break;
                if (WinUtil::clientOrigin(job.target) != base) {
                    result = QStringLiteral("Остановлено: окно «%1» сдвинулось.").arg(job.targetName);
                    break;
                }
                haveLast = false;
            }
            if (shouldStop())
                break;
            const QPoint p = origin + job.clicks[i];
            if (!travelTo(p))
                break;
            preciseWait(job.beforePressMs);
            mouseButton(p, true);
            preciseWait(job.afterPressMs);
            mouseButton(p, false);
            preciseWait(job.clickPauseMs);
            emit progress(i + 1, total);
        }
        emit report(result.isEmpty() ? QStringLiteral("Цвет залит.") : result);
        return result.isEmpty();
    }

    // ---- рисование
    const QPoint origin = base + job.origin;
    const int total = job.strokes.size();
    for (int i = 0; i < total && result.isEmpty(); ++i) {
        // если что-то перехватило фокус (уведомление, другое окно) — возвращаем его
        if (job.target && !WinUtil::isForegroundWindow(job.target)) {
            if (!focusTarget())
                break;
            const QPoint nowBase = WinUtil::clientOrigin(job.target);
            if (nowBase != base) {
                result = QStringLiteral("Остановлено: окно «%1» сдвинулось.").arg(job.targetName);
                break;
            }
            haveLast = false; // после переключения не считаем это движением рукой
        }

        const Stroke pts = Contours::densify(job.strokes[i], job.step);
        for (int j = 0; j < pts.size(); ++j) {
            if (shouldStop())
                break;
            const QPoint p = origin + pts[j];
            if (j == 0) {
                if (!travelTo(p))
                    break;
                preciseWait(job.beforePressMs);
                mouseButton(p, true);
                pressed = true;
                preciseWait(job.afterPressMs);
            } else {
                go(p);
                preciseWait(job.delayMs);
            }
        }
        if (pressed) {
            preciseWait(job.beforeReleaseMs);
            mouseButton(last, false);
            pressed = false;
        }
        // главное лекарство от «шлейфа»: даём программе обработать отпускание,
        // прежде чем курсор уедет к следующему штриху
        preciseWait(job.afterReleaseMs);
        if ((i + 1) % 5 == 0 || i + 1 == total)
            emit progress(i + 1, total);
    }
    if (pressed)
        mouseButton(last, false);

    emit report(result.isEmpty() ? QStringLiteral("Готово! Контуры нарисованы, можно заливать.") : result);
    return result.isEmpty();
#endif
}
