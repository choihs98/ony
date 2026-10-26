#include "Brush.h"

#include <QPainter>
#include <QPolygonF>
#include <QRadialGradient>
#include <QTransform>
#include <algorithm>
#include <cmath>

namespace {
const BrushPreset kPresets[] = {
    //  이름           모양                 크기 불투 경도 간격 흩뿌 질감  지움   필압   앨리어싱 납작 각도
    {"펜 브러시",     BrushShape::Round,   6, 100, 85,  12,   0,   0, false, false, false, 100,   0},
    {"연필 브러시",   BrushShape::Round,   4, 100, 100, 12,   0,  35, false, false, true,  100,   0},
    {"지우개",        BrushShape::Round,  12, 100, 90,  12,   0,   0, true,  false, false, 100,   0},
    {"에어브러시",    BrushShape::Round,  30,  40,  0,  10,   0,   0, false, false, false, 100,   0},
    {"마커",          BrushShape::Round,  14,  60, 100, 10,   0,   0, false, false, false, 100,   0},
    {"잉크 펜",       BrushShape::Round,   5, 100, 100,  6,   0,   0, false, true,  false, 100,   0},
    {"G 펜",          BrushShape::Round,   8, 100, 95,   6,   0,   0, false, true,  false, 100,   0},
    {"붓",            BrushShape::Round,  18,  90, 70,   7,   0,  12, false, true,  false, 100,   0},
    {"수채",          BrushShape::Round,  28,  30, 30,   6,   4,  25, false, true,  false, 100,   0},
    {"유화 붓",       BrushShape::Round,  26,  95, 80,   8,   6,  45, false, true,  false,  60,  20},
    {"목탄",          BrushShape::Round,  16,  85, 60,   8,   8,  75, false, true,  false, 100,   0},
    {"크레용",        BrushShape::Round,  14,  90, 90,  10,  10,  85, false, false, false, 100,   0},
    {"분필",          BrushShape::Square, 12,  90, 100, 10,   6,  70, false, false, false,  80,  15},
    {"스프레이",      BrushShape::Spray,  40,  80, 100, 14,   0,   0, false, false, false, 100,   0},
    {"캘리그래피",    BrushShape::Round,  14, 100, 100,  5,   0,   0, false, false, false,  25,  45},
    {"평붓",          BrushShape::Square, 24, 100, 100,  8,   0,   8, false, false, false,  30,   0},
    {"사각 브러시",   BrushShape::Square, 16, 100, 100, 12,   0,   0, false, false, false, 100,   0},
    {"하이라이터",    BrushShape::Square, 22,  40, 100,  8,   0,   0, false, false, false,  40,   0},
    {"점선",          BrushShape::Round,   8, 100, 100, 220,  0,   0, false, false, false, 100,   0},
    {"별 도장",       BrushShape::Star,   26, 100, 100, 160,  0,   0, false, false, false, 100,   0},
    {"부드러운 지우개", BrushShape::Round, 30, 100,  0,  10,   0,   0, true,  false, false, 100,   0},
    {"질감 지우개",   BrushShape::Round,  22, 100, 80,   8,   6,  70, true,  false, false, 100,   0},
    // 데모3: 점선 계열 · 컷 브러시 (follow = 획 방향으로 회전)
    {"파선",          BrushShape::Square, 16, 100, 100, 190,  0,   0, false, false, false,  30,   0, true},
    {"점선 (촘촘)",   BrushShape::Round,   5, 100, 100, 150,  0,   0, false, false, false, 100,   0},
    {"점선 (사각)",   BrushShape::Square,  8, 100, 100, 200,  0,   0, false, false, false, 100,   0},
    {"컷 브러시",     BrushShape::Round,   3, 100, 100,   6,  0,   0, true,  false, true,  100,   0},
    {"컷 (사각)",     BrushShape::Square, 14, 100, 100,   8,  0,   0, true,  false, true,  100,   0},
};
constexpr int kCount = sizeof(kPresets) / sizeof(kPresets[0]);

QImage makeDab(const BrushPreset& pr, int d, int hardness, const QColor& c) {
    d = std::max(1, d);
    const double asp = std::clamp(pr.aspect, 5, 100) / 100.0;
    const int side = int(std::ceil(d * 1.5)) + 4; // 회전해도 잘리지 않게 여유
    QImage img(side, side, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, !pr.aliased);
    p.setPen(Qt::NoPen);
    p.translate(side / 2.0, side / 2.0);
    p.rotate(pr.angle);
    if (pr.shape == BrushShape::Square) {
        p.setBrush(c);
        p.drawRect(QRectF(-d / 2.0, -d * asp / 2.0, d, d * asp));
    } else if (pr.shape == BrushShape::Star) {
        QPolygonF poly;
        for (int i = 0; i < 10; ++i) {
            const double a = -M_PI / 2 + i * M_PI / 5;
            const double r = (i % 2 == 0) ? d / 2.0 : d / 4.6;
            poly << QPointF(std::cos(a) * r, std::sin(a) * r);
        }
        p.setBrush(c);
        p.drawPolygon(poly);
    } else {
        // 둥근 모양 (납작하게 눌러서 캘리그래피/유화 붓)
        p.scale(1.0, asp);
        if (hardness >= 99) {
            p.setBrush(c);
        } else {
            QRadialGradient g(QPointF(0, 0), d / 2.0);
            QColor full = c, none = c;
            full.setAlpha(255);
            none.setAlpha(0);
            g.setColorAt(0.0, full);
            g.setColorAt(std::clamp(hardness / 100.0, 0.0, 0.98), full);
            g.setColorAt(1.0, none);
            p.setBrush(QBrush(g));
        }
        p.drawEllipse(QRectF(-d / 2.0, -d / 2.0, d, d));
    }
    return img;
}

const QImage& dabFor(BrushDabCache& cache, int preset, int d, int hardness, const QColor& c) {
    if (cache.preset == preset && cache.d == d && cache.hardness == hardness && cache.color == c.rgba() && !cache.img.isNull())
        return cache.img;
    cache.preset = preset;
    cache.d = d;
    cache.hardness = hardness;
    cache.color = c.rgba();
    cache.img = makeDab(kPresets[preset], d, hardness, c);
    return cache.img;
}
} // namespace

