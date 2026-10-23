#include "contours.h"

#include <QPainter>
#include <QPen>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <random>

namespace {

// ------------------------------------------------------------ вспомогательное

std::vector<float> toGray(const QImage& src)
{
    const QImage img = src.convertToFormat(QImage::Format_RGB32);
    const int w = img.width(), h = img.height();
    std::vector<float> g(size_t(w) * h);
    for (int y = 0; y < h; ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < w; ++x)
            g[size_t(y) * w + x] = 0.299f * qRed(line[x]) + 0.587f * qGreen(line[x])
                                 + 0.114f * qBlue(line[x]);
    }
    return g;
}

void gaussianBlur(std::vector<float>& img, int w, int h, int radius)
{
    if (radius <= 0)
        return;
    const int k = 2 * radius + 1;
    const double sigma = 0.3 * ((k - 1) * 0.5 - 1) + 0.8;
    std::vector<float> kernel(k);
    double sum = 0;
    for (int i = 0; i < k; ++i) {
        const double d = i - radius;
        kernel[i] = float(std::exp(-d * d / (2 * sigma * sigma)));
        sum += kernel[i];
    }
    for (float& v : kernel)
        v = float(v / sum);

    std::vector<float> tmp(img.size());
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float acc = 0;
            for (int i = -radius; i <= radius; ++i) {
                const int xx = std::clamp(x + i, 0, w - 1);
                acc += img[size_t(y) * w + xx] * kernel[i + radius];
            }
            tmp[size_t(y) * w + x] = acc;
        }
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float acc = 0;
            for (int i = -radius; i <= radius; ++i) {
                const int yy = std::clamp(y + i, 0, h - 1);
                acc += tmp[size_t(yy) * w + x] * kernel[i + radius];
            }
            img[size_t(y) * w + x] = acc;
        }
}

// ------------------------------------------------------------ режим «линии» (Canny)

std::vector<uint8_t> canny(const QImage& img, int low, int high, int blur)
{
    const int w = img.width(), h = img.height();
    if (low > high)
        std::swap(low, high);

    std::vector<float> g = toGray(img);
    gaussianBlur(g, w, h, blur);

    std::vector<float> mag(size_t(w) * h, 0.f);
    std::vector<uint8_t> dir(size_t(w) * h, 0);
    auto at = [&](int x, int y) { return g[size_t(y) * w + x]; };

    // градиент Собеля
    for (int y = 1; y < h - 1; ++y)
        for (int x = 1; x < w - 1; ++x) {
            const float gx = -at(x - 1, y - 1) + at(x + 1, y - 1)
                           - 2 * at(x - 1, y) + 2 * at(x + 1, y)
                           - at(x - 1, y + 1) + at(x + 1, y + 1);
            const float gy = -at(x - 1, y - 1) - 2 * at(x, y - 1) - at(x + 1, y - 1)
                           + at(x - 1, y + 1) + 2 * at(x, y + 1) + at(x + 1, y + 1);
            const float ax = std::abs(gx), ay = std::abs(gy);
            const size_t i = size_t(y) * w + x;
            mag[i] = ax + ay;
            if (ay <= ax * 0.41421356f)
                dir[i] = 0;                      // градиент по горизонтали
            else if (ay >= ax * 2.41421356f)
                dir[i] = 2;                      // по вертикали
            else
                dir[i] = (gx * gy > 0) ? 1 : 3;  // по диагоналям
        }

    // соседи вдоль направления градиента
    static const int off[4][2] = {{1, 0}, {1, 1}, {0, 1}, {1, -1}};

    // подавление немаксимумов + двойной порог
    std::vector<uint8_t> state(size_t(w) * h, 0); // 1 = слабая, 2 = сильная
    std::vector<int> stack;
    for (int y = 1; y < h - 1; ++y)
        for (int x = 1; x < w - 1; ++x) {
            const int i = y * w + x;
            const float m = mag[i];
            if (m < low)
                continue;
            const int d = off[dir[i]][1] * w + off[dir[i]][0];
            if (m > mag[i + d] && m >= mag[i - d]) {
                if (m >= high) {
                    state[i] = 2;
                    stack.push_back(i);
                } else {
                    state[i] = 1;
                }
            }
        }

    // гистерезис: слабые линии оставляем, только если они касаются сильных
    std::vector<uint8_t> out(size_t(w) * h, 0);
    while (!stack.empty()) {
        const int i = stack.back();
        stack.pop_back();
        if (out[i])
            continue;
        out[i] = 1;
        const int x = i % w, y = i / w;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                const int nx = x + dx, ny = y + dy;
                if (nx < 0 || ny < 0 || nx >= w || ny >= h)
                    continue;
                const int j = ny * w + nx;
                if (state[j] && !out[j])
                    stack.push_back(j);
            }
    }
    return out;
}

