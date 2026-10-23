#pragma once

#include <QColor>
#include <QImage>
#include <QPoint>
#include <QString>
#include <QVector>
#include <cstdint>
#include <vector>

// Одна линия, которую бот проводит, не отпуская кнопку мыши
using Stroke = QVector<QPoint>;

struct EdgeParams {
    int mode = 0;          // 0 = линии (Canny), 1 = границы цветов
    int low = 60;          // нижний порог Canny
    int high = 160;        // верхний порог Canny
    int colors = 6;        // число цветов для режима «границы цветов»
    int blur = 1;          // подавление шума
    double minLen = 8;     // выкидывать линии короче этого (px)
    double simplify = 0.8; // упрощение ломаной (px), 0 = точь-в-точь
};

// Штриховка теней: чем темнее участок, тем больше слоёв штрихов на нём
struct HatchParams {
    double spacing = 6;    // расстояние между штрихами, px
    int layers = 3;        // сколько слоёв (направлений) максимум, 1–4
    double light = 0.2;    // с какой темноты начинается штриховка (0 = почти белое, 1 = чёрное)
    double minSeg = 4;     // самый короткий штрих, px
};

// Раскраска: картинка делится на несколько цветов, бот обводит границы
// и потом по очереди заливает области каждого цвета
struct ColoringParams {
    int colors = 6;          // сколько цветов искать (2–16)
    int smooth = 2;          // сглаживание пятен
    bool usePalette = true;  // подгонять цвета под стандартную палитру Paint
    bool skipWhite = true;   // не заливать белый (это фон холста)
    int minArea = 40;        // области меньше этого (px²) не заливать
    double fillSpacing = 3;  // расстояние между штрихами при заливке штрихами
};

struct ColorLayer {
    QColor color;
    int paletteIndex = -1;   // номер в палитре Paint (0–19) или −1
    bool background = false; // белый фон — пропускаем
    qint64 area = 0;         // сколько пикселей этого цвета
    QVector<QPoint> seeds;   // куда кликнуть ведром: по точке внутри каждой области
    QVector<Stroke> fill;    // штрихи для заливки без ведра
    int skippedRegions = 0;  // области, слишком мелкие для заливки
};

struct Coloring {
    QVector<Stroke> outline;   // контуры всех областей
    QVector<ColorLayer> layers; // по убыванию площади, фон в конце
    QImage posterized;          // как будет выглядеть результат
};

struct PaletteColor {
    QColor color;
    QString name;
    int row;                 // 1 = верхний ряд палитры Paint, 2 = нижний
    int column;              // 1–10 слева направо
};

namespace Contours {

// Стандартные 20 цветов палитры Paint
const QVector<PaletteColor>& paintPalette();
int nearestPaletteIndex(const QColor& c);

Coloring buildColoring(const QImage& img, const ColoringParams& p, double minLen, double simplify);

// Параллельные штрихи по тёмным местам. Слои идут под углами 45°, −45°, 0°, 90°:
// светлая тень получает один слой, тёмная — перекрёстную штриховку из всех.
QVector<Stroke> hatchStrokes(const QImage& img, const HatchParams& p);

// Убирает прозрачность (кладёт картинку на белый фон)
QImage flattenOnWhite(const QImage& src);

// Вписывает картинку в area с сохранением пропорций.
// offset — сдвиг, чтобы картинка оказалась по центру области.
QImage fitInto(const QImage& src, QSize area, QPoint* offset);

// Карта контуров: 1 = пиксель линии, размер w*h
std::vector<uint8_t> detectEdges(const QImage& img, const EdgeParams& p);

// Превращает пиксели контуров в линии и упорядочивает их,
// чтобы курсор меньше бегал впустую
QVector<Stroke> traceStrokes(std::vector<uint8_t> edges, int w, int h,
                             double minLen, double simplify);

// Добавляет промежуточные точки с шагом step
Stroke densify(const Stroke& s, double step);

// Картинка для превью
QImage renderPreview(const QImage& fitted, const QVector<Stroke>& strokes, bool showImage);

} // namespace Contours