int brushPresetCount() { return kCount; }
const BrushPreset& brushPreset(int i) { return kPresets[std::clamp(i, 0, kCount - 1)]; }

void brushStampAt(QImage& buf, const BrushParams& params, const QColor& color, const QPointF& pos, double pressure,
                  QRandomGenerator& rng, BrushDabCache& cache) {
    const int pi = std::clamp(params.preset, 0, kCount - 1);
    const BrushPreset& pr = kPresets[pi];
    const bool usePressure = params.pressure;
    double r = params.size / 2.0 * (usePressure ? (0.15 + 0.85 * pressure) : 1.0);
    r = std::max(0.5, r);
    const double sc = usePressure ? std::clamp(pressure, 0.05, 1.0) : 1.0;
    QPainter p(&buf);
    p.setRenderHint(QPainter::SmoothPixmapTransform, !pr.aliased);

    if (pr.shape == BrushShape::Spray) {
        // 반지름 안에 작은 점을 흩뿌린다
        p.setPen(Qt::NoPen);
        QColor c = color;
        const int dots = std::max(4, int(r * r * 0.12));
        for (int i = 0; i < dots; ++i) {
            const double a = rng.generateDouble() * 2 * M_PI;
            const double rr = r * std::sqrt(rng.generateDouble());
            c.setAlphaF(std::clamp(0.35 + rng.generateDouble() * 0.65, 0.0, 1.0) * sc);
            p.setBrush(c);
            const double ds = 0.7 + rng.generateDouble() * 1.1;
            p.drawEllipse(QPointF(pos.x() + std::cos(a) * rr, pos.y() + std::sin(a) * rr), ds, ds);
        }
        return;
    }

    QPointF at = pos;
    if (pr.scatter > 0) {
        const double sr = params.size * pr.scatter / 100.0 * 0.5;
        const double a = rng.generateDouble() * 2 * M_PI;
        const double rr = sr * std::sqrt(rng.generateDouble());
        at += QPointF(std::cos(a) * rr, std::sin(a) * rr);
    }
    const QImage& img = dabFor(cache, pi, int(std::ceil(r * 2.0)), pr.aliased ? 100 : params.hardness, pr.eraser ? QColor(Qt::black) : color);
    p.setOpacity(sc);
    if (pr.follow) {
        p.translate(at);
        p.rotate(cache.dir);
        p.drawImage(QPointF(-img.width() / 2.0, -img.height() / 2.0), img);
    } else {
        p.drawImage(at - QPointF(img.width() / 2.0, img.height() / 2.0), img);
    }
}