// ------------------------------------------------------------ режим «границы цветов»

using Color = std::array<float, 3>;

float dist2(const Color& a, const Color& b)
{
    const float d0 = a[0] - b[0], d1 = a[1] - b[1], d2 = a[2] - b[2];
    return d0 * d0 + d1 * d1 + d2 * d2;
}

int nearest(const Color& c, const std::vector<Color>& centers)
{
    int best = 0;
    float bestD = std::numeric_limits<float>::max();
    for (int k = 0; k < int(centers.size()); ++k) {
        const float d = dist2(c, centers[k]);
        if (d < bestD) {
            bestD = d;
            best = k;
        }
    }
    return best;
}

// Картинка, разбитая на несколько цветов: номер цвета для каждого пикселя
// и сам цвет для каждого номера
struct Quantized {
    std::vector<uint8_t> label;
    std::vector<Color> colors;
    std::vector<int> paletteIndex; // номер цвета палитры Paint, если подгоняли под неё
};

// Разница цветов «на глаз» (формула redmean): ближе к восприятию, чем простое RGB
float perceptualDist(const Color& a, const Color& b)
{
    const float rm = (a[0] + b[0]) * 0.5f;
    const float dr = a[0] - b[0], dg = a[1] - b[1], db = a[2] - b[2];
    return (2.f + rm / 256.f) * dr * dr + 4.f * dg * dg + (2.f + (255.f - rm) / 256.f) * db * db;
}

int nearestPerceptual(const Color& c, const std::vector<Color>& palette)
{
    int best = 0;
    float bestD = std::numeric_limits<float>::max();
    for (int k = 0; k < int(palette.size()); ++k) {
        const float d = perceptualDist(c, palette[k]);
        if (d < bestD) {
            bestD = d;
            best = k;
        }
    }
    return best;
}

