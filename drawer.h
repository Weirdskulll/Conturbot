#pragma once

#include <QRect>
#include <QThread>
#include <atomic>

#include "contours.h"

// Водит курсором и рисует линии в отдельном потоке, чтобы окно не зависало
class Drawer : public QThread
{
    Q_OBJECT
public:
    struct Job {
        QVector<Stroke> strokes;
        QPoint origin;          // левый верхний угол картинки: на экране или относительно окна target
        double step = 3;        // шаг курсора, px
        double delayMs = 2;     // пауза на каждую точку

        // Паузы вокруг нажатия. Медленные программы и игры (Roblox и т.п.) не успевают
        // заметить отпускание кнопки, если курсор сразу «телепортируется» дальше, —
        // тогда между штрихами остаётся лишний след.
        double beforePressMs = 15;   // курсор уже на месте, ждём перед нажатием
        double afterPressMs = 10;    // нажали, ждём перед первым движением
        double beforeReleaseMs = 5;  // дорисовали, ждём перед отпусканием
        double afterReleaseMs = 30;  // отпустили, ждём перед переходом к следующему штриху
        bool smoothTravel = false;   // вести курсор к следующему штриху плавно, без телепорта

        bool guard = true;      // остановиться, если пользователь сдвинул мышь
        int stopKey = 0x1B;     // клавиша остановки (виртуальный код Windows)
        QRect outline;          // если задано — просто обвести рамку курсором, без нажатия
        QVector<QPoint> clicks; // если задано — просто кликнуть в эти точки (заливка ведром)
        double clickPauseMs = 60; // пауза после клика, чтобы программа успела залить

        quintptr target = 0;    // окно, на которое надо насильно переключиться (0 = не трогать)
        quint32 targetPid = 0;
        QString targetName;
    };

    using QThread::QThread;

    // Флаг остановки сбрасываем здесь, а не в run(): иначе «Стоп», нажатый между
    // setJob() и фактическим запуском потока (пауза перед стартом), терялся бы.
    void setJob(const Job& job) { m_job = job; m_stop = false; }
    void requestStop() { m_stop = true; }

signals:
    void progress(int done, int total);
    void report(const QString& message);
    void completed(bool ok); // ok = дорисовал до конца, а не остановлен

protected:
    void run() override;

private:
    bool runJob(); // true = выполнено до конца
    Job m_job;
    std::atomic_bool m_stop{false};
};
