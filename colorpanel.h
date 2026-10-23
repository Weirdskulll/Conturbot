#pragma once

#include <QWidget>

#include "contours.h"

class QLabel;
class QPushButton;

// Небольшое окно поверх всех окон: показывает, какой цвет выбрать в Paint,
// и ждёт, пока пользователь нажмёт клавишу старта
class ColorPanel : public QWidget
{
    Q_OBJECT
public:
    explicit ColorPanel(QWidget* parent = nullptr);

    // fillMethod: 0 = ведром, 1 = штрихами
    void showColor(int number, int total, const ColorLayer& layer, bool exactPalette,
                   int fillMethod, const QString& startKey);
    void showBusy(const QString& text);
    void showStatus(const QString& text);
    void showDone(const QString& text);

    // ставит окно в угол экрана, который не закрывает область рисования
    void placeAwayFrom(const QRect& physicalArea);

signals:
    void fillRequested();
    void skipRequested();
    void finishRequested();

private:
    void fitHeight();

    QLabel* m_title = nullptr;
    QLabel* m_swatch = nullptr;
    QLabel* m_codes = nullptr;
    QLabel* m_paletteSwatch = nullptr;
    QLabel* m_paletteText = nullptr;
    QLabel* m_steps = nullptr;
    QLabel* m_status = nullptr;
    QPushButton* m_copy = nullptr;
    QPushButton* m_fill = nullptr;
    QPushButton* m_skip = nullptr;
    QPushButton* m_finish = nullptr;
    QString m_hex;
};