void brushStampAlong(QImage& buf, const BrushParams& params, const QColor& color, const QPointF& a, const QPointF& b,
                     double pa, double pb, double& carry, QRandomGenerator& rng, BrushDabCache& cache) {
    const BrushPreset& pr = kPresets[std::clamp(params.preset, 0, kCount - 1)];
    const double dx = b.x() - a.x(), dy = b.y() - a.y();
    const double dist = std::hypot(dx, dy);
    if (dist < 1e-6) return;
    const double step = std::max(1.0, params.size * pr.spacing / 100.0);
    cache.dir = std::atan2(dy, dx) * 180.0 / M_PI;
    double t = carry;
    while (t <= dist) {
        const double u = t / dist;
        brushStampAt(buf, params, color, QPointF(a.x() + dx * u, a.y() + dy * u), pa + (pb - pa) * u, rng, cache);
        t += step;
    }
    carry = t - dist;
}

void brushApplyGrain(QImage& buf, int grain, QPoint origin) {
    if (grain <= 0) return;
    static QImage tile[101];
    const int g = std::clamp(grain, 1, 100);
    if (tile[g].isNull()) {
        QImage t(128, 128, QImage::Format_ARGB32_Premultiplied);
        QRandomGenerator rng(4242);
        for (int y = 0; y < 128; ++y) {
            QRgb* s = reinterpret_cast<QRgb*>(t.scanLine(y));
            for (int x = 0; x < 128; ++x) {
                // 값이 클수록 구멍이 많다. grain 이 세면 대부분이 비어 거칠게 보인다.
                const double n = rng.generateDouble();
                const double keep = std::clamp(1.0 - g / 100.0 * n * 1.35, 0.0, 1.0);
                s[x] = qRgba(0, 0, 0, int(keep * 255));
            }
        }
        tile[g] = t;
    }
    QPainter p(&buf);
    p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    QBrush gb(tile[g]);
    gb.setTransform(QTransform::fromTranslate(-origin.x(), -origin.y()));
    p.fillRect(buf.rect(), gb);
}

QImage brushPreview(const BrushParams& params, const QColor& color, QSize size) {
    QImage buf(size, QImage::Format_ARGB32_Premultiplied);
    buf.fill(Qt::transparent);
    QRandomGenerator rng(99);
    BrushDabCache cache;
    BrushParams p = params;
    // 미리보기 크기를 영역에 맞게 줄인다
    p.size = std::min(params.size, std::max(4, size.height() * 2 / 3));
    double carry = 0;
    const int n = 48;
    QPointF prev;
    double prevPr = 0.0;
    for (int i = 0; i <= n; ++i) {
        const double u = i / double(n);
        const QPointF pt(size.width() * (0.08 + 0.84 * u), size.height() * (0.5 + 0.28 * std::sin(u * 2 * M_PI)));
        const double pr = std::sin(u * M_PI); // 양끝은 가볍게, 가운데는 세게
        if (i == 0) brushStampAt(buf, p, color, pt, pr, rng, cache);
        else brushStampAlong(buf, p, color, prev, pt, prevPr, pr, carry, rng, cache);
        prev = pt;
        prevPr = pr;
    }
    brushApplyGrain(buf, brushPreset(p.preset).grain);
    if (p.opacity < 100) {
        QImage o(buf.size(), QImage::Format_ARGB32_Premultiplied);
        o.fill(Qt::transparent);
        QPainter qp(&o);
        qp.setOpacity(p.opacity / 100.0);
        qp.drawImage(0, 0, buf);
        return o;
    }
    return buf;
}
