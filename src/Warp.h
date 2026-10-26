#pragma once
#include <QImage>
#include <QPointF>
#include <QRect>
#include <QTransform>
#include <QVector>

namespace Warp {

// 핀 기반 퍼핏 워프: dst 로 옮긴 핀 주변 픽셀이 따라오고, fixed 핀과 이미지 가장자리는 고정된다.
struct Pin {
    QPointF src;
    QPointF dst;
    bool fixed = false;
};

QImage pinWarp(const QImage& src, const QVector<Pin>& pins);

// region(비어 있으면 전체) 내용을 t 로 변환해 다시 그린다. t 는 이미지 좌표계 기준.
QImage transformRegion(const QImage& src, QRect region, const QTransform& t);

// 이동/배율/회전/좌우반전을 region 중심 기준으로 합성한 변환 행렬
QTransform makeTransform(const QRect& region, const QSize& imageSize, double dx, double dy, double scalePct,
                         double rotationDeg, bool flipH);

// 4점 원근: region 의 네 모서리를 offsets(좌상, 우상, 우하, 좌하)만큼 옮긴 사각형으로 변환
bool makePerspective(const QRect& region, const QSize& imageSize, const QPointF offsets[4], QTransform* out);

// 메시 워프: 원본을 cols x rows 격자로 나눴을 때, 각 격자점(행 우선, (cols+1)*(rows+1)개)이 옮겨진 위치(dst)
QImage meshWarp(const QImage& src, int cols, int rows, const QVector<QPointF>& dst);

}
