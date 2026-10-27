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


// ---------------------------------------------------------------- 뼈대(본) 변형
namespace {
// 점에서 선분까지의 거리
double segDistance(const QPointF& p, const QPointF& a, const QPointF& b) {
    const QPointF ab = b - a;
    const double len2 = ab.x() * ab.x() + ab.y() * ab.y();
    if (len2 < 1e-9) return std::hypot(p.x() - a.x(), p.y() - a.y());
    double t = ((p.x() - a.x()) * ab.x() + (p.y() - a.y()) * ab.y()) / len2;
    t = std::clamp(t, 0.0, 1.0);
    const QPointF c = a + ab * t;
    return std::hypot(p.x() - c.x(), p.y() - c.y());
}

// (a,b) 를 (da,db) 로 옮기는 닮음 변환 (회전 + 크기 + 이동)
QTransform similarity(const QPointF& a, const QPointF& b, const QPointF& da, const QPointF& db) {
    const QPointF u = b - a, v = db - da;
    const double lu = std::hypot(u.x(), u.y()), lv = std::hypot(v.x(), v.y());
    if (lu < 1e-6) return QTransform::fromTranslate(da.x() - a.x(), da.y() - a.y());
    const double s = lv < 1e-6 ? 1.0 : lv / lu;
    const double ang = std::atan2(v.y(), v.x()) - std::atan2(u.y(), u.x());
    const double cs = std::cos(ang) * s, sn = std::sin(ang) * s;
    // p' = R*S*(p - a) + da
    QTransform t(cs, sn, -sn, cs, 0, 0);
    const QPointF moved = t.map(QPointF(0, 0));
    Q_UNUSED(moved);
    QTransform full;
    full.translate(da.x(), da.y());
    full = QTransform(cs, sn, -sn, cs, da.x(), da.y());
    // (p - a) 를 먼저 빼야 하므로 앞에 평행이동을 붙인다
    QTransform pre;
    pre.translate(-a.x(), -a.y());
    return pre * full;
}

// 가중치: 뼈에서 멀어질수록 0 에 가까워진다
double boneWeight(double dist, double radius) {
    if (radius <= 1e-6) return 0.0;
    const double t = std::clamp(dist / radius, 0.0, 1.0);
    const double w = 1.0 - t;
    return w * w * w;   // 가까운 곳이 세게 끌린다
}
} // namespace

QPointF boneMapPoint(const QVector<Bone>& bones, const QPointF& p, bool followNearest) {
    double sum = 0;
    QPointF acc(0, 0);
    int nearest = -1;
    double nearestD = 1e18;
    for (int i = 0; i < bones.size(); ++i) {
        const Bone& bn = bones[i];
        const double d = segDistance(p, bn.a, bn.b);
        if (d < nearestD) {
            nearestD = d;
            nearest = i;
        }
        const double w = boneWeight(d, bn.radius);
        if (w <= 1e-6) continue;
        acc += similarity(bn.a, bn.b, bn.da, bn.db).map(p) * w;
        sum += w;
    }
    if (sum > 1e-6) return acc / sum;
    if (followNearest && nearest >= 0)
        return similarity(bones[nearest].a, bones[nearest].b, bones[nearest].da, bones[nearest].db).map(p);
    return p;
}

QImage boneWarp(const QImage& src, const QVector<Bone>& bones, bool followNearest) {
    if (bones.isEmpty()) return src;
    const QImage s = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QImage out(s.size(), QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    // 역방향 매핑: 결과 픽셀마다 원본 어디에서 왔는지 찾는다.
    // 가중치는 "옮긴 뒤의 뼈" 기준으로 재고, 변환은 거꾸로 적용한다.
    QVector<QTransform> inv(bones.size());
    for (int i = 0; i < bones.size(); ++i) {
        bool ok = false;
        inv[i] = similarity(bones[i].a, bones[i].b, bones[i].da, bones[i].db).inverted(&ok);
        if (!ok) inv[i] = QTransform();
    }
    const int w = s.width(), h = s.height();
    for (int y = 0; y < h; ++y) {
        QRgb* d = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const QPointF q(x, y);
            double sum = 0;
            QPointF acc(0, 0);
            int nearest = -1;
            double nearestD = 1e18;
            for (int i = 0; i < bones.size(); ++i) {
                const double dd = segDistance(q, bones[i].da, bones[i].db);
                if (dd < nearestD) {
                    nearestD = dd;
                    nearest = i;
                }
                const double wt = boneWeight(dd, bones[i].radius);
                if (wt <= 1e-6) continue;
                acc += inv[i].map(q) * wt;
                sum += wt;
            }
            QPointF sp = q;
            if (sum > 1e-6) sp = acc / sum;
            else if (followNearest && nearest >= 0) sp = inv[nearest].map(q);
            // 쌍선형 샘플링
            const double fx = sp.x(), fy = sp.y();
            if (fx < 0 || fy < 0 || fx > w - 1 || fy > h - 1) {
                d[x] = 0;
                continue;
            }
            const int x0 = int(fx), y0 = int(fy);
            const int x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
            const double ux = fx - x0, uy = fy - y0;
            const QRgb* r0 = reinterpret_cast<const QRgb*>(s.constScanLine(y0));
            const QRgb* r1 = reinterpret_cast<const QRgb*>(s.constScanLine(y1));
            auto mix = [&](int shift) {
                const double a = (r0[x0] >> shift & 255) * (1 - ux) + (r0[x1] >> shift & 255) * ux;
                const double b = (r1[x0] >> shift & 255) * (1 - ux) + (r1[x1] >> shift & 255) * ux;
                return int(a * (1 - uy) + b * uy + 0.5);
            };
            d[x] = qRgba(mix(16), mix(8), mix(0), mix(24));
        }
    }
    return out;
}


}
