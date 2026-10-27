#include "Effects.h"
#include <QVector>
#include <QPainter>
#include <QRandomGenerator>
#include <algorithm>
#include <cmath>

namespace {
template <typename F>
QImage mapStraight(const QImage& in, F f) {
    QImage o = in.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < o.height(); ++y) {
        QRgb* s = reinterpret_cast<QRgb*>(o.scanLine(y));
        for (int x = 0; x < o.width(); ++x) s[x] = f(s[x]);
    }
    return o.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}
inline int clamp255(int v) { return std::clamp(v, 0, 255); }
}

namespace Effects {

QImage invert(const QImage& in) {
    return mapStraight(in, [](QRgb c) {
        return qRgba(255 - qRed(c), 255 - qGreen(c), 255 - qBlue(c), qAlpha(c));
    });
}

QImage grayscale(const QImage& in) {
    return mapStraight(in, [](QRgb c) {
        const int g = (qRed(c) * 299 + qGreen(c) * 587 + qBlue(c) * 114) / 1000;
        return qRgba(g, g, g, qAlpha(c));
    });
}

QImage brightness(const QImage& in, int d) {
    return mapStraight(in, [d](QRgb c) {
        return qRgba(clamp255(qRed(c) + d), clamp255(qGreen(c) + d), clamp255(qBlue(c) + d), qAlpha(c));
    });
}

QImage boxBlur(const QImage& in, int r) {
    if (r < 1) return in;
    const QImage src = in.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const int w = src.width(), h = src.height();
    QImage tmp(w, h, QImage::Format_ARGB32_Premultiplied);
    QImage dst(w, h, QImage::Format_ARGB32_Premultiplied);

    auto pass = [&](const QImage& a, QImage& b, bool horiz) {
        const int len = horiz ? w : h;
        const int lines = horiz ? h : w;
        QVector<int> P[4];
        for (auto& v : P) v.resize(len + 1);
        for (int ln = 0; ln < lines; ++ln) {
            for (int c = 0; c < 4; ++c) P[c][0] = 0;
            for (int i = 0; i < len; ++i) {
                const QRgb px = horiz ? reinterpret_cast<const QRgb*>(a.constScanLine(ln))[i]
                                      : reinterpret_cast<const QRgb*>(a.constScanLine(i))[ln];
                P[0][i + 1] = P[0][i] + qAlpha(px);
                P[1][i + 1] = P[1][i] + qRed(px);
                P[2][i + 1] = P[2][i] + qGreen(px);
                P[3][i + 1] = P[3][i] + qBlue(px);
            }
            for (int i = 0; i < len; ++i) {
                const int lo = std::max(0, i - r), hi = std::min(len - 1, i + r);
                const int n = hi - lo + 1;
                const QRgb out = qRgba((P[1][hi + 1] - P[1][lo]) / n, (P[2][hi + 1] - P[2][lo]) / n,
                                       (P[3][hi + 1] - P[3][lo]) / n, (P[0][hi + 1] - P[0][lo]) / n);
                if (horiz) reinterpret_cast<QRgb*>(b.scanLine(ln))[i] = out;
                else reinterpret_cast<QRgb*>(b.scanLine(i))[ln] = out;
            }
        }
    };
    pass(src, tmp, true);
    pass(tmp, dst, false);
    return dst;
}


namespace {
// 프리멀티플라이드 ARGB 이미지에서 쌍선형 샘플링
QRgb sampleBilinear(const QImage& im, double x, double y) {
    if (x < 0 || y < 0 || x > im.width() - 1 || y > im.height() - 1) return 0;
    const int x0 = int(x), y0 = int(y);
    const int x1 = std::min(x0 + 1, im.width() - 1), y1 = std::min(y0 + 1, im.height() - 1);
    const double fx = x - x0, fy = y - y0;
    const QRgb* r0 = reinterpret_cast<const QRgb*>(im.constScanLine(y0));
    const QRgb* r1 = reinterpret_cast<const QRgb*>(im.constScanLine(y1));
    auto mix = [&](int shift) {
        const double a = (r0[x0] >> shift & 255) * (1 - fx) + (r0[x1] >> shift & 255) * fx;
        const double b = (r1[x0] >> shift & 255) * (1 - fx) + (r1[x1] >> shift & 255) * fx;
        return int(a * (1 - fy) + b * fy + 0.5);
    };
    return qRgba(mix(16), mix(8), mix(0), mix(24));
}

template <typename F>
QImage warpImage(const QImage& in, F map) {
    const QImage src = in.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QImage dst(src.size(), QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < dst.height(); ++y) {
        QRgb* d = reinterpret_cast<QRgb*>(dst.scanLine(y));
        for (int x = 0; x < dst.width(); ++x) {
            double sx = x, sy = y;
            map(sx, sy);
            d[x] = sampleBilinear(src, sx, sy);
        }
    }
    return dst;
}

void rgb2hsl(int r, int g, int b, double& h, double& s, double& l) {
    const double R = r / 255.0, G = g / 255.0, B = b / 255.0;
    const double mx = std::max({R, G, B}), mn = std::min({R, G, B});
    l = (mx + mn) / 2;
    const double d = mx - mn;
    if (d < 1e-9) { h = 0; s = 0; return; }
    s = l > 0.5 ? d / (2 - mx - mn) : d / (mx + mn);
    if (mx == R) h = (G - B) / d + (G < B ? 6 : 0);
    else if (mx == G) h = (B - R) / d + 2;
    else h = (R - G) / d + 4;
    h /= 6;
}
double hue2rgb(double p, double q, double t) {
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1.0 / 6) return p + (q - p) * 6 * t;
    if (t < 0.5) return q;
    if (t < 2.0 / 3) return p + (q - p) * (2.0 / 3 - t) * 6;
    return p;
}
QRgb hsl2rgb(double h, double s, double l, int a) {
    double r, g, b;
    if (s < 1e-9) r = g = b = l;
    else {
        const double q = l < 0.5 ? l * (1 + s) : l + s - l * s, p = 2 * l - q;
        r = hue2rgb(p, q, h + 1.0 / 3); g = hue2rgb(p, q, h); b = hue2rgb(p, q, h - 1.0 / 3);
    }
    return qRgba(clamp255(int(r * 255 + 0.5)), clamp255(int(g * 255 + 0.5)), clamp255(int(b * 255 + 0.5)), a);
}

// 3x3 컨볼루션 (스트레이트 알파 RGB 에만 적용, 알파 유지)
QImage convolve3(const QImage& in, const double k[9], double bias, bool onAlpha) {
    const QImage s = in.convertToFormat(QImage::Format_ARGB32);
    QImage o = s;
    const int w = s.width(), h = s.height();
    for (int y = 0; y < h; ++y) {
        QRgb* d = reinterpret_cast<QRgb*>(o.scanLine(y));
        for (int x = 0; x < w; ++x) {
            double a = 0, r = 0, g = 0, b = 0;
            for (int j = -1; j <= 1; ++j)
                for (int i = -1; i <= 1; ++i) {
                    const int xx = std::clamp(x + i, 0, w - 1), yy = std::clamp(y + j, 0, h - 1);
                    const QRgb p = reinterpret_cast<const QRgb*>(s.constScanLine(yy))[xx];
                    const double kv = k[(j + 1) * 3 + (i + 1)];
                    a += qAlpha(p) * kv; r += qRed(p) * kv; g += qGreen(p) * kv; b += qBlue(p) * kv;
                }
            const int oa = onAlpha ? clamp255(int(a + bias)) : qAlpha(d[x]);
            d[x] = qRgba(clamp255(int(r + bias)), clamp255(int(g + bias)), clamp255(int(b + bias)), oa);
        }
    }
    return o.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

QImage directionalBlur(const QImage& in, int len, bool horiz) {
    if (len < 1) return in;
    const QImage src = in.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QImage dst(src.size(), QImage::Format_ARGB32_Premultiplied);
    const int w = src.width(), h = src.height();
    for (int y = 0; y < h; ++y) {
        QRgb* d = reinterpret_cast<QRgb*>(dst.scanLine(y));
        for (int x = 0; x < w; ++x) {
            int a = 0, r = 0, g = 0, b = 0, n = 0;
            for (int t = -len; t <= len; ++t) {
                const int xx = horiz ? x + t : x, yy = horiz ? y : y + t;
                if (xx < 0 || yy < 0 || xx >= w || yy >= h) continue;
                const QRgb p = reinterpret_cast<const QRgb*>(src.constScanLine(yy))[xx];
                a += qAlpha(p); r += qRed(p); g += qGreen(p); b += qBlue(p); ++n;
            }
            d[x] = n ? qRgba(r / n, g / n, b / n, a / n) : 0;
        }
    }
    return dst;
}

// 알파 마스크를 흐리게 → 그림자/외곽선에 사용
QImage alphaOnly(const QImage& in, QRgb color) {
    QImage o = in.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < o.height(); ++y) {
        QRgb* s = reinterpret_cast<QRgb*>(o.scanLine(y));
        for (int x = 0; x < o.width(); ++x) {
            const int a = qAlpha(s[x]);
            s[x] = qRgba(qRed(color) * a / 255, qGreen(color) * a / 255, qBlue(color) * a / 255, a);
        }
    }
    return o;
}
}


