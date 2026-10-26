#include "Warp.h"

#include <QPainter>
#include <QPolygonF>
#include <algorithm>
#include <cmath>

namespace Warp {

namespace {
struct Anchor {
    float x, y, dx, dy;
};

inline QRgb sampleBilinear(const QImage& s, float x, float y) {
    const int w = s.width(), h = s.height();
    x -= 0.5f;
    y -= 0.5f;
    const int x0 = int(std::floor(x)), y0 = int(std::floor(y));
    const float fx = x - x0, fy = y - y0;
    auto px = [&](int xx, int yy) -> QRgb {
        if (xx < 0 || yy < 0 || xx >= w || yy >= h) return 0;
        return reinterpret_cast<const QRgb*>(s.constScanLine(yy))[xx];
    };
    const QRgb c00 = px(x0, y0), c10 = px(x0 + 1, y0), c01 = px(x0, y0 + 1), c11 = px(x0 + 1, y0 + 1);
    auto mix = [&](int (*ch)(QRgb)) {
        const float top = ch(c00) * (1 - fx) + ch(c10) * fx;
        const float bot = ch(c01) * (1 - fx) + ch(c11) * fx;
        return int(top * (1 - fy) + bot * fy + 0.5f);
    };
    return qRgba(mix(qRed), mix(qGreen), mix(qBlue), mix(qAlpha));
}
}

QImage pinWarp(const QImage& src, const QVector<Pin>& pins) {
    if (pins.isEmpty()) return src;
    const QImage s = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const int w = s.width(), h = s.height();
    QImage out(w, h, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);

    QVector<Anchor> a;
    for (const Pin& p : pins) {
        if (p.fixed) a.push_back({float(p.src.x()), float(p.src.y()), 0.f, 0.f});
        else a.push_back({float(p.dst.x()), float(p.dst.y()), float(p.dst.x() - p.src.x()), float(p.dst.y() - p.src.y())});
    }
    // 가장자리 고정점
    const float xs[3] = {0.f, w / 2.f, float(w)};
    const float ys[3] = {0.f, h / 2.f, float(h)};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            if (!(i == 1 && j == 1)) a.push_back({xs[i], ys[j], 0.f, 0.f});

    for (int y = 0; y < h; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(out.scanLine(y));
        const float py = y + 0.5f;
        for (int x = 0; x < w; ++x) {
            const float px = x + 0.5f;
            float sw = 0.f, sdx = 0.f, sdy = 0.f;
            bool exact = false;
            for (const Anchor& an : a) {
                const float dx = px - an.x, dy = py - an.y;
                const float d2 = dx * dx + dy * dy;
                if (d2 < 1e-3f) {
                    sdx = an.dx;
                    sdy = an.dy;
                    sw = 1.f;
                    exact = true;
                    break;
                }
                const float wgt = 1.0f / d2;
                sw += wgt;
                sdx += wgt * an.dx;
                sdy += wgt * an.dy;
            }
            Q_UNUSED(exact);
            const float ox = sdx / sw, oy = sdy / sw;
            line[x] = sampleBilinear(s, px - ox, py - oy);
        }
    }
    return out;
}

QImage transformRegion(const QImage& src, QRect region, const QTransform& t) {
    QImage base = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const QRect full(QPoint(0, 0), base.size());
    if (!region.isValid() || region.isEmpty()) region = full;
    region = region.intersected(full);
    if (region.isEmpty()) return base;
    const QImage content = base.copy(region);
    {
        QPainter p(&base);
        p.setCompositionMode(QPainter::CompositionMode_Clear);
        p.fillRect(region, Qt::black);
    }
    QPainter p(&base);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setTransform(t);
    p.drawImage(region.topLeft(), content);
    return base;
}

QTransform makeTransform(const QRect& regionIn, const QSize& imageSize, double dx, double dy, double scalePct,
                         double rotationDeg, bool flipH) {
    QRect region = regionIn;
    if (!region.isValid() || region.isEmpty()) region = QRect(QPoint(0, 0), imageSize);
    const QPointF c = QRectF(region).center();
    const double s = std::max(0.01, scalePct / 100.0);
    QTransform t;
    t.translate(dx, dy);
    t.translate(c.x(), c.y());
    t.rotate(rotationDeg);
    t.scale(flipH ? -s : s, s);
    t.translate(-c.x(), -c.y());
    return t;
}

bool makePerspective(const QRect& regionIn, const QSize& imageSize, const QPointF offsets[4], QTransform* out) {
    QRect region = regionIn;
    if (!region.isValid() || region.isEmpty()) region = QRect(QPoint(0, 0), imageSize);
    const QRectF r(region);
    QPolygonF from;
    from << r.topLeft() << r.topRight() << r.bottomRight() << r.bottomLeft();
    QPolygonF to;
    for (int i = 0; i < 4; ++i) to << from[i] + offsets[i];
    return QTransform::quadToQuad(from, to, *out);
}

namespace {
bool triAffine(const QPointF s[3], const QPointF d[3], QTransform* out) {
    const double det = (s[1].x() - s[0].x()) * (s[2].y() - s[0].y()) - (s[2].x() - s[0].x()) * (s[1].y() - s[0].y());
    if (std::abs(det) < 1e-9) return false;
    // d = M * s + t 를 풀어 a,b,c(x), d,e,f(y) 구한다
    auto solve = [&](double v0, double v1, double v2, double& a, double& b, double& c) {
        a = ((v1 - v0) * (s[2].y() - s[0].y()) - (v2 - v0) * (s[1].y() - s[0].y())) / det;
        b = ((v2 - v0) * (s[1].x() - s[0].x()) - (v1 - v0) * (s[2].x() - s[0].x())) / det;
        c = v0 - a * s[0].x() - b * s[0].y();
    };
    double a, b, c, d2, e, f;
    solve(d[0].x(), d[1].x(), d[2].x(), a, b, c);
    solve(d[0].y(), d[1].y(), d[2].y(), d2, e, f);
    *out = QTransform(a, d2, b, e, c, f);
    return true;
}
}

QImage meshWarp(const QImage& src, int cols, int rows, const QVector<QPointF>& dst) {
    if (cols < 1 || rows < 1 || dst.size() != (cols + 1) * (rows + 1)) return src;
    const QImage s = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QImage out(s.size(), QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const double cw = double(s.width()) / cols, ch = double(s.height()) / rows;
    auto sp = [&](int c, int r) { return QPointF(c * cw, r * ch); };
    auto dp = [&](int c, int r) { return dst[r * (cols + 1) + c]; };
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            const QPointF S[4] = {sp(c, r), sp(c + 1, r), sp(c + 1, r + 1), sp(c, r + 1)};
            const QPointF D[4] = {dp(c, r), dp(c + 1, r), dp(c + 1, r + 1), dp(c, r + 1)};
            const int tri[2][3] = {{0, 1, 2}, {0, 2, 3}};
            for (const auto& t : tri) {
                const QPointF ts[3] = {S[t[0]], S[t[1]], S[t[2]]};
                const QPointF td[3] = {D[t[0]], D[t[1]], D[t[2]]};
                QTransform m;
                if (!triAffine(ts, td, &m)) continue;
                const QPointF cen = (td[0] + td[1] + td[2]) / 3.0;
                QPolygonF poly;
                for (const QPointF& v : td) {
                    QPointF dir = v - cen;
                    const double len = std::hypot(dir.x(), dir.y());
                    poly << (len > 0 ? v + dir / len * 0.7 : v);
                }
                p.save();
                p.setClipRegion(QRegion(poly.toPolygon()));
                p.setTransform(m);
                p.drawImage(0, 0, s);
                p.restore();
            }
        }
    }
    return out;
}

}