Quantized quantize(const QImage& src, int k, int smooth, const std::vector<Color>* palette)
{
    const QImage img = src.convertToFormat(QImage::Format_RGB32);
    const int w = img.width(), h = img.height();
    const int n = w * h;
    k = std::clamp(k, 2, 16);

    std::vector<Color> px(n);
    for (int y = 0; y < h; ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < w; ++x)
            px[y * w + x] = {float(qRed(line[x])), float(qGreen(line[x])), float(qBlue(line[x]))};
    }

    // k-means по выборке пикселей
    std::vector<int> sample;
    const int stride = std::max(1, n / 20000);
    for (int i = 0; i < n; i += stride)
        sample.push_back(i);

    std::mt19937 rng(42);
    std::vector<Color> centers{px[sample[rng() % sample.size()]]};
    std::vector<float> d2(sample.size());
    while (int(centers.size()) < k) { // k-means++
        double total = 0;
        for (size_t s = 0; s < sample.size(); ++s) {
            d2[s] = dist2(px[sample[s]], centers[nearest(px[sample[s]], centers)]);
            total += d2[s];
        }
        if (total <= 0)
            break; // цветов меньше, чем просили
        double r = std::uniform_real_distribution<double>(0, total)(rng);
        size_t pick = 0;
        for (; pick < sample.size() - 1; ++pick) {
            r -= d2[pick];
            if (r <= 0)
                break;
        }
        centers.push_back(px[sample[pick]]);
    }

    for (int iter = 0; iter < 15; ++iter) {
        std::vector<std::array<double, 4>> acc(centers.size(), {0, 0, 0, 0});
        for (int s : sample) {
            auto& a = acc[nearest(px[s], centers)];
            a[0] += px[s][0];
            a[1] += px[s][1];
            a[2] += px[s][2];
            a[3] += 1;
        }
        for (size_t c = 0; c < centers.size(); ++c)
            if (acc[c][3] > 0)
                centers[c] = {float(acc[c][0] / acc[c][3]), float(acc[c][1] / acc[c][3]),
                              float(acc[c][2] / acc[c][3])};
    }

    // почти одинаковые цвета склеиваем, иначе однотонная заливка с шумом
    // разваливается на два цвета и даёт кучу мусорных линий.
    // Порог уменьшается, когда просят много цветов: на градиенте соседние
    // полосы близки, и с постоянным порогом 16 цветов давали меньше полос, чем 8.
    const float mergeDist = 40.f * std::min(1.f, 6.f / float(k));
    std::vector<int> merged(centers.size());
    for (size_t a = 0; a < centers.size(); ++a) {
        merged[a] = int(a);
        for (size_t b = 0; b < a; ++b)
            if (merged[b] == int(b) && dist2(centers[a], centers[b]) < mergeDist * mergeDist) {
                merged[a] = int(b);
                break;
            }
    }

    // номера групп: либо склеенные центры, либо (с палитрой) ближайший цвет палитры
    std::vector<int> group(centers.size());
    std::vector<int> groupPalette;
    {
        std::vector<int> keyOf(centers.size());
        for (size_t c = 0; c < centers.size(); ++c)
            keyOf[c] = palette ? nearestPerceptual(centers[merged[c]], *palette) : merged[c];
        std::vector<int> keys; // по возрастанию, чтобы порядок номеров был стабильным
        for (int key : keyOf)
            if (std::find(keys.begin(), keys.end(), key) == keys.end())
                keys.push_back(key);
        std::sort(keys.begin(), keys.end());
        for (size_t c = 0; c < centers.size(); ++c)
            group[c] = int(std::find(keys.begin(), keys.end(), keyOf[c]) - keys.begin());
        if (palette)
            groupPalette = keys;
    }

    std::vector<uint8_t> label(n);
    for (int i = 0; i < n; ++i)
        label[i] = uint8_t(group[nearest(px[i], centers)]);

    // убираем мелкие пятна: каждый пиксель берёт самый частый цвет вокруг
    for (int it = 0; it < smooth; ++it) {
        std::vector<uint8_t> next(label);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                int count[16] = {0};
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = x + dx, ny = y + dy;
                        if (nx >= 0 && ny >= 0 && nx < w && ny < h)
                            ++count[label[ny * w + nx]];
                    }
                int best = label[y * w + x];
                for (int c = 0; c < 16; ++c)
                    if (count[c] > count[best])
                        best = c;
                next[y * w + x] = uint8_t(best);
            }
        label.swap(next);
    }

    Quantized q;
    const int groups = group.empty() ? 0 : *std::max_element(group.begin(), group.end()) + 1;
    q.colors.assign(groups, Color{0, 0, 0});
    if (palette) {
        q.paletteIndex = groupPalette;
        for (int g = 0; g < groups; ++g)
            q.colors[g] = (*palette)[groupPalette[g]];
    } else {
        // настоящий средний цвет каждой группы
        std::vector<std::array<double, 4>> acc(groups, {0, 0, 0, 0});
        for (int i = 0; i < n; ++i) {
            auto& a = acc[label[i]];
            a[0] += px[i][0];
            a[1] += px[i][1];
            a[2] += px[i][2];
            a[3] += 1;
        }
        for (int g = 0; g < groups; ++g)
            if (acc[g][3] > 0)
                q.colors[g] = {float(acc[g][0] / acc[g][3]), float(acc[g][1] / acc[g][3]),
                               float(acc[g][2] / acc[g][3])};
    }
    q.label = std::move(label);
    return q;
}

std::vector<uint8_t> edgesOfLabels(const std::vector<uint8_t>& label, int w, int h)
{
    std::vector<uint8_t> out(size_t(w) * h, 0);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const int i = y * w + x;
            if ((x + 1 < w && label[i + 1] != label[i]) || (y + 1 < h && label[i + w] != label[i]))
                out[i] = 1;
        }
    return out;
}

