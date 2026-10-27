#pragma once
#include <QColor>
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QPainterPath>
#include <QPointF>
#include <QRect>
#include <QTransform>
#include <QVector>
#include <QWidget>
#include <functional>
#include <algorithm>

#include "Brush.h"
#include "Document.h"
#include "Warp.h"

class Document;

class CanvasWidget : public QWidget {
    Q_OBJECT
public:
    enum class Tool { Pen, Pencil, Eraser, Line, Rect, Circle, Spline, Bucket, Select, Pan, Warp,
                      Pick, Lasso, Polygon, Wand, Gradient, Text };

    explicit CanvasWidget(Document* doc, QWidget* parent = nullptr);

    void setTool(Tool t);
    Tool tool() const { return tool_; }
    void setColorA(const QColor& c) { colorA_ = c; }
    QColor colorA() const { return colorA_; }
    void setBrush(const BrushParams& b) { brush_ = b; brush_.tiltAngle = tiltAngle_; brush_.tiltAmount = tiltAmount_; }
    void setPenTilt(double angleDeg, double amount);   // 테스트·보정용으로 기울기를 직접 지정
    BrushParams brush() const { return brush_; }
    void setSmoothing(bool on) { smoothing_ = on; }
    void setSmoothingStrength(int s) { smoothStrength_ = std::clamp(s, 1, 99); }   // 1 = 약함, 99 = 아주 강함
    int smoothingStrength() const { return smoothStrength_; }
    bool smoothing() const { return smoothing_; }
    void setPaper(bool on) { paper_ = on; update(); }
    void setStencil(bool on) { stencil_ = on; }
    void setLightTable(bool on);
    bool lightTable() const { return lightTable_; }
    void setCameraView(bool on);
    qreal zoom() const { return zoom_; }
    void setBackdrop(const QColor& c) { backdrop_ = c; update(); }

    // 가이드라인 오버레이
    struct Guides {
        bool grid = false;
        int gridSize = 64;
        bool center = false;
        bool safe = false;
        int safePct = 90;
        bool thirds = false;
        // 직선자: 켜면 획이 각도에 맞춰 곧게 펴진다 (0 끔, 1 수평·수직, 2 각도 지정, 3 원근 소실점)
        int ruler = 0;
        double rulerAngle = 0;          // 도
        bool perspective = false;       // 소실점 가이드 그리기
        int vanishCount = 1;            // 소실점 1~3개
        double vp[3][2] = {{-1, -1}, {-1, -1}, {-1, -1}};   // 소실점 좌표 (음수 = 기본 위치)
        bool snapToVanish = false;      // 획을 가장 가까운 소실점 방향으로 펴기
    };
    QPointF vanishPoint(int i) const;
    void setGuides(const Guides& g) { guides_ = g; update(); }
    Guides guides() const { return guides_; }

    // 라이트 테이블 설정
    struct LightTableParams {
        enum Mode { Instance, Frame, Bookmark, ImageMark };
        static constexpr int kSlots = 8;
        int mode = Instance;
        int colorMode = 0;               // 0 = 색 입히기, 1 = 원래 색, 2 = 거리에 따라 옅어지는 그라데이션
        QColor prevColor{0x40, 0xc0, 0x60};   // 이전 이미지 (초록)
        QColor nextColor{0xff, 0x90, 0x20};   // 다음 이미지 (주황)
        int prevOp[kSlots] = {60, 45, 32, 22, 0, 0, 0, 0};   // 가까운 것부터, % (0 = 끔)
        int nextOp[kSlots] = {60, 45, 32, 22, 0, 0, 0, 0};
        bool blendToFront = false;       // 라이트 테이블 이미지를 현재 이미지 위에 표시
        bool allLayers = false;          // 보이는 모든 레이어에 적용
    };
    void setLightTableParams(const LightTableParams& p) { ltParams_ = p; ltCache_.clear(); ltOverlay_ = QImage(); update(); }
    LightTableParams lightTableParams() const { return ltParams_; }

