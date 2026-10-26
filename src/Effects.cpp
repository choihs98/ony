#include "Effects.h"
#include <QVector>
#include <algorithm>

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

QImage flipH(const QImage& in) { return in.mirrored(true, false); }
QImage flipV(const QImage& in) { return in.mirrored(false, true); }

QImage apply(const QImage& in, const Item& item) {
    switch (item.type) {
    case Type::Invert: return invert(in);
    case Type::Grayscale: return grayscale(in);
    case Type::Brightness: return brightness(in, item.param);
    case Type::Blur: return boxBlur(in, std::max(1, item.param));
    }
    return in;
}

QString typeName(Type t) {
    switch (t) {
    case Type::Invert: return QStringLiteral("색 반전");
    case Type::Grayscale: return QStringLiteral("흑백");
    case Type::Brightness: return QStringLiteral("밝기");
    case Type::Blur: return QStringLiteral("블러");
    }
    return QString();
}

}