std::vector<uint8_t> colorEdges(const QImage& src, int k, int smooth)
{
    const Quantized q = quantize(src, k, smooth, nullptr);
    return edgesOfLabels(q.label, src.width(), src.height());
}

// ------------------------------------------------------------ линии

double pathLength(const Stroke& s)
{
    double len = 0;
    for (int i = 1; i < s.size(); ++i)
        len += std::hypot(double(s[i].x() - s[i - 1].x()), double(s[i].y() - s[i - 1].y()));
    return len;
}

// Рамер — Дуглас — Пекер: выкидывает лишние точки, сохраняя форму
Stroke simplifyPath(const Stroke& pts, double eps)
{
    const int n = pts.size();
    if (n < 3 || eps <= 0)
        return pts;
    std::vector<char> keep(n, 0);
    keep[0] = keep[n - 1] = 1;
    std::vector<std::pair<int, int>> st{{0, n - 1}};
    while (!st.empty()) {
        const auto [a, b] = st.back();
        st.pop_back();
        const double ax = pts[a].x(), ay = pts[a].y();
        const double dx = pts[b].x() - ax, dy = pts[b].y() - ay;
        const double len = std::hypot(dx, dy);
        double best = -1;
        int bi = -1;
        for (int i = a + 1; i < b; ++i) {
            const double px = pts[i].x() - ax, py = pts[i].y() - ay;
            const double d = len > 0 ? std::abs(dy * px - dx * py) / len : std::hypot(px, py);
            if (d > best) {
                best = d;
                bi = i;
            }
        }
        if (bi >= 0 && best > eps) {
            keep[bi] = 1;
            st.push_back({a, bi});
            st.push_back({bi, b});
        }
    }
    Stroke out;
    for (int i = 0; i < n; ++i)
        if (keep[i])
            out.push_back(pts[i]);
    return out;
}

qint64 sq(const QPoint& a, const QPoint& b)
{
    const qint64 dx = a.x() - b.x(), dy = a.y() - b.y();
    return dx * dx + dy * dy;
}

QVector<Stroke> orderStrokes(QVector<Stroke> strokes)
{
    QVector<Stroke> result;
    result.reserve(strokes.size());
    std::vector<char> used(strokes.size(), 0);
    QPoint pos(0, 0);
    for (int step = 0; step < strokes.size(); ++step) {
        int best = -1;
        bool reverse = false;
        qint64 bestD = std::numeric_limits<qint64>::max();
        for (int i = 0; i < strokes.size(); ++i) {
            if (used[i])
                continue;
            const qint64 ds = sq(pos, strokes[i].first());
            const qint64 de = sq(pos, strokes[i].last());
            if (ds < bestD) { bestD = ds; best = i; reverse = false; }
            if (de < bestD) { bestD = de; best = i; reverse = true; }
        }
        used[best] = 1;
        Stroke s = strokes[best];
        if (reverse)
            std::reverse(s.begin(), s.end());
        pos = s.last();
        result.push_back(std::move(s));
    }
    return result;
}

} // namespace

// ============================================================ публичные функции