    // 내비게이터용
    QRectF visibleImageRect() const;
    void centerOnImagePoint(const QPointF& ip);

    void zoomBy(qreal factor);
    void zoomFit();
    void zoomActual();
    void clearCurrent();
    void selectAll();
    void clearSelection();
    QRect selection() const { return selValid_ ? selRect_ : QRect(); }
    // 사각형이 아닌 선택(올가미·다각형·마술봉)일 때의 알파 마스크. 사각형 선택이면 null.
    QImage selectionMask() const { return selValid_ ? selMask_ : QImage(); }
    void setSelectionMask(const QImage& mask);          // 마스크로 선택 지정 (빈 이미지 = 해제)
    void setSelectionRect(const QRect& r);              // 사각형 선택 지정
    void invertSelection();
    void growSelection(int px);      // 양수 = 키우기, 음수 = 줄이기
    void featherSelection(int px);   // 가장자리 부드럽게
    bool selectionFeathered() const { return selValid_ && selFeathered_; }
    int wandTolerance() const { return wandTol_; }
    void setWandTolerance(int t) { wandTol_ = std::clamp(t, 0, 255); }
    bool wandSampleAllLayers() const { return wandAll_; }
    void setWandSampleAllLayers(bool on) { wandAll_ = on; }

    // 대칭 그리기
    struct Symmetry {
        bool on = false;
        bool vertical = true;    // 세로축 기준 좌우 대칭
        bool horizontal = false; // 가로축 기준 위아래 대칭
        int radial = 1;          // 방사 대칭 횟수 (1 = 끔)
        double axisX = -1, axisY = -1; // 음수 = 캔버스 가운데
    };
    void setSymmetry(const Symmetry& s) { sym_ = s; update(); }
    Symmetry symmetry() const { return sym_; }

    // 그라디언트
    struct GradientParams {
        int type = 0;      // 0 선형, 1 원형, 2 원뿔
        bool toTransparent = false; // B 색 대신 투명으로
        bool dither = false;
    };
    void setGradientParams(const GradientParams& g) { grad_ = g; }
    GradientParams gradientParams() const { return grad_; }
    void setColorB(const QColor& c) { colorB_ = c; }
    QColor colorB() const { return colorB_; }

