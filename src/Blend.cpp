#include "Blend.h"

#include <QtGlobal>
#include <algorithm>
#include <cmath>

namespace Blend {

static const char* kKo[] = {"색상", "뒤", "지우기", "음영", "빛", "채색", "틴트", "채도", "명도", "더하기", "빼기", "곱하기", "스크린",
                            "대체", "치환", "차이", "나누기", "오버레이 1", "오버레이 2", "닷지", "번", "하드 라이트", "소프트 라이트",
                            "그레인 추출", "그레인 병합", "빼기 2", "어두운 쪽만", "밝은 쪽만"};
static const char* kEn[] = {"Color", "Behind", "Erase", "Shade", "Light", "Colorize", "Tint", "Saturate", "Value", "Add", "Sub",
                            "Multiply", "Screen", "Replace", "Substitute", "Difference", "Divide", "Overlay 1", "Overlay 2",
                            "Dodge (Light2)", "Burn (Shade2)", "Hard Light", "Soft Light", "Grain Extract", "Grain Merge",
                            "Subtract", "Darken Only", "Lighten Only"};
static const char* kDesc[] = {
    "기본 모드. 레이어의 색이 아래 그림 위에 그대로 올라갑니다.",
    "레이어의 픽셀을 아래 그림의 투명한 부분에만 적용합니다.",
    "레이어의 불투명한 픽셀로 아래 그림을 지웁니다.",
    "레이어의 불투명한 픽셀이 아래 그림에 그늘을 드리웁니다.",
    "레이어의 불투명한 픽셀이 아래 그림을 밝게 합니다.",
    "아래 그림에 레이어의 색을 입히되 아래 그림의 밝기는 유지합니다.",
    "아래 그림에 레이어의 색을 입히되 아래 그림의 밝기와 채도는 유지합니다.",
    "레이어의 값으로 아래 그림의 채도를 높입니다.",
    "레이어의 밝기와 아래 그림의 색조·채도를 사용합니다.",
    "레이어의 색을 아래 그림에 더합니다.",
    "아래 그림에서 레이어의 색을 뺍니다.",
    "레이어의 색을 아래 그림에 곱합니다. 보통 더 어두워집니다.",
    "곱하기의 반대입니다. 보통 더 밝아집니다.",
    "아래 그림의 픽셀을 레이어의 픽셀로 모두 바꿉니다.",
    "레이어의 불투명도 비율로 아래 그림과 레이어를 섞습니다.",
    "아래 그림에서 레이어를 뺀 값의 절댓값입니다.",
    "아래 그림을 레이어 값으로 나눕니다. 보통 더 밝아집니다.",
    "오버레이: 곱하기보다는 덜 어두워집니다.",
    "오버레이 2: 곱하기와 오버레이의 중간입니다.",
    "아래 그림의 밝기에 따라 레이어가 아래 그림을 밝게 합니다.",
    "레이어 값에 따라 아래 그림을 어둡게 합니다.",
    "오버레이와 비슷하지만 레이어와 아래 그림의 역할을 바꿉니다. 밝고 선명합니다.",
    "오버레이와 비슷하지만 가장자리가 부드럽고 색이 덜 밝습니다.",
    "레이어를 이용해 아래 그림에서 질감(그레인)을 추출합니다.",
    "레이어의 질감(그레인)을 아래 그림에 합칩니다.",
    "레이어를 빼는 또 다른 방법입니다.",
    "레이어와 아래 그림 중 더 어두운 값을 고릅니다.",
    "레이어와 아래 그림 중 더 밝은 값을 고릅니다.",
};

QString name(int m) { return QString::fromUtf8(kKo[std::clamp(m, 0, Count - 1)]); }
QString englishName(int m) { return QString::fromLatin1(kEn[std::clamp(m, 0, Count - 1)]); }
QString description(int m) { return QString::fromUtf8(kDesc[std::clamp(m, 0, Count - 1)]); }

namespace {
struct V3 { float r, g, b; };

inline float lum(V3 c) { return 0.3f * c.r + 0.59f * c.g + 0.11f * c.b; }
inline V3 clipColor(V3 c) {
    const float l = lum(c);
    const float n = std::min({c.r, c.g, c.b}), x = std::max({c.r, c.g, c.b});
    if (n < 0) {
        const float k = l / (l - n + 1e-6f);
        c = {l + (c.r - l) * k, l + (c.g - l) * k, l + (c.b - l) * k};
    }
    if (x > 1) {
        const float k = (1 - l) / (x - l + 1e-6f);
        c = {l + (c.r - l) * k, l + (c.g - l) * k, l + (c.b - l) * k};
    }
    return c;
}
inline V3 setLum(V3 c, float l) {
    const float d = l - lum(c);
    return clipColor({c.r + d, c.g + d, c.b + d});
}
inline float sat(V3 c) { return std::max({c.r, c.g, c.b}) - std::min({c.r, c.g, c.b}); }
inline V3 setSat(V3 c, float s) {
    float* v[3] = {&c.r, &c.g, &c.b};
    std::sort(v, v + 3, [](float* a, float* b) { return *a < *b; });
    if (*v[2] > *v[0]) {
        *v[1] = (*v[1] - *v[0]) * s / (*v[2] - *v[0]);
        *v[2] = s;
    } else {
        *v[1] = *v[2] = 0;
    }
    *v[0] = 0;
    return c;
}

inline float ch(int mode, float b, float s) { // 채널별 혼합 (b: 아래, s: 레이어), 0~1
    switch (mode) {
    case Add: return std::min(1.f, b + s);
    case Sub: return std::max(0.f, b - s);
    case Multiply: return b * s;
    case Screen: return 1.f - (1.f - b) * (1.f - s);
    case Difference: return std::fabs(b - s);
    case Divide: return s <= 1e-4f ? 1.f : std::min(1.f, b / s);
    case Overlay1: return b < 0.5f ? 2 * b * s : 1 - 2 * (1 - b) * (1 - s);
    case Overlay2: { const float o = b < 0.5f ? 2 * b * s : 1 - 2 * (1 - b) * (1 - s); return 0.5f * (o + b * s); }
    case Dodge: return s >= 1.f ? 1.f : std::min(1.f, b / (1.f - s));
    case Burn: return s <= 1e-4f ? 0.f : 1.f - std::min(1.f, (1.f - b) / s);
    case HardLight: return s < 0.5f ? 2 * b * s : 1 - 2 * (1 - b) * (1 - s);
    case SoftLight: {
        if (s <= 0.5f) return b - (1 - 2 * s) * b * (1 - b);
        const float d = b <= 0.25f ? ((16 * b - 12) * b + 4) * b : std::sqrt(b);
        return b + (2 * s - 1) * (d - b);
    }
    case GrainExtract: return std::clamp(b - s + 0.5f, 0.f, 1.f);
    case GrainMerge: return std::clamp(b + s - 0.5f, 0.f, 1.f);
    case Subtract: return std::max(0.f, s - b);
    case DarkenOnly: return std::min(b, s);
    case LightenOnly: return std::max(b, s);
    default: return s;
    }
}
} // namespace

void apply(QImage& dst, const QImage& src, int mode, qreal opacity) {
    if (dst.size() != src.size()) return;
    const float op = float(std::clamp(opacity, 0.0, 1.0));
    const int w = dst.width(), h = dst.height();
    for (int y = 0; y < h; ++y) {
        QRgb* d = reinterpret_cast<QRgb*>(dst.scanLine(y));
        const QRgb* s = reinterpret_cast<const QRgb*>(src.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            const float sa = qAlpha(s[x]) / 255.f * op;
            const float da = qAlpha(d[x]) / 255.f;
            // 직선(비곱셈) 색
            V3 sc{0, 0, 0}, dc{0, 0, 0};
            if (qAlpha(s[x]) > 0) sc = {qRed(s[x]) / float(qAlpha(s[x])), qGreen(s[x]) / float(qAlpha(s[x])), qBlue(s[x]) / float(qAlpha(s[x]))};
            if (qAlpha(d[x]) > 0) dc = {qRed(d[x]) / float(qAlpha(d[x])), qGreen(d[x]) / float(qAlpha(d[x])), qBlue(d[x]) / float(qAlpha(d[x]))};
            float oa;
            V3 oc;
            switch (mode) {
            case Behind: { // 레이어를 아래에 깐다
                oa = da + sa * (1 - da);
                oc = oa > 0 ? V3{(dc.r * da + sc.r * sa * (1 - da)) / oa, (dc.g * da + sc.g * sa * (1 - da)) / oa, (dc.b * da + sc.b * sa * (1 - da)) / oa} : V3{0, 0, 0};
                break;
            }
            case Erase: oa = da * (1 - sa); oc = dc; break;
            case Replace:
                if (qAlpha(s[x]) == 0) continue;
                oa = sa;
                oc = sc;
                break;
            case Substitute: { // 레이어와 아래 그림을 불투명도 비율로 섞는다
                if (qAlpha(s[x]) == 0) continue;
                const float k = op;
                const float sa0 = qAlpha(s[x]) / 255.f;
                oa = da * (1 - k) + sa0 * k;
                oc = oa > 0 ? V3{(dc.r * da * (1 - k) + sc.r * sa0 * k) / oa, (dc.g * da * (1 - k) + sc.g * sa0 * k) / oa, (dc.b * da * (1 - k) + sc.b * sa0 * k) / oa} : V3{0, 0, 0};
                break;
            }
            default: {
                if (sa <= 0) { continue; }
                V3 B;
                switch (mode) {
                case Shade: { const float k = 1 - (1 - lum(sc)) * 1.0f; B = {dc.r * k, dc.g * k, dc.b * k}; break; }
                case Light: { const float k = lum(sc); B = {dc.r + (1 - dc.r) * k, dc.g + (1 - dc.g) * k, dc.b + (1 - dc.b) * k}; break; }
                case Colorize: B = setLum(sc, lum(dc)); break;
                case Tint: B = setLum(setSat(sc, sat(dc)), lum(dc)); break;
                case Saturate: B = setLum(setSat(dc, std::min(1.f, sat(dc) + sat(sc))), lum(dc)); break;
                case Value: B = setLum(dc, lum(sc)); break;
                case Color: B = sc; break;
                default: B = {ch(mode, dc.r, sc.r), ch(mode, dc.g, sc.g), ch(mode, dc.b, sc.b)}; break;
                }
                // 합성 규격: 아래가 불투명한 만큼만 혼합 결과를 쓴다
                const V3 Cs{(1 - da) * sc.r + da * B.r, (1 - da) * sc.g + da * B.g, (1 - da) * sc.b + da * B.b};
                oa = sa + da * (1 - sa);
                oc = oa > 0 ? V3{(sa * Cs.r + (1 - sa) * da * dc.r) / oa, (sa * Cs.g + (1 - sa) * da * dc.g) / oa, (sa * Cs.b + (1 - sa) * da * dc.b) / oa} : V3{0, 0, 0};
                break;
            }
            }
            const int A = std::clamp(int(oa * 255 + 0.5f), 0, 255);
            d[x] = qRgba(std::clamp(int(oc.r * oa * 255 + 0.5f), 0, A), std::clamp(int(oc.g * oa * 255 + 0.5f), 0, A),
                         std::clamp(int(oc.b * oa * 255 + 0.5f), 0, A), A);
        }
    }
}

} // namespace Blend