namespace Contours {

QImage flattenOnWhite(const QImage& src)
{
    QImage out(src.size(), QImage::Format_RGB32);
    out.fill(Qt::white);
    QPainter p(&out);
    p.drawImage(0, 0, src);
    return out;
}

QImage fitInto(const QImage& src, QSize area, QPoint* offset)
{
    area = area.expandedTo(QSize(1, 1));
    QImage scaled = src.scaled(area, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (offset)
        *offset = QPoint((area.width() - scaled.width()) / 2, (area.height() - scaled.height()) / 2);
    return scaled.convertToFormat(QImage::Format_RGB32);
}

std::vector<uint8_t> detectEdges(const QImage& img, const EdgeParams& p)
{
    if (img.isNull())
        return {};
    return p.mode == 1 ? colorEdges(img, p.colors, p.blur) : canny(img, p.low, p.high, p.blur);
}

QVector<Stroke> traceStrokes(std::vector<uint8_t> left, int w, int h, double minLen, double simplify)
{
    // сначала соседи по прямой, потом по диагонали — линии получаются ровнее
    static const int NB[8][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1},
                                 {1, 1}, {-1, 1}, {1, -1}, {-1, -1}};

    auto walk = [&](int x, int y, std::vector<QPoint>& path) {
        for (;;) {
            bool moved = false;
            for (const auto& d : NB) {
                const int nx = x + d[0], ny = y + d[1];
                if (nx >= 0 && ny >= 0 && nx < w && ny < h && left[size_t(ny) * w + nx]) {
                    left[size_t(ny) * w + nx] = 0;
                    x = nx;
                    y = ny;
                    path.emplace_back(x, y);
                    moved = true;
                    break;
                }
            }
            if (!moved)
                return;
        }
    };

    // концы линий (ровно один сосед) — с них начинать лучше всего,
    // тогда линия проходится целиком за один раз
    std::vector<int> ends, all;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const int i = y * w + x;
            if (!left[i])
                continue;
            all.push_back(i);
            int cnt = 0;
            for (const auto& d : NB) {
                const int nx = x + d[0], ny = y + d[1];
                if (nx >= 0 && ny >= 0 && nx < w && ny < h && left[size_t(ny) * w + nx])
                    ++cnt;
            }
            if (cnt == 1)
                ends.push_back(i);
        }

    QVector<Stroke> strokes;
    auto startFrom = [&](int i) {
        if (!left[i])
            return;
        left[i] = 0;
        const int x = i % w, y = i / w;
        std::vector<QPoint> fwd, back;
        walk(x, y, fwd);
        walk(x, y, back); // если начали с середины — дотягиваем в другую сторону
        Stroke path;
        path.reserve(int(fwd.size() + back.size() + 1));
        for (auto it = back.rbegin(); it != back.rend(); ++it)
            path.push_back(*it);
        path.push_back(QPoint(x, y));
        for (const QPoint& p : fwd)
            path.push_back(p);
        if (path.size() < 2 || pathLength(path) < minLen)
            return;
        strokes.push_back(simplifyPath(path, simplify));
    };
    for (int i : ends)
        startFrom(i);
    for (int i : all)
        startFrom(i);

    return orderStrokes(std::move(strokes));
}

const QVector<PaletteColor>& paintPalette()
{
    // стандартная палитра Paint: два ряда по 10 цветов
    static const QVector<PaletteColor> pal = {
        {QColor(0x00, 0x00, 0x00), QStringLiteral("Чёрный"), 1, 1},
        {QColor(0x7F, 0x7F, 0x7F), QStringLiteral("Серый"), 1, 2},
        {QColor(0x88, 0x00, 0x15), QStringLiteral("Тёмно-красный"), 1, 3},
        {QColor(0xED, 0x1C, 0x24), QStringLiteral("Красный"), 1, 4},
        {QColor(0xFF, 0x7F, 0x27), QStringLiteral("Оранжевый"), 1, 5},
        {QColor(0xFF, 0xF2, 0x00), QStringLiteral("Жёлтый"), 1, 6},
        {QColor(0x22, 0xB1, 0x4C), QStringLiteral("Зелёный"), 1, 7},
        {QColor(0x00, 0xA2, 0xE8), QStringLiteral("Бирюзовый"), 1, 8},
        {QColor(0x3F, 0x48, 0xCC), QStringLiteral("Индиго"), 1, 9},
        {QColor(0xA3, 0x49, 0xA4), QStringLiteral("Фиолетовый"), 1, 10},
        {QColor(0xFF, 0xFF, 0xFF), QStringLiteral("Белый"), 2, 1},
        {QColor(0xC3, 0xC3, 0xC3), QStringLiteral("Светло-серый"), 2, 2},
        {QColor(0xB9, 0x7A, 0x57), QStringLiteral("Коричневый"), 2, 3},
        {QColor(0xFF, 0xAE, 0xC9), QStringLiteral("Розовый"), 2, 4},
        {QColor(0xFF, 0xC9, 0x0E), QStringLiteral("Золотой"), 2, 5},
        {QColor(0xEF, 0xE4, 0xB0), QStringLiteral("Светло-жёлтый"), 2, 6},
        {QColor(0xB5, 0xE6, 0x1D), QStringLiteral("Лайм"), 2, 7},
        {QColor(0x99, 0xD9, 0xEA), QStringLiteral("Светло-бирюзовый"), 2, 8},
        {QColor(0x70, 0x92, 0xBE), QStringLiteral("Серо-голубой"), 2, 9},
        {QColor(0xC8, 0xBF, 0xE7), QStringLiteral("Лавандовый"), 2, 10},
    };
    return pal;
}

