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
    // 색 보정 (파라미터가 많아 FX 스택에는 세기 하나로만 들어간다)
    Curves, Levels,
    // 데모6: TVPaint 의 FX 목록을 참고해 넓힌 효과들
    RadialBlur, SpinBlur,                                  // 블러
    Solarize, BlackWhite, ColorRange, Lumix,               // 색
    Lens, Ripple,                                          // 왜곡
    ColorKeyer,                                            // 키잉
    Glow, Bloom, Halo,                                     // 렌더링
    ToonShading, Bevel, Erode, Dilate, Halftone, Dither, AntiAlias,   // 스타일화
    RepeatLine, Dashes,                                    // 페인트
    Last = Dashes
};

struct ParamInfo {
    bool has = false;
    int lo = 0, hi = 0, def = 0;
    QString label;
};
ParamInfo paramInfo(Type t);
// 색을 쓰는 효과인지 (param 에 색이 들어간다)
bool usesColorParam(Type t);
// param 에 색과 세기를 함께 담는다 (상위 7비트 = 세기, 하위 24비트 = RGB)
int packColorParam(const QColor& c, int strength);
QColor colorFromParam(int param);
int strengthFromParam(int param);
QString categoryOf(Type t);
QStringList categories();            // 메뉴 순서: 블러, 색, 왜곡, 키잉, 모션, 렌더링, 스타일화
QVector<Type> typesIn(const QString& category);
QImage crossfade(const QImage& a, const QImage& b, double t);     // 전환: 디졸브
// 모양 보간(모프): 두 그림의 테두리까지의 거리를 섞어 중간 모양을 만든다. 선화에 잘 맞는다.
QImage morph(const QImage& a, const QImage& b, double t);
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
QImage gaussBlur(const QImage& in, int radius);
QImage curves(const QImage& in, const QVector<int>& lutR, const QVector<int>& lutG, const QVector<int>& lutB);
QImage levels(const QImage& in, int inLow, int inHigh, double gamma, int outLow, int outHigh);
QImage flipH(const QImage& in);
QImage flipV(const QImage& in);

QImage apply(const QImage& in, const Item& item);
QString typeName(Type t);

}