QImage gaussBlur(const QImage& in, int r) {
    if (r < 1) return in;
    return boxBlur(boxBlur(boxBlur(in, std::max(1, r / 2)), std::max(1, r / 2)), std::max(1, r / 2 + 1));
}
QImage contrast(const QImage& in, int amount) {
    const double f = (259.0 * (amount + 255)) / (255.0 * (259 - amount));
    return mapStraight(in, [f](QRgb c) {
        return qRgba(clamp255(int(f * (qRed(c) - 128) + 128)), clamp255(int(f * (qGreen(c) - 128) + 128)),
                     clamp255(int(f * (qBlue(c) - 128) + 128)), qAlpha(c));
    });
}
QImage saturation(const QImage& in, int amount) {
    return mapStraight(in, [amount](QRgb c) {
        double h, s, l;
        rgb2hsl(qRed(c), qGreen(c), qBlue(c), h, s, l);
        s = std::clamp(s * (100 + amount) / 100.0, 0.0, 1.0);
        return hsl2rgb(h, s, l, qAlpha(c));
    });
}
QImage hueShift(const QImage& in, int degrees) {
    return mapStraight(in, [degrees](QRgb c) {
        double h, s, l;
        rgb2hsl(qRed(c), qGreen(c), qBlue(c), h, s, l);
        h = std::fmod(h + degrees / 360.0 + 2.0, 1.0);
        return hsl2rgb(h, s, l, qAlpha(c));
    });
}
QImage sepia(const QImage& in, int) {
    return mapStraight(in, [](QRgb c) {
        const int r = qRed(c), g = qGreen(c), b = qBlue(c);
        return qRgba(clamp255(int(r * .393 + g * .769 + b * .189)), clamp255(int(r * .349 + g * .686 + b * .168)),
                     clamp255(int(r * .272 + g * .534 + b * .131)), qAlpha(c));
    });
}
QImage posterize(const QImage& in, int levels) {
    levels = std::clamp(levels, 2, 16);
    return mapStraight(in, [levels](QRgb c) {
        auto q = [levels](int v) { return clamp255(int(std::round(v * (levels - 1) / 255.0) * 255.0 / (levels - 1))); };
        return qRgba(q(qRed(c)), q(qGreen(c)), q(qBlue(c)), qAlpha(c));
    });
}
QImage threshold(const QImage& in, int t) {
    return mapStraight(in, [t](QRgb c) {
        const int g = (qRed(c) * 299 + qGreen(c) * 587 + qBlue(c) * 114) / 1000;
        const int v = g >= t ? 255 : 0;
        return qRgba(v, v, v, qAlpha(c));
    });
}
QImage wave(const QImage& in, int amp) {
    const double period = std::max(20.0, in.height() / 6.0);
    return warpImage(in, [&](double& x, double& y) { x += amp * std::sin(y / period * 2 * M_PI); });
}
QImage swirl(const QImage& in, int strength) {
    const double cx = in.width() / 2.0, cy = in.height() / 2.0, R = std::min(cx, cy);
    const double k = strength / 100.0 * 3.0;
    return warpImage(in, [&](double& x, double& y) {
        const double dx = x - cx, dy = y - cy, d = std::hypot(dx, dy);
        if (d >= R) return;
        const double ang = k * (1 - d / R) * (1 - d / R);
        const double c = std::cos(ang), s = std::sin(ang);
        x = cx + dx * c - dy * s;
        y = cy + dx * s + dy * c;
    });
}
QImage bulge(const QImage& in, int strength) {
    const double cx = in.width() / 2.0, cy = in.height() / 2.0, R = std::min(cx, cy);
    const double k = strength / 100.0;
    return warpImage(in, [&](double& x, double& y) {
        const double dx = x - cx, dy = y - cy, d = std::hypot(dx, dy);
        if (d >= R || d < 1e-6) return;
        const double n = d / R;
        const double nn = k >= 0 ? std::pow(n, 1.0 + k) : std::pow(n, 1.0 / (1.0 - k));
        x = cx + dx / d * nn * R;
        y = cy + dy / d * nn * R;
    });
}
QImage keyColor(const QImage& in, int mode, int tol) {
    return mapStraight(in, [mode, tol](QRgb c) {
        const int r = qRed(c), g = qGreen(c), b = qBlue(c);
        bool hit = false;
        if (mode == 0) hit = std::min({r, g, b}) >= 255 - tol;
        else if (mode == 1) hit = std::max({r, g, b}) <= tol;
        else hit = g > r + tol / 2 && g > b + tol / 2 && g > 60;
        return hit ? qRgba(r, g, b, 0) : c;
    });
}
QImage noise(const QImage& in, int amount) {
    QRandomGenerator rng(12345);
    return mapStraight(in, [&](QRgb c) {
        const int n = int(rng.bounded(2 * amount + 1)) - amount;
        return qRgba(clamp255(qRed(c) + n), clamp255(qGreen(c) + n), clamp255(qBlue(c) + n), qAlpha(c));
    });
}
QImage vignette(const QImage& in, int amount) {
    QImage o = in.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const double cx = o.width() / 2.0, cy = o.height() / 2.0, R = std::hypot(cx, cy);
    for (int y = 0; y < o.height(); ++y) {
        QRgb* s = reinterpret_cast<QRgb*>(o.scanLine(y));
        for (int x = 0; x < o.width(); ++x) {
            const double d = std::hypot(x - cx, y - cy) / R;
            const double f = std::clamp(1.0 - amount / 100.0 * d * d, 0.0, 1.0);
            s[x] = qRgba(int(qRed(s[x]) * f), int(qGreen(s[x]) * f), int(qBlue(s[x]) * f), qAlpha(s[x]));
        }
    }
    return o;
}
QImage dropShadow(const QImage& in, int dist) {
    QImage sh = gaussBlur(alphaOnly(in, qRgba(0, 0, 0, 255)), std::max(1, dist / 2));
    QImage o(in.size(), QImage::Format_ARGB32_Premultiplied);
    o.fill(Qt::transparent);
    QPainter p(&o);
    p.setOpacity(0.6);
    p.drawImage(dist, dist, sh);
    p.setOpacity(1.0);
    p.drawImage(0, 0, in);
    return o;
}
QImage pixelate(const QImage& in, int block) {
    block = std::max(2, block);
    const QImage s = in.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const QSize small(std::max(1, s.width() / block), std::max(1, s.height() / block));
    return s.scaled(small, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).scaled(s.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation);
}
QImage emboss(const QImage& in, int) {
    static const double k[9] = {-2, -1, 0, -1, 1, 1, 0, 1, 2};
    return convolve3(in, k, 128, false);
}
QImage edge(const QImage& in, int) {
    static const double k[9] = {-1, -1, -1, -1, 8, -1, -1, -1, -1};
    return convolve3(in, k, 0, false);
}
QImage sharpen(const QImage& in, int amount) {
    const double a = amount / 100.0;
    const double k[9] = {0, -a, 0, -a, 1 + 4 * a, -a, 0, -a, 0};
    return convolve3(in, k, 0, false);
}
QImage outline(const QImage& in, int width) {
    width = std::clamp(width, 1, 20);
    const QImage src = in.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QImage o(src.size(), QImage::Format_ARGB32_Premultiplied);
    o.fill(Qt::transparent);
    const QImage mask = alphaOnly(src, qRgba(0, 0, 0, 255));
    QPainter p(&o);
    for (int a = 0; a < 16; ++a) {
        const double ang = a * M_PI / 8;
        p.drawImage(QPointF(std::cos(ang) * width, std::sin(ang) * width), mask);
    }
    p.drawImage(0, 0, src);
    return o;
}
QImage echo(const QImage& in, int dist) {
    QImage o(in.size(), QImage::Format_ARGB32_Premultiplied);
    o.fill(Qt::transparent);
    QPainter p(&o);
    for (int i = 3; i >= 1; --i) {
        p.setOpacity(0.25 * (4 - i) / 3.0 + 0.1);
        p.drawImage(-dist * i, 0, in);
    }
    p.setOpacity(1.0);
    p.drawImage(0, 0, in);
    return o;
}