int nearestPaletteIndex(const QColor& c)
{
    const Color v{float(c.red()), float(c.green()), float(c.blue())};
    std::vector<Color> pal;
    for (const PaletteColor& p : paintPalette())
        pal.push_back({float(p.color.red()), float(p.color.green()), float(p.color.blue())});
    return nearestPerceptual(v, pal);
}

Coloring buildColoring(const QImage& src, const ColoringParams& p, double minLen, double simplify)
{
    Coloring out;
    if (src.isNull())
        return out;
    const QImage img = src.convertToFormat(QImage::Format_RGB32);
    const int w = img.width(), h = img.height();
    const int n = w * h;

    std::vector<Color> pal;
    for (const PaletteColor& c : paintPalette())
        pal.push_back({float(c.color.red()), float(c.color.green()), float(c.color.blue())});
    const Quantized q = quantize(img, p.colors, p.smooth, p.usePalette ? &pal : nullptr);
    const int groups = int(q.colors.size());
    const std::vector<uint8_t>& label = q.label;

    // белый (почти белый) — это фон холста, его можно не заливать
    std::vector<char> bg(groups, 0);
    for (int g = 0; g < groups; ++g) {
        const Color& c = q.colors[g];
        bg[g] = p.skipWhite && std::min({c[0], c[1], c[2]}) >= 225.f;
    }

    // границы между цветами + край картинки у закрашиваемых областей,
    // чтобы ведро не вытекло за пределы рисунка
    std::vector<uint8_t> edges = edgesOfLabels(label, w, h);
    for (int x = 0; x < w; ++x) {
        if (!bg[label[x]]) edges[x] = 1;
        if (!bg[label[size_t(h - 1) * w + x]]) edges[size_t(h - 1) * w + x] = 1;
    }
    for (int y = 0; y < h; ++y) {
        if (!bg[label[size_t(y) * w]]) edges[size_t(y) * w] = 1;
        if (!bg[label[size_t(y) * w + w - 1]]) edges[size_t(y) * w + w - 1] = 1;
    }

    // Контур не должен иметь разрывов, иначе заливка вытечет в соседнюю область:
    // короткие куски не выбрасываем и почти не упрощаем
    Q_UNUSED(minLen)
    out.outline = traceStrokes(edges, w, h, 0, std::min(simplify, 0.8));

    // расстояние до ближайшей линии контура (4-связно)
    std::vector<int> dist(n, -1);
    std::vector<int> queue;
    queue.reserve(n);
    for (int i = 0; i < n; ++i)
        if (edges[i]) {
            dist[i] = 0;
            queue.push_back(i);
        }
    for (size_t qi = 0; qi < queue.size(); ++qi) {
        const int i = queue[qi];
        const int x = i % w, y = i / w;
        const int nb[4] = {x > 0 ? i - 1 : -1, x + 1 < w ? i + 1 : -1, y > 0 ? i - w : -1, y + 1 < h ? i + w : -1};
        for (int j : nb)
            if (j >= 0 && dist[j] < 0) {
                dist[j] = dist[i] + 1;
                queue.push_back(j);
            }
    }
    for (int i = 0; i < n; ++i) // картинка без контуров вообще (один цвет)
        if (dist[i] < 0)
            dist[i] = 1 << 20;

    // слои по цветам
    QVector<ColorLayer> layers(groups);
    for (int g = 0; g < groups; ++g) {
        const Color& c = q.colors[g];
        layers[g].color = QColor(qRound(c[0]), qRound(c[1]), qRound(c[2]));
        layers[g].paletteIndex = p.usePalette ? q.paletteIndex[g] : nearestPaletteIndex(layers[g].color);
        layers[g].background = bg[g];
    }
    for (int i = 0; i < n; ++i)
        ++layers[label[i]].area;

    // области: связные куски одного цвета, в каждую — одна точка для ведра,
    // как можно дальше от линий
    std::vector<int> comp(n, -1);
    std::vector<int> stack;
    const int minSeedDist = 3;
    for (int start = 0; start < n; ++start) {
        if (comp[start] >= 0 || edges[start] || bg[label[start]])
            continue;
        const int g = label[start];
        int area = 0, best = start;
        stack.assign(1, start);
        comp[start] = start;
        while (!stack.empty()) {
            const int i = stack.back();
            stack.pop_back();
            ++area;
            if (dist[i] > dist[best])
                best = i;
            const int x = i % w, y = i / w;
            const int nb[4] = {x > 0 ? i - 1 : -1, x + 1 < w ? i + 1 : -1, y > 0 ? i - w : -1, y + 1 < h ? i + w : -1};
            for (int j : nb)
                if (j >= 0 && comp[j] < 0 && !edges[j] && label[j] == g) {
                    comp[j] = start;
                    stack.push_back(j);
                }
        }
        if (area >= p.minArea && dist[best] >= minSeedDist)
            layers[g].seeds.push_back(QPoint(best % w, best / w));
        else if (area >= 4)
            ++layers[g].skippedRegions;
    }

    // заливка штрихами: горизонтальные линии «змейкой», чуть отступив от контура
    const int step = std::max(1, int(std::lround(p.fillSpacing)));
    for (int g = 0; g < groups; ++g) {
        if (bg[g])
            continue;
        bool reverse = false;
        for (int y = step / 2; y < h; y += step) {
            QVector<Stroke> row;
            int runStart = -1;
            for (int x = 0; x <= w; ++x) {
                const bool on = x < w && label[size_t(y) * w + x] == g && dist[size_t(y) * w + x] >= 2;
                if (on && runStart < 0)
                    runStart = x;
                if (!on && runStart >= 0) {
                    if (x - 1 - runStart >= 1)
                        row.push_back(Stroke{QPoint(runStart, y), QPoint(x - 1, y)});
                    runStart = -1;
                }
            }
            if (row.isEmpty())
                continue;
            if (reverse) {
                std::reverse(row.begin(), row.end());
                for (Stroke& s : row)
                    std::reverse(s.begin(), s.end());
            }
            reverse = !reverse;
            for (Stroke& s : row)
                layers[g].fill.push_back(std::move(s));
        }
    }

    // большие области первыми, фон — в конце
    std::stable_sort(layers.begin(), layers.end(), [](const ColorLayer& a, const ColorLayer& b) {
        if (a.background != b.background)
            return !a.background;
        return a.area > b.area;
    });
    out.layers = layers;

    // превью результата
    out.posterized = QImage(w, h, QImage::Format_RGB32);
    for (int y = 0; y < h; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(out.posterized.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const size_t i = size_t(y) * w + x;
            if (edges[i]) {
                line[x] = qRgb(20, 20, 24);
            } else if (bg[label[i]]) {
                line[x] = qRgb(255, 255, 255);
            } else {
                const Color& c = q.colors[label[i]];
                line[x] = qRgb(qRound(c[0]), qRound(c[1]), qRound(c[2]));
            }
        }
    }
    return out;
}

