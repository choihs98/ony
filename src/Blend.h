#pragma once
#include <QImage>
#include <QString>

// 레이어 블렌딩 모드 (28종)
namespace Blend {
enum Mode {
    Color = 0, Behind, Erase, Shade, Light, Colorize, Tint, Saturate, Value, Add, Sub, Multiply, Screen, Replace,
    Substitute, Difference, Divide, Overlay1, Overlay2, Dodge, Burn, HardLight, SoftLight, GrainExtract, GrainMerge,
    Subtract, DarkenOnly, LightenOnly, Count
};
QString name(int mode);        // 한글 이름
QString englishName(int mode); // 매뉴얼의 영문 이름
QString description(int mode); // 간단한 설명

// src(레이어 이미지)를 dst(아래 레이어들의 합성)에 mode 로 섞는다. 둘 다 ARGB32_Premultiplied, 같은 크기.
void apply(QImage& dst, const QImage& src, int mode, qreal opacity);
}