    // 텍스트 도구
    struct TextParams {
        QString text = QStringLiteral("ONY");
        QString family;
        int size = 48;
        bool bold = false, italic = false;
        bool outline = false;
        int outlineWidth = 2;
    };
    void setTextParams(const TextParams& t) { text_ = t; update(); }
    TextParams textParams() const { return text_; }
    bool hasPendingWarp() const { return !pins_.isEmpty(); }
    void commitWarp();
    void cancelWarp();

signals:
    void cursorMoved(int x, int y);
    void colorPicked(const QColor& c);
    void selectionChanged();
    void zoomChanged(qreal zoom);
    void message(const QString& text);
    void strokeFinished();
    void viewChanged();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void tabletEvent(QTabletEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void keyReleaseEvent(QKeyEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    QRect dirty_;
    void markDirty(const QRectF& r);
    StrokeRec rec_;
    bool recActive_ = false;
    QElapsedTimer recClock_;
    QColor backdrop_{0x2d, 0x31, 0x36};
    QImage stencilMask_;
    QHash<quint64, QImage> ltCache_;   // 라이트 테이블 색 입힌 그림 캐시
    QImage ltOverlay_;                 // 겹쳐 둔 라이트 테이블 한 장
    int ltOverlayFrame_ = -1;
    class QVariantAnimation* zoomAnim_ = nullptr;
    qreal zoomTarget_ = 1.0;
    QPointF zoomAnchorWidget_, zoomAnchorImage_;
    QPointF imageTopLeft() const;
    QPointF toImage(const QPointF& widgetPos) const;

    bool beginEdit(int layer = -1);
    void commitEdit(const QString& text);

    // 브러시 엔진
    void beginStroke(const QPointF& ip);
    void stampAlong(const QPointF& a, const QPointF& b, double pa, double pb);
    void stamp(const QPointF& pos, double pressure);
    void flushStroke();
    const QImage& dab(int diameter, int hardness, const QColor& c, bool aa);
    void drawViaBuffer(const std::function<void(QPainter&)>& fn, const QString& text);

    void bucketFill(const QPoint& seed);
    void ctgFill(const QPoint& seed);
    void clearSelectionArea();
    QPainterPath splinePath() const;
    void commitSpline();
    void updateWarpPreview();
    QImage paperTexture() const;

    Document* doc_;
    Tool tool_ = Tool::Pen;
    QColor colorA_ = Qt::black;
    BrushParams brush_;
    bool smoothing_ = false;
    int smoothStrength_ = 60;
    bool paper_ = false;
    bool stencil_ = false;
    bool lightTable_ = false;
    bool cameraView_ = false;
    double pressure_ = 1.0;

    BrushDabCache dabCache_;
    QRandomGenerator rng_{777};
    Guides guides_;
    LightTableParams ltParams_;
    QRectF lastView_;
    qreal zoom_ = 1.0;
    QPointF pan_{0, 0};
    bool fitPending_ = true;
    bool spaceDown_ = false;
    bool panning_ = false;
    QPoint panLast_;

    QImage cache_;
    bool cacheDirty_ = true;
    QRect partialDirty_;   // 획을 그리는 동안 바뀐 영역

    // 편집 상태
    int editLayer_ = -1;
    int editKey_ = -1;
    QImage editBefore_;
    bool stroking_ = false;
    QPointF last_;
    double lastPressure_ = 1.0;
    double carry_ = 0.0;
    QImage strokeBase_;
    QImage strokeBuf_;
    mutable QImage paperTex_;
    int dabD_ = -1, dabH_ = -1;
    QRgb dabC_ = 0;
    bool dabAA_ = true;
    QImage dabImg_;

    bool shaping_ = false;
    QPointF shapeStart_, shapeEnd_;

    // 스플라인
    QVector<QPointF> splinePts_;
    QPointF hover_;

    // 선택/이동
    QRect selRect_;
    bool selValid_ = false;
    QImage selMask_;             // 사각형이 아닌 선택의 알파 마스크 (캔버스 크기, null = 사각형)
    bool selFeathered_ = false;   // 가장자리를 부드럽게 한 선택인지
    QVector<QPointF> lassoPts_;  // 올가미·다각형이 그려지는 중의 점들
    bool lassoing_ = false;
    int wandTol_ = 32;
    bool wandAll_ = false;
    Symmetry sym_;
    GradientParams grad_;
    QColor colorB_{Qt::white};
    TextParams text_;
    double tiltAngle_ = 0, tiltAmount_ = 0;
    QPointF strokeOrigin_;
    QVector<QPointF> vecPts_;
    QVector<float> vecPr_;
    QVector<VecStroke> vecWork_;
    int vecEditStroke_ = -1, vecEditPoint_ = -1;
    QPointF gradStart_, gradEnd_;
    bool grading_ = false;
    QPointF textPos_;
    void applySelectionClip(QPainter& p) const;
    void setSelectionFromPath(const QPainterPath& path, Qt::KeyboardModifiers mods);
    void wandSelect(const QPoint& seed, Qt::KeyboardModifiers mods);
    void pickColorAt(const QPointF& ip);
    void commitGradient(const QPointF& a, const QPointF& b);
    void commitText(const QPointF& at);
    QVector<QTransform> symmetryTransforms() const;
    bool selecting_ = false;
    QPoint selStart_;
    bool moving_ = false;
    QImage floating_;
    QPointF moveStart_;
    QPoint floatPos_;

    // 핀 워프
    QVector<Warp::Pin> pins_;
    int dragPin_ = -1;
    int warpLayer_ = -1;
    int warpKey_ = -1;
    QImage warpBefore_;
    QElapsedTimer warpTimer_;
};