QVector<Stroke> hatchStrokes(const QImage& img, const HatchParams& p)
{
    QVector<Stroke> out;
    if (img.isNull())
        return out;
    const int w = img.width(), h = img.height();

    // тон: 0 = белый, 1 = чёрный; слегка размываем, чтобы штрихи не рвались на шуме
    std::vector<float> g = toGray(img);
    gaussianBlur(g, w, h, 2);
    std::vector<float> dark(g.size());
    for (size_t i = 0; i < g.size(); ++i)
        dark[i] = 1.f - g[i] / 255.f;

    static const double angles[4] = {45, -45, 0, 90};
    const int layers = std::clamp(p.layers, 1, 4);
    const double spacing = std::max(2.0, p.spacing);
    const double light = std::clamp(p.light, 0.0, 0.95);
    const double minSeg = std::max(1.0, p.minSeg);

    for (int k = 0; k < layers; ++k) {
        // каждый следующий слой включается на более тёмных местах
        const float thr = float(light + (0.92 - light) * k / layers);
        const double a = angles[k] * 3.14159265358979323846 / 180.0;
        const double dx = std::cos(a), dy = std::sin(a);   // вдоль штриха
        const double nx = -dy, ny = dx;                     // поперёк штрихов

        // диапазон проекций углов картинки на оба направления
        const double corners[4][2] = {{0, 0}, {double(w), 0}, {0, double(h)}, {double(w), double(h)}};
        double nMin = 1e9, nMax = -1e9, tMin = 1e9, tMax = -1e9;
        for (const auto& c : corners) {
            nMin = std::min(nMin, c[0] * nx + c[1] * ny);
            nMax = std::max(nMax, c[0] * nx + c[1] * ny);
            tMin = std::min(tMin, c[0] * dx + c[1] * dy);
            tMax = std::max(tMax, c[0] * dx + c[1] * dy);
        }

        bool reverse = false; // «змейкой»: каждая следующая линия в обратную сторону
        for (double o = nMin + spacing * 0.5; o < nMax; o += spacing) {
            QVector<Stroke> line;
            bool inRun = false;
            QPoint runStart, runEnd;
            for (double t = tMin; t <= tMax + 1; t += 1.0) {
                const int x = int(std::lround(o * nx + t * dx));
                const int y = int(std::lround(o * ny + t * dy));
                const bool inside = x >= 0 && y >= 0 && x < w && y < h;
                const bool on = inside && dark[size_t(y) * w + x] > thr;
                if (on) {
                    if (!inRun) {
                        inRun = true;
                        runStart = QPoint(x, y);
                    }
                    runEnd = QPoint(x, y);
                } else if (inRun) {
                    inRun = false;
                    if (std::hypot(double(runEnd.x() - runStart.x()), double(runEnd.y() - runStart.y())) >= minSeg)
                        line.push_back(Stroke{runStart, runEnd});
                }
            }
            if (inRun && std::hypot(double(runEnd.x() - runStart.x()), double(runEnd.y() - runStart.y())) >= minSeg)
                line.push_back(Stroke{runStart, runEnd});

            if (reverse) {
                std::reverse(line.begin(), line.end());
                for (Stroke& s : line)
                    std::reverse(s.begin(), s.end());
            }
            if (!line.isEmpty())
                reverse = !reverse;
            for (Stroke& s : line)
                out.push_back(std::move(s));
        }
    }
    return out;
}

