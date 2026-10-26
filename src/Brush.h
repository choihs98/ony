#pragma once
#include <QColor>
#include <QImage>
#include <QPointF>
#include <QRandomGenerator>
#include <QSize>

struct BrushParams {
    int size = 6;        // 지름(px)
    int opacity = 100;   // 0~100
    int hardness = 85;   // 0~100 (0 = 에어브러시처럼 부드러움)
    bool pressure = false;
    int preset = 0;      // brushPreset() 인덱스
};

enum class BrushShape { Round, Square, Star, Spray };

struct BrushPreset {
    const char* name;
    BrushShape shape;
    int size;        // 기본 크기
    int opacity;     // 기본 불투명도
    int hardness;    // 기본 경도
    int spacing;     // 스탬프 간격 (크기의 %)
    int scatter;     // 흩뿌림 (크기의 %)
    int grain;       // 질감 세기 0~100
    bool eraser;     // 지우개 계열
    bool pressure;   // 기본 필압 사용
    bool aliased;    // 안티앨리어싱 끔 (연필)
    int aspect;      // 납작함: 짧은 축 비율 % (100 = 원/정사각)
    int angle;       // 납작한 붓의 각도(도)
};

int brushPresetCount();
const BrushPreset& brushPreset(int index);

// 스탬프 캐시
struct BrushDabCache {
    QImage img;
    int preset = -1, d = -1, hardness = -1;
    QRgb color = 0;
};

// a→b 구간에 스탬프를 찍는다. carry 는 구간 사이 남은 거리.
void brushStampAlong(QImage& buf, const BrushParams& params, const QColor& color, const QPointF& a, const QPointF& b,
                     double pressureA, double pressureB, double& carry, QRandomGenerator& rng, BrushDabCache& cache);
// 점 하나 찍기 (스트로크 시작)
void brushStampAt(QImage& buf, const BrushParams& params, const QColor& color, const QPointF& pos, double pressure,
                  QRandomGenerator& rng, BrushDabCache& cache);
// 스트로크가 끝난 버퍼에 질감을 입힌다 (grain 0 이면 그대로)
void brushApplyGrain(QImage& buf, int grain);
// 미리보기: 물결 모양 스트로크를 그려서 돌려준다 (투명 배경)
QImage brushPreview(const BrushParams& params, const QColor& color, QSize size);
