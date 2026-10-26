#pragma once
#include <QImage>
#include <QString>

namespace Effects {

enum class Type { Invert, Grayscale, Brightness, Blur };

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
