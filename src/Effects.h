#pragma once
#include <QImage>
#include <QString>
#include <QStringList>
#include <QVector>

namespace Effects {

// 값은 저장 파일에 들어가므로 기존 항목의 순서를 바꾸지 않는다 (새 항목은 뒤에 추가).
enum class Type {
    Invert, Grayscale, Brightness, Blur,
    // 블러
    GaussBlur,
    // 색
    Contrast, Saturation, HueShift, Sepia, Posterize, Threshold,
    // 왜곡
    Wave, Swirl, Bulge,
    // 키잉
    KeyWhite, KeyBlack, KeyGreen,
    // 모션
    MotionBlurH, MotionBlurV, Echo,
    // 렌더링
    Noise, Vignette, DropShadow,
    // 스타일화
    Pixelate, Emboss, Edge, Outline, Sharpen,
    Last = Sharpen
};

struct ParamInfo {
    bool has = false;
    int lo = 0, hi = 0, def = 0;
    QString label;
};
ParamInfo paramInfo(Type t);
QString categoryOf(Type t);
QStringList categories();            // 메뉴 순서: 블러, 색, 왜곡, 키잉, 모션, 렌더링, 스타일화
QVector<Type> typesIn(const QString& category);
QImage crossfade(const QImage& a, const QImage& b, double t);     // 전환: 디졸브
QImage wipe(const QImage& a, const QImage& b, double t, int dir); // 전환: 와이프 (0 좌→우, 1 우→좌, 2 위→아래, 3 아래→위)
QImage slide(const QImage& a, const QImage& b, double t);         // 전환: 밀기 (좌→우)

struct Item {
    Type type = Type::Invert;
    int param = 0;
};

QImage invert(const QImage& in);
QImage grayscale(const QImage& in);
QImage brightness(const QImage& in, int delta);
QImage boxBlur(const QImage& in, int radius);
QImage flipH(const QImage& in);
QImage flipV(const QImage& in);

QImage apply(const QImage& in, const Item& item);
QString typeName(Type t);

}
