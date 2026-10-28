#pragma once
#include <QColor>
#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QRandomGenerator>
#include <QSize>
#include <QVector>

struct BrushParams {
    int size = 6;        // 지름(px)
    int opacity = 100;   // 0~100
    int hardness = 85;   // 0~100 (0 = 에어브러시처럼 부드러움)
    bool pressure = false;
    // ---- 필압 조절 (pressure 가 켜져 있을 때만 쓴다) ----
    bool prSize = true;       // 필압이 굵기에 작용
    bool prOpacity = true;    // 필압이 진하기에 작용
    int prMinSize = 6;        // 가장 약하게 눌렀을 때의 굵기 (전체의 %)
    int prMinOpacity = 0;     // 가장 약하게 눌렀을 때의 진하기 (전체의 %)
    int prCurve = -35;        // 감도 -100 ~ +100. 음수 = 살짝만 눌러도 진하게, 양수 = 꾹 눌러야 진하게
    int preset = 0;      // brushPreset() 인덱스
    bool tilt = false;        // 펜 기울기를 쓰면 붓이 납작해지고 기운 쪽으로 돈다
    int customBrush = -1;     // 사용자 브러시 번호 (-1 = 없음)
    // 애니메이션 브러시(그림 여러 장짜리)를 어떤 순서로 찍을지: 0 차례대로, 1 무작위
    int animMode = 0;
    // 아래 둘은 설정이 아니라 그리는 동안 채워지는 값 (태블릿 기울기)
    double tiltAngle = 0;     // 도
    double tiltAmount = 0;    // 0~1
};

// 사용자 브러시: 그림 한 장을 도장처럼 쓴다 (알파만 쓰고 색은 현재 색으로 칠한다)
int customBrushCount();
QString customBrushName(int i);
QImage customBrushImage(int i);                 // 첫 장 (그림 한 장짜리면 그 장)
// 애니메이션 브러시: 그림 여러 장을 한 브러시로 묶어, 찍을 때마다 다음 장이 나온다
int customBrushFrameCount(int i);
QImage customBrushFrame(int i, int frame);
int addCustomBrush(const QString& name, const QImage& img);   // 번호를 돌려준다
int addAnimBrush(const QString& name, const QVector<QImage>& frames);
void removeCustomBrush(int i);
void clearCustomBrushes();
// 사용자 질감: 비어 있으면 기본 질감을 쓴다
void setCustomGrainTexture(const QImage& tile);
QImage customGrainTexture();

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
    bool follow = false; // 획 방향으로 스탬프를 회전 (파선)
};

int brushPresetCount();
const BrushPreset& brushPreset(int index);

// 태블릿이 준 필압(0~1)을 설정에 맞춰 손본다.
// 굵기 배율과 진하기 배율을 각각 돌려준다 (필압을 끄면 둘 다 1).
void pressureFactors(const BrushParams& p, double pressure, double* sizeMul, double* opacityMul);
// 감도 곡선만 적용한 값 (미리 보기 그림용)
double pressureCurve(int curve, double pressure);

// 스탬프 캐시
struct BrushDabCache {
    QImage img;
    int preset = -1, d = -1, hardness = -1;
    QRgb color = 0;
    int custom = -1;
    int customFrame = 0;
    int tiltKey = 0;
    int animStep = 0;   // 애니메이션 브러시에서 지금까지 몇 번 찍었는지
    double dir = 0;  // 마지막 획 방향(도)
};

// a→b 구간에 스탬프를 찍는다. carry 는 구간 사이 남은 거리.
void brushStampAlong(QImage& buf, const BrushParams& params, const QColor& color, const QPointF& a, const QPointF& b,
                     double pressureA, double pressureB, double& carry, QRandomGenerator& rng, BrushDabCache& cache);
// 점 하나 찍기 (스트로크 시작)
void brushStampAt(QImage& buf, const BrushParams& params, const QColor& color, const QPointF& pos, double pressure,
                  QRandomGenerator& rng, BrushDabCache& cache);
// 스트로크가 끝난 버퍼에 질감을 입힌다 (grain 0 이면 그대로)
void brushApplyGrain(QImage& buf, int grain, QPoint origin = QPoint());   // origin: buf 왼쪽 위의 실제 좌표 (질감 무늬를 맞춘다)
// 미리보기: 물결 모양 스트로크를 그려서 돌려준다 (투명 배경)
QImage brushPreview(const BrushParams& params, const QColor& color, QSize size);
