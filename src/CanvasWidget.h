#pragma once
#include <QColor>
#include <QElapsedTimer>
#include <QImage>
#include <QPainterPath>
#include <QPointF>
#include <QRect>
#include <QVector>
#include <QWidget>
#include <functional>

#include "Brush.h"
#include "Document.h"
#include "Warp.h"

class Document;

class CanvasWidget : public QWidget {
    Q_OBJECT
public:
    enum class Tool { Pen, Pencil, Eraser, Line, Rect, Circle, Spline, Bucket, Select, Pan, Warp };

    explicit CanvasWidget(Document* doc, QWidget* parent = nullptr);

    void setTool(Tool t);
    Tool tool() const { return tool_; }
    void setColorA(const QColor& c) { colorA_ = c; }
    QColor colorA() const { return colorA_; }
    void setBrush(const BrushParams& b) { brush_ = b; }
    BrushParams brush() const { return brush_; }
    void setSmoothing(bool on) { smoothing_ = on; }
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
    };
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
    void setLightTableParams(const LightTableParams& p) { ltParams_ = p; update(); }
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
    bool hasPendingWarp() const { return !pins_.isEmpty(); }
    void commitWarp();
    void cancelWarp();

signals:
    void cursorMoved(int x, int y);
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