// 알파 모양의 부호 있는 거리장 (안쪽은 음수, 바깥쪽은 양수, 단위는 픽셀)
static QVector<float> signedDistance(const QImage& src) {
    const QImage s = src.convertToFormat(QImage::Format_ARGB32);
    const int w = s.width(), h = s.height();
    const float big = float(w + h);
    QVector<float> din(w * h, big), dout(w * h, big);
    for (int y = 0; y < h; ++y) {
        const QRgb* r = reinterpret_cast<const QRgb*>(s.constScanLine(y));
        for (int x = 0; x < w; ++x) (qAlpha(r[x]) >= 128 ? din : dout)[y * w + x] = 0.0f;
    }
    // 체임퍼 거리: 두 번 훑는다
    auto chamfer = [&](QVector<float>& d) {
        const float a = 1.0f, b = 1.4142f;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                float v = d[y * w + x];
                if (x > 0) v = std::min(v, d[y * w + x - 1] + a);
                if (y > 0) v = std::min(v, d[(y - 1) * w + x] + a);
                if (x > 0 && y > 0) v = std::min(v, d[(y - 1) * w + x - 1] + b);
                if (x + 1 < w && y > 0) v = std::min(v, d[(y - 1) * w + x + 1] + b);
                d[y * w + x] = v;
            }
        for (int y = h - 1; y >= 0; --y)
            for (int x = w - 1; x >= 0; --x) {
                float v = d[y * w + x];
                if (x + 1 < w) v = std::min(v, d[y * w + x + 1] + a);
                if (y + 1 < h) v = std::min(v, d[(y + 1) * w + x] + a);
                if (x + 1 < w && y + 1 < h) v = std::min(v, d[(y + 1) * w + x + 1] + b);
                if (x > 0 && y + 1 < h) v = std::min(v, d[(y + 1) * w + x - 1] + b);
                d[y * w + x] = v;
            }
    };
    chamfer(din);
    chamfer(dout);
    QVector<float> sd(w * h);
    for (int i = 0; i < w * h; ++i) sd[i] = din[i] - dout[i];   // 안쪽 음수, 바깥쪽 양수
    return sd;
}