Stroke densify(const Stroke& s, double step)
{
    Stroke out;
    if (s.isEmpty())
        return out;
    step = std::max(1.0, step);
    out.push_back(s.first());
    for (int i = 1; i < s.size(); ++i) {
        const QPoint a = s[i - 1], b = s[i];
        const int dist = std::max(std::abs(b.x() - a.x()), std::abs(b.y() - a.y()));
        const int n = std::max(1, int(dist / step));
        for (int t = 1; t <= n; ++t)
            out.push_back(QPoint(qRound(a.x() + (b.x() - a.x()) * double(t) / n),
                                 qRound(a.y() + (b.y() - a.y()) * double(t) / n)));
    }
    return out;
}

QImage renderPreview(const QImage& fitted, const QVector<Stroke>& strokes, bool showImage)
{
    QImage canvas(fitted.size(), QImage::Format_RGB32);
    canvas.fill(Qt::white);
    QPainter p(&canvas);
    if (showImage) {
        p.setOpacity(0.35);
        p.drawImage(0, 0, fitted);
        p.setOpacity(1.0);
    }
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(showImage ? QColor(220, 30, 60) : QColor(25, 25, 30), 1.2));
    for (const Stroke& s : strokes)
        p.drawPolyline(s.constData(), s.size());
    return canvas;
}

} // namespace Contours
