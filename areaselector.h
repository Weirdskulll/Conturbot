#pragma once

#include <QPixmap>
#include <QWidget>

class QScreen;

// Полноэкранное окно со снимком экрана: пользователь выделяет мышкой область,
// в которой бот будет рисовать
class AreaSelector : public QWidget
{
    Q_OBJECT
public:
    explicit AreaSelector(QScreen* screen);

signals:
    void selected(const QRect& physicalRect); // в настоящих пикселях экрана
    void canceled();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void closeEvent(QCloseEvent*) override;

private:
    QPoint physicalCursor(QMouseEvent* e) const;

    QScreen* m_screen;
    QPixmap m_shot;
    QPoint m_a, m_b;      // углы выделения в координатах окна
    QPoint m_physA;       // первый угол в пикселях экрана
    bool m_dragging = false;
    bool m_done = false;
};