// 알파의 무게중심 (그림이 비어 있으면 가운데)
static QPointF alphaCentroid(const QImage& s) {
    double sx = 0, sy = 0, sw = 0;
    for (int y = 0; y < s.height(); ++y) {
        const QRgb* r = reinterpret_cast<const QRgb*>(s.constScanLine(y));
        for (int x = 0; x < s.width(); ++x) {
            const double a = qAlpha(r[x]) / 255.0;
            if (a <= 0.01) continue;
            sx += x * a;
            sy += y * a;
            sw += a;
        }
    }
    if (sw < 1e-6) return QPointF(s.width() / 2.0, s.height() / 2.0);
    return QPointF(sx / sw, sy / sw);
}

static QImage shiftImage(const QImage& s, const QPointF& d) {
    QImage out(s.size(), QImage::Format_ARGB32);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.drawImage(d, s);
    p.end();
    return out;
}

QImage morph(const QImage& a, const QImage& b, double t) {
    if (a.size() != b.size()) return crossfade(a, b, t);
    t = std::clamp(t, 0.0, 1.0);
    QImage A = a.convertToFormat(QImage::Format_ARGB32);
    QImage B = b.convertToFormat(QImage::Format_ARGB32);
    const int w = A.width(), h = A.height();
    // 두 그림이 떨어져 있으면 거리장만으로는 이어지지 않는다.
    // 무게중심을 가운데로 모아 모양만 섞은 뒤, 결과를 중간 위치로 옮긴다.
    const QPointF ca = alphaCentroid(A), cb = alphaCentroid(B);
    const QPointF center(w / 2.0, h / 2.0);
    const QImage As = shiftImage(A, center - ca);
    const QImage Bs = shiftImage(B, center - cb);
    const QVector<float> da = signedDistance(As), db = signedDistance(Bs);
    // 가는 선은 중간에서 끊기기 쉽다. 두 그림의 선 굵기를 보고 중간 프레임만 살짝 두껍게 잡는다.
    auto halfThickness = [](const QVector<float>& d) {
        float deepest = 0;
        for (float v : d) deepest = std::min(deepest, v);
        return -deepest;   // 안쪽으로 가장 깊은 거리 = 대략 선 굵기의 절반
    };
    const double thick = std::min(6.0, 0.75 * (halfThickness(da) + halfThickness(db)) / 2.0);
    const double bias = thick * 4.0 * t * (1.0 - t);   // 양 끝에서는 0, 가운데에서 가장 큼
    QImage mid(w, h, QImage::Format_ARGB32);
    for (int y = 0; y < h; ++y) {
        const QRgb* ra = reinterpret_cast<const QRgb*>(As.constScanLine(y));
        const QRgb* rb = reinterpret_cast<const QRgb*>(Bs.constScanLine(y));
        QRgb* d = reinterpret_cast<QRgb*>(mid.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const int i = y * w + x;
            const double sd = da[i] * (1 - t) + db[i] * t;
            const double cover = std::clamp(0.5 - (sd - bias), 0.0, 1.0);
            if (cover <= 0.001) {
                d[x] = qRgba(0, 0, 0, 0);
                continue;
            }
            const int aa = qAlpha(ra[x]), ab = qAlpha(rb[x]);
            double wa = (1 - t) * (aa > 0 ? 1.0 : 0.15), wb = t * (ab > 0 ? 1.0 : 0.15);
            const double sum = wa + wb;
            if (sum < 1e-6) {
                wa = 1 - t;
                wb = t;
            } else {
                wa /= sum;
                wb /= sum;
            }
            d[x] = qRgba(clamp255(int(qRed(ra[x]) * wa + qRed(rb[x]) * wb)),
                         clamp255(int(qGreen(ra[x]) * wa + qGreen(rb[x]) * wb)),
                         clamp255(int(qBlue(ra[x]) * wa + qBlue(rb[x]) * wb)),
                         clamp255(int(std::round(cover * 255))));
        }
    }
    const QPointF target = ca * (1 - t) + cb * t;
    return shiftImage(mid, target - center).convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

QImage crossfade(const QImage& a, const QImage& b, double t) {
    QImage o(a.size(), QImage::Format_ARGB32_Premultiplied);
    o.fill(Qt::transparent);
    QPainter p(&o);
    p.setOpacity(1.0 - t);
    p.drawImage(0, 0, a);
    p.setOpacity(t);
    p.drawImage(0, 0, b);
    return o;
}
QImage wipe(const QImage& a, const QImage& b, double t, int dir) {
    QImage o = a.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QPainter p(&o);
    const int w = o.width(), h = o.height();
    QRect r;
    switch (dir) {
    case 0: r = QRect(0, 0, int(w * t), h); break;
    case 1: r = QRect(w - int(w * t), 0, int(w * t), h); break;
    case 2: r = QRect(0, 0, w, int(h * t)); break;
    default: r = QRect(0, h - int(h * t), w, int(h * t)); break;
    }
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.drawImage(r, b, r);
    return o;
}
QImage slide(const QImage& a, const QImage& b, double t) {
    QImage o(a.size(), QImage::Format_ARGB32_Premultiplied);
    o.fill(Qt::transparent);
    QPainter p(&o);
    const int dx = int(a.width() * t);
    p.drawImage(dx, 0, a);
    p.drawImage(dx - a.width(), 0, b);
    return o;
}

ParamInfo paramInfo(Type t) {
    switch (t) {
    case Type::Brightness: return {true, -100, 100, 24, QStringLiteral("밝기")};
    case Type::Blur: return {true, 1, 16, 4, QStringLiteral("반경")};
    case Type::GaussBlur: return {true, 1, 32, 6, QStringLiteral("반경")};
    case Type::Contrast: return {true, -100, 100, 30, QStringLiteral("대비")};
    case Type::Saturation: return {true, -100, 200, 40, QStringLiteral("채도 (%)")};
    case Type::HueShift: return {true, -180, 180, 30, QStringLiteral("색상 이동 (도)")};
    case Type::Posterize: return {true, 2, 16, 4, QStringLiteral("단계")};
    case Type::Threshold: return {true, 1, 254, 128, QStringLiteral("기준값")};
    case Type::Wave: return {true, 1, 60, 10, QStringLiteral("진폭")};
    case Type::Swirl: return {true, -100, 100, 50, QStringLiteral("세기")};
    case Type::Bulge: return {true, -90, 90, 50, QStringLiteral("세기")};
    case Type::KeyWhite: return {true, 0, 120, 20, QStringLiteral("허용 오차")};
    case Type::KeyBlack: return {true, 0, 120, 20, QStringLiteral("허용 오차")};
    case Type::KeyGreen: return {true, 10, 200, 60, QStringLiteral("허용 오차")};
    case Type::MotionBlurH: case Type::MotionBlurV: return {true, 1, 40, 8, QStringLiteral("길이")};
    case Type::Echo: return {true, 1, 60, 8, QStringLiteral("간격")};
    case Type::Noise: return {true, 1, 120, 30, QStringLiteral("양")};
    case Type::Vignette: return {true, 1, 100, 60, QStringLiteral("세기")};
    case Type::DropShadow: return {true, 1, 60, 8, QStringLiteral("거리")};
    case Type::Pixelate: return {true, 2, 64, 8, QStringLiteral("블록 크기")};
    case Type::Outline: return {true, 1, 20, 2, QStringLiteral("두께")};
    case Type::Sharpen: return {true, 10, 300, 100, QStringLiteral("세기")};
    case Type::Curves: return {true, -100, 100, 40, QStringLiteral("S 곡선 세기")};
    case Type::Levels: return {true, 0, 100, 30, QStringLiteral("대비 올리기")};
    default: return {};
    }
}

QString categoryOf(Type t) {
    switch (t) {
    case Type::Blur: case Type::GaussBlur: return QStringLiteral("블러");
    case Type::Invert: case Type::Grayscale: case Type::Brightness: case Type::Contrast: case Type::Saturation:
    case Type::HueShift: case Type::Sepia: case Type::Posterize: case Type::Threshold: return QStringLiteral("색");
    case Type::Wave: case Type::Swirl: case Type::Bulge: return QStringLiteral("왜곡");
    case Type::KeyWhite: case Type::KeyBlack: case Type::KeyGreen: return QStringLiteral("키잉");
    case Type::MotionBlurH: case Type::MotionBlurV: case Type::Echo: return QStringLiteral("모션");
    case Type::Noise: case Type::Vignette: case Type::DropShadow: return QStringLiteral("렌더링");
    case Type::Curves: case Type::Levels: return QStringLiteral("색");
    default: return QStringLiteral("스타일화");
    }
}

QStringList categories() {
    return {QStringLiteral("블러"), QStringLiteral("색"), QStringLiteral("왜곡"), QStringLiteral("키잉"),
            QStringLiteral("모션"), QStringLiteral("렌더링"), QStringLiteral("스타일화")};
}

QVector<Type> typesIn(const QString& category) {
    QVector<Type> v;
    for (int i = 0; i <= int(Type::Last); ++i)
        if (categoryOf(Type(i)) == category) v.push_back(Type(i));
    return v;
}

QImage curves(const QImage& in, const QVector<int>& lutR, const QVector<int>& lutG, const QVector<int>& lutB) {
    if (lutR.size() != 256 || lutG.size() != 256 || lutB.size() != 256) return in;
    return mapStraight(in, [&](QRgb c) {
        return qRgba(clamp255(lutR[qRed(c)]), clamp255(lutG[qGreen(c)]), clamp255(lutB[qBlue(c)]), qAlpha(c));
    });
}

QImage levels(const QImage& in, int inLow, int inHigh, double gamma, int outLow, int outHigh) {
    inLow = std::clamp(inLow, 0, 254);
    inHigh = std::clamp(inHigh, inLow + 1, 255);
    outLow = std::clamp(outLow, 0, 255);
    outHigh = std::clamp(outHigh, 0, 255);
    gamma = std::clamp(gamma, 0.05, 10.0);
    QVector<int> lut(256);
    for (int i = 0; i < 256; ++i) {
        double v = (double(i) - inLow) / double(inHigh - inLow);
        v = std::clamp(v, 0.0, 1.0);
        v = std::pow(v, 1.0 / gamma);
        lut[i] = clamp255(int(std::round(outLow + v * (outHigh - outLow))));
    }
    return mapStraight(in, [&](QRgb c) {
        return qRgba(lut[qRed(c)], lut[qGreen(c)], lut[qBlue(c)], qAlpha(c));
    });
}

QImage flipH(const QImage& in) { return in.mirrored(true, false); }
QImage flipV(const QImage& in) { return in.mirrored(false, true); }

QImage apply(const QImage& in, const Item& item) {
    switch (item.type) {
    case Type::Invert: return invert(in);
    case Type::Grayscale: return grayscale(in);
    case Type::Brightness: return brightness(in, item.param);
    case Type::Blur: return boxBlur(in, std::max(1, item.param));
    case Type::GaussBlur: return gaussBlur(in, item.param);
    case Type::Contrast: return contrast(in, item.param);
    case Type::Saturation: return saturation(in, item.param);
    case Type::HueShift: return hueShift(in, item.param);
    case Type::Sepia: return sepia(in, 0);
    case Type::Posterize: return posterize(in, item.param);
    case Type::Threshold: return threshold(in, item.param);
    case Type::Wave: return wave(in, item.param);
    case Type::Swirl: return swirl(in, item.param);
    case Type::Bulge: return bulge(in, item.param);
    case Type::KeyWhite: return keyColor(in, 0, item.param);
    case Type::KeyBlack: return keyColor(in, 1, item.param);
    case Type::KeyGreen: return keyColor(in, 2, item.param);
    case Type::MotionBlurH: return directionalBlur(in, item.param, true);
    case Type::MotionBlurV: return directionalBlur(in, item.param, false);
    case Type::Echo: return echo(in, item.param);
    case Type::Noise: return noise(in, item.param);
    case Type::Vignette: return vignette(in, item.param);
    case Type::DropShadow: return dropShadow(in, item.param);
    case Type::Pixelate: return pixelate(in, item.param);
    case Type::Emboss: return emboss(in, 0);
    case Type::Edge: return edge(in, 0);
    case Type::Outline: return outline(in, item.param);
    case Type::Sharpen: return sharpen(in, item.param);
    case Type::Curves: {   // FX 스택에서는 세기 하나로 S 커브를 준다 (-100 ~ 100)
        const double k = std::clamp(item.param, -100, 100) / 100.0;
        QVector<int> lut(256);
        for (int i = 0; i < 256; ++i) {
            const double x = i / 255.0;
            const double s = x + k * (x * (1 - x) * (x - 0.5) * 4.0);
            lut[i] = clamp255(int(std::round(std::clamp(s, 0.0, 1.0) * 255)));
        }
        return curves(in, lut, lut, lut);
    }
    case Type::Levels: {   // 세기 = 대비를 올리는 정도
        const int d = std::clamp(item.param, 0, 100) * 60 / 100;
        return levels(in, d, 255 - d, 1.0, 0, 255);
    }
    }
    return in;
}

QString typeName(Type t) {
    switch (t) {
    case Type::Invert: return QStringLiteral("색 반전");
    case Type::Grayscale: return QStringLiteral("흑백");
    case Type::Brightness: return QStringLiteral("밝기");
    case Type::Blur: return QStringLiteral("블러");
    case Type::GaussBlur: return QStringLiteral("가우시안 블러");
    case Type::Contrast: return QStringLiteral("대비");
    case Type::Saturation: return QStringLiteral("채도");
    case Type::HueShift: return QStringLiteral("색상 이동");
    case Type::Sepia: return QStringLiteral("세피아");
    case Type::Posterize: return QStringLiteral("포스터화");
    case Type::Threshold: return QStringLiteral("이진화");
    case Type::Wave: return QStringLiteral("물결");
    case Type::Swirl: return QStringLiteral("소용돌이");
    case Type::Bulge: return QStringLiteral("볼록/오목");
    case Type::KeyWhite: return QStringLiteral("흰색 제거");
    case Type::KeyBlack: return QStringLiteral("검정 제거");
    case Type::KeyGreen: return QStringLiteral("초록 제거");
    case Type::MotionBlurH: return QStringLiteral("모션 블러 (가로)");
    case Type::MotionBlurV: return QStringLiteral("모션 블러 (세로)");
    case Type::Echo: return QStringLiteral("잔상");
    case Type::Noise: return QStringLiteral("노이즈");
    case Type::Vignette: return QStringLiteral("비네트");
    case Type::DropShadow: return QStringLiteral("그림자");
    case Type::Pixelate: return QStringLiteral("모자이크");
    case Type::Emboss: return QStringLiteral("엠보스");
    case Type::Edge: return QStringLiteral("윤곽 검출");
    case Type::Outline: return QStringLiteral("외곽선");
    case Type::Sharpen: return QStringLiteral("선명하게");
    case Type::Curves: return QStringLiteral("커브 (S 곡선)");
    case Type::Levels: return QStringLiteral("레벨 (대비)");
    }
    return QString();
}

}
