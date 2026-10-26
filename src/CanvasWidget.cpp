#include "CanvasWidget.h"
#include "Document.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QRandomGenerator>
#include <QTabletEvent>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {
void dilateMask(QVector<uchar>& m, int w, int h, int passes) {
    QVector<uchar> t = m;
    for (int pass = 0; pass < passes; ++pass) {
        t = m;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const int i = y * w + x;
                if (m[i]) continue;
                if ((x > 0 && m[i - 1]) || (x + 1 < w && m[i + 1]) || (y > 0 && m[i - w]) || (y + 1 < h && m[i + w]))
                    t[i] = 1;
            }
        }
        m = t;
    }
}
}

CanvasWidget::CanvasWidget(Document* doc, QWidget* parent) : QWidget(parent), doc_(doc) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(320, 240);
    setCursor(Qt::CrossCursor);
    warpTimer_.start();
    auto invalidate = [this] {
        cacheDirty_ = true;
        update();
    };
    connect(doc_, &Document::changed, this, invalidate);
    connect(doc_, &Document::currentChanged, this, [this] {
        if (!pins_.isEmpty()) commitWarp();
        splinePts_.clear();
        selValid_ = false;
        cacheDirty_ = true;
        update();
    });
    connect(doc_, &Document::structureChanged, this, invalidate);
}

void CanvasWidget::setTool(Tool t) {
    if (tool_ == Tool::Warp && t != Tool::Warp) commitWarp();
    if (tool_ == Tool::Spline && t != Tool::Spline) {
        if (splinePts_.size() >= 2) commitSpline();
        splinePts_.clear();
    }
    tool_ = t;
    setCursor(t == Tool::Pan ? Qt::OpenHandCursor : Qt::CrossCursor);
    update();
}

void CanvasWidget::setLightTable(bool on) {
    lightTable_ = on;
    update();
}

void CanvasWidget::setCameraView(bool on) {
    cameraView_ = on;
    cacheDirty_ = true;
    update();
}

QPointF CanvasWidget::imageTopLeft() const {
    const QSize s = doc_->size();
    return QPointF((width() - s.width() * zoom_) / 2.0, (height() - s.height() * zoom_) / 2.0) + pan_;
}

QPointF CanvasWidget::toImage(const QPointF& p) const {
    return (p - imageTopLeft()) / zoom_;
}

void CanvasWidget::zoomBy(qreal factor) {
    const QPointF wp(width() / 2.0, height() / 2.0);
    const QPointF ip = toImage(wp);
    zoom_ = std::clamp(zoom_ * factor, 0.05, 32.0);
    const QSize s = doc_->size();
    const QPointF center((width() - s.width() * zoom_) / 2.0, (height() - s.height() * zoom_) / 2.0);
    pan_ = (wp - ip * zoom_) - center;
    emit zoomChanged(zoom_);
    update();
}

void CanvasWidget::zoomFit() {
    const QSize s = doc_->size();
    if (width() < 40 || height() < 40) return;
    zoom_ = std::clamp(std::min((width() - 24.0) / s.width(), (height() - 24.0) / s.height()), 0.05, 32.0);
    pan_ = QPointF(0, 0);
    emit zoomChanged(zoom_);
    update();
}

void CanvasWidget::zoomActual() {
    zoom_ = 1.0;
    pan_ = QPointF(0, 0);
    emit zoomChanged(zoom_);
    update();
}

void CanvasWidget::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    if (fitPending_ && width() > 100 && height() > 100) {
        fitPending_ = false;
        zoomFit();
    }
}

// ---------------------------------------------------------------- 그리기(화면)
void CanvasWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(0x3a, 0x3a, 0x3a));
    if (cacheDirty_ || cache_.size() != doc_->size()) {
        cache_ = doc_->composite(doc_->currentFrame(), true, cameraView_);
        cacheDirty_ = false;
    }
    const QSize s = doc_->size();
    const QRectF r(imageTopLeft(), QSizeF(s.width() * zoom_, s.height() * zoom_));
    p.setRenderHint(QPainter::SmoothPixmapTransform, zoom_ < 1.0);
    p.drawImage(r, cache_);

    // 라이트 테이블: 현재 레이어의 이전 키(빨강)와 다음 키(파랑)를 흐리게 표시
    if (lightTable_) {
        const int l = doc_->currentLayer();
        const int f = doc_->currentFrame();
        const int k = doc_->keyIndexAt(l, f);
        auto tinted = [](const QImage& src, const QColor& c) {
            QImage t = src;
            QPainter tp(&t);
            tp.setCompositionMode(QPainter::CompositionMode_SourceIn);
            tp.fillRect(t.rect(), c);
            return t;
        };
        const double base = ltParams_.opacity / 100.0;
        int cur = k;
        for (int i = 0; i < ltParams_.prev && cur > 0; ++i) {
            const int pk = doc_->keyIndexAt(l, cur - 1);
            if (pk < 0) break;
            p.setOpacity(base * (1.0 - 0.5 * i / std::max(1, ltParams_.prev)));
            p.drawImage(r, tinted(doc_->keyImage(l, pk), QColor(255, 60, 60)));
            cur = pk;
        }
        int nf = f;
        for (int i = 0; i < ltParams_.next; ++i) {
            const int nk = doc_->nextKeyIndex(l, nf);
            if (nk < 0) break;
            p.setOpacity(base * 0.85 * (1.0 - 0.5 * i / std::max(1, ltParams_.next)));
            p.drawImage(r, tinted(doc_->keyImage(l, nk), QColor(60, 120, 255)));
            nf = nk;
        }
        p.setOpacity(1.0);
    }

    // 오버레이(이미지 좌표계)
    p.save();
    p.translate(r.topLeft());
    p.scale(zoom_, zoom_);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QPen shapePen(colorA_, brush_.size, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    if (moving_ && !floating_.isNull()) p.drawImage(floatPos_, floating_);
    if (shaping_) {
        p.setOpacity(brush_.opacity / 100.0);
        p.setPen(shapePen);
        if (tool_ == Tool::Line) p.drawLine(shapeStart_, shapeEnd_);
        else if (tool_ == Tool::Rect) p.drawRect(QRectF(shapeStart_, shapeEnd_).normalized());
        else if (tool_ == Tool::Circle) p.drawEllipse(QRectF(shapeStart_, shapeEnd_).normalized());
        p.setOpacity(1.0);
    }
    if (tool_ == Tool::Spline && !splinePts_.isEmpty()) {
        QVector<QPointF> keep = splinePts_;
        splinePts_.push_back(hover_);
        const QPainterPath path = splinePath();
        splinePts_ = keep;
        p.setOpacity(brush_.opacity / 100.0);
        p.setPen(shapePen);
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
        p.setOpacity(1.0);
        p.setPen(QPen(QColor(0x4a, 0x90, 0xe2), 0));
        p.setBrush(Qt::white);
        for (const QPointF& pt : splinePts_) p.drawEllipse(pt, 3.0 / zoom_, 3.0 / zoom_);
    }
    if (selValid_ || selecting_) {
        QRect sr = selRect_;
        if (moving_) sr.moveTopLeft(floatPos_);
        p.setBrush(Qt::NoBrush);
        QPen dash(Qt::white, 0, Qt::DashLine);
        p.setPen(dash);
        p.drawRect(sr);
        QPen dash2(Qt::black, 0, Qt::DashLine);
        dash2.setDashOffset(3);
        p.setPen(dash2);
        p.drawRect(sr);
    }
    {
        const double w = s.width(), h = s.height();
        p.setBrush(Qt::NoBrush);
        p.setOpacity(1.0);
        if (guides_.grid && guides_.gridSize > 1) {
            p.setPen(QPen(QColor(120, 180, 255, 110), 0));
            for (double x = guides_.gridSize; x < w; x += guides_.gridSize) p.drawLine(QPointF(x, 0), QPointF(x, h));
            for (double y = guides_.gridSize; y < h; y += guides_.gridSize) p.drawLine(QPointF(0, y), QPointF(w, y));
        }
        if (guides_.thirds) {
            p.setPen(QPen(QColor(255, 255, 120, 140), 0));
            for (int i = 1; i < 3; ++i) {
                p.drawLine(QPointF(w * i / 3, 0), QPointF(w * i / 3, h));
                p.drawLine(QPointF(0, h * i / 3), QPointF(w, h * i / 3));
            }
        }
        if (guides_.center) {
            p.setPen(QPen(QColor(255, 120, 120, 170), 0));
            p.drawLine(QPointF(w / 2, 0), QPointF(w / 2, h));
            p.drawLine(QPointF(0, h / 2), QPointF(w, h / 2));
        }
        if (guides_.safe) {
            const double f = std::clamp(guides_.safePct, 10, 100) / 100.0;
            p.setPen(QPen(QColor(120, 255, 160, 170), 0, Qt::DashLine));
            p.drawRect(QRectF(w * (1 - f) / 2, h * (1 - f) / 2, w * f, h * f));
        }
    }
    if (doc_->cameraEnabled() && !cameraView_) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(255, 190, 0), 0));
        p.drawPolygon(doc_->cameraFrame(doc_->currentFrame()));
    }
    if (tool_ == Tool::Warp) {
        const double rr = 5.0 / zoom_;
        for (const Warp::Pin& pin : pins_) {
            if (pin.fixed) {
                p.setPen(QPen(QColor(80, 160, 255), 0));
                p.setBrush(QColor(80, 160, 255));
                p.drawRect(QRectF(pin.src.x() - rr, pin.src.y() - rr, rr * 2, rr * 2));
            } else {
                p.setPen(QPen(QColor(255, 120, 120), 0));
                p.drawLine(pin.src, pin.dst);
                p.setBrush(QColor(150, 150, 150));
                p.drawEllipse(pin.src, rr * 0.6, rr * 0.6);
                p.setPen(QPen(Qt::white, 0));
                p.setBrush(QColor(230, 60, 60));
                p.drawEllipse(pin.dst, rr, rr);
            }
        }
    }
    p.restore();

    p.setPen(QColor(0x80, 0x80, 0x80));
    p.setBrush(Qt::NoBrush);
    p.drawRect(r);

    const QRectF vr = visibleImageRect();
    if (vr != lastView_) {
        lastView_ = vr;
        QMetaObject::invokeMethod(this, &CanvasWidget::viewChanged, Qt::QueuedConnection);
    }
}

QRectF CanvasWidget::visibleImageRect() const {
    const QPointF tl = toImage(QPointF(0, 0));
    const QPointF br = toImage(QPointF(width(), height()));
    return QRectF(tl, br).normalized();
}

void CanvasWidget::centerOnImagePoint(const QPointF& ip) {
    const QSize s = doc_->size();
    // imageTopLeft = widget 중심 + pan_ - image 크기/2 * zoom (pan_ 이 중심 오프셋일 때)
    const QPointF cur = toImage(QPointF(width() / 2.0, height() / 2.0));
    pan_ -= (ip - cur) * zoom_;
    Q_UNUSED(s);
    update();
}

// ---------------------------------------------------------------- 편집 공통
bool CanvasWidget::beginEdit(int layer) {
    editLayer_ = layer >= 0 ? layer : doc_->currentLayer();
    const int f = doc_->currentFrame();
    const Layer& L = doc_->layer(editLayer_);
    if (L.locked || !L.visible) {
        emit message(QStringLiteral("잠겼거나 숨겨진 레이어에는 그릴 수 없습니다."));
        editLayer_ = -1;
        return false;
    }
    editKey_ = doc_->keyIndexAt(editLayer_, f);
    editBefore_ = editKey_ >= 0 ? doc_->keyImage(editLayer_, editKey_) : QImage();
    if (!doc_->editableImage(editLayer_, f)) return false;
    editKey_ = doc_->keyIndexAt(editLayer_, f);
    return editKey_ >= 0;
}

void CanvasWidget::commitEdit(const QString& text) {
    if (editLayer_ < 0 || editKey_ < 0) return;
    const QImage after = doc_->keyImage(editLayer_, editKey_);
    doc_->pushKeyEdit(editLayer_, editKey_, editBefore_, after, text);
    editLayer_ = -1;
    editKey_ = -1;
    editBefore_ = QImage();
}

// ---------------------------------------------------------------- 브러시 엔진
QImage CanvasWidget::paperTexture() const {
    if (paperTex_.isNull()) {
        paperTex_ = QImage(256, 256, QImage::Format_ARGB32_Premultiplied);
        QRandomGenerator rng(12345);
        for (int y = 0; y < 256; ++y) {
            QRgb* s = reinterpret_cast<QRgb*>(paperTex_.scanLine(y));
            for (int x = 0; x < 256; ++x) {
                const int a = 110 + int(rng.bounded(146));
                s[x] = qRgba(0, 0, 0, a);
            }
        }
    }
    return paperTex_;
}

const QImage& CanvasWidget::dab(int, int, const QColor&, bool) {
    return dabCache_.img; // 구 API (브러시 엔진은 Brush.cpp 로 이동)
}

void CanvasWidget::stamp(const QPointF& pos, double pr) {
    brushStampAt(strokeBuf_, brush_, tool_ == Tool::Eraser ? QColor(Qt::black) : colorA_, pos, pr, rng_, dabCache_);
}

void CanvasWidget::stampAlong(const QPointF& a, const QPointF& b, double pa, double pb) {
    brushStampAlong(strokeBuf_, brush_, tool_ == Tool::Eraser ? QColor(Qt::black) : colorA_, a, b, pa, pb, carry_, rng_, dabCache_);
}

void CanvasWidget::flushStroke() {
    QImage* img = doc_->editableImage(editLayer_, doc_->currentFrame());
    if (!img) return;
    *img = strokeBase_;
    QImage buf = strokeBuf_;
    if (tool_ == Tool::Pen || tool_ == Tool::Pencil || tool_ == Tool::Eraser) brushApplyGrain(buf, brushPreset(brush_.preset).grain);
    if (paper_) {
        QPainter bp(&buf);
        bp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        bp.fillRect(buf.rect(), QBrush(paperTexture()));
    }
    QPainter p(img);
    if (stencil_ && selValid_) p.setClipRect(selRect_);
    p.setOpacity(brush_.opacity / 100.0);
    if (tool_ == Tool::Eraser) p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
    p.drawImage(0, 0, buf);
    p.end();
    doc_->notifyChanged();
}

void CanvasWidget::beginStroke(const QPointF& ip) {
    if (!beginEdit()) return;
    strokeBase_ = doc_->keyImage(editLayer_, editKey_);
    strokeBuf_ = QImage(doc_->size(), QImage::Format_ARGB32_Premultiplied);
    strokeBuf_.fill(Qt::transparent);
    carry_ = 0.0;
    last_ = ip;
    lastPressure_ = pressure_;
    stroking_ = true;
    stamp(ip, pressure_);
    flushStroke();
}

void CanvasWidget::drawViaBuffer(const std::function<void(QPainter&)>& fn, const QString& text) {
    if (!beginEdit()) return;
    strokeBase_ = doc_->keyImage(editLayer_, editKey_);
    strokeBuf_ = QImage(doc_->size(), QImage::Format_ARGB32_Premultiplied);
    strokeBuf_.fill(Qt::transparent);
    {
        QPainter p(&strokeBuf_);
        p.setRenderHint(QPainter::Antialiasing, true);
        fn(p);
    }
    flushStroke();
    commitEdit(text);
}

// ---------------------------------------------------------------- 마우스
void CanvasWidget::mousePressEvent(QMouseEvent* e) {
    setFocus();
    if (e->button() == Qt::MiddleButton || tool_ == Tool::Pan || spaceDown_) {
        panning_ = true;
        panLast_ = e->pos();
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    const QPointF ip = toImage(e->position());

    // 워프: 오른쪽 클릭으로 핀 삭제
    if (tool_ == Tool::Warp && e->button() == Qt::RightButton) {
        for (int i = 0; i < pins_.size(); ++i) {
            const QPointF at = pins_[i].fixed ? pins_[i].src : pins_[i].dst;
            if (std::hypot(at.x() - ip.x(), at.y() - ip.y()) < 12.0 / zoom_) {
                pins_.removeAt(i);
                updateWarpPreview();
                break;
            }
        }
        update();
        return;
    }
    if (e->button() != Qt::LeftButton) return;

    switch (tool_) {
    case Tool::Pen:
    case Tool::Pencil:
    case Tool::Eraser:
        beginStroke(ip);
        break;
    case Tool::Line:
    case Tool::Rect:
    case Tool::Circle: {
        const Layer& L = doc_->layer(doc_->currentLayer());
        if (L.locked || !L.visible) {
            emit message(QStringLiteral("잠겼거나 숨겨진 레이어에는 그릴 수 없습니다."));
            return;
        }
        shaping_ = true;
        shapeStart_ = shapeEnd_ = ip;
        break;
    }
    case Tool::Spline: {
        const Layer& L = doc_->layer(doc_->currentLayer());
        if (L.locked || !L.visible) {
            emit message(QStringLiteral("잠겼거나 숨겨진 레이어에는 그릴 수 없습니다."));
            return;
        }
        splinePts_.push_back(ip);
        hover_ = ip;
        break;
    }
    case Tool::Bucket: {
        const int cur = doc_->currentLayer();
        if (doc_->layer(cur).ctgLines && cur > 0) ctgFill(ip.toPoint());
        else bucketFill(ip.toPoint());
        break;
    }
    case Tool::Select:
        if (selValid_ && selRect_.contains(ip.toPoint())) {
            const int f = doc_->currentFrame();
            const int l = doc_->currentLayer();
            if (doc_->layer(l).locked || doc_->keyIndexAt(l, f) < 0) return;
            if (!beginEdit()) return;
            QImage* img = doc_->editableImage(editLayer_, f);
            floating_ = img->copy(selRect_);
            {
                QPainter p(img);
                p.setCompositionMode(QPainter::CompositionMode_Clear);
                p.fillRect(selRect_, Qt::black);
            }
            moving_ = true;
            moveStart_ = ip;
            floatPos_ = selRect_.topLeft();
            doc_->notifyChanged();
        } else {
            selecting_ = true;
            selValid_ = false;
            selStart_ = ip.toPoint();
            selRect_ = QRect(selStart_, QSize(0, 0));
        }
        break;
    case Tool::Warp: {
        if (pins_.isEmpty()) {
            const int l = doc_->currentLayer();
            const int f = doc_->currentFrame();
            if (doc_->layer(l).locked || !doc_->layer(l).visible) {
                emit message(QStringLiteral("잠겼거나 숨겨진 레이어에는 적용할 수 없습니다."));
                return;
            }
            const int k = doc_->keyIndexAt(l, f);
            if (k < 0) {
                emit message(QStringLiteral("현재 프레임에 이미지가 없습니다."));
                return;
            }
            warpLayer_ = l;
            warpKey_ = k;
            warpBefore_ = doc_->keyImage(l, k);
        }
        dragPin_ = -1;
        for (int i = 0; i < pins_.size(); ++i) {
            if (pins_[i].fixed) continue;
            if (std::hypot(pins_[i].dst.x() - ip.x(), pins_[i].dst.y() - ip.y()) < 12.0 / zoom_) {
                dragPin_ = i;
                break;
            }
        }
        if (dragPin_ < 0) {
            Warp::Pin pin;
            pin.src = pin.dst = ip;
            pin.fixed = e->modifiers() & Qt::ShiftModifier;
            pins_.push_back(pin);
            if (!pin.fixed) dragPin_ = pins_.size() - 1;
        }
        break;
    }
    default:
        break;
    }
    update();
}

void CanvasWidget::mouseMoveEvent(QMouseEvent* e) {
    if (panning_) {
        pan_ += QPointF(e->pos() - panLast_);
        panLast_ = e->pos();
        update();
        return;
    }
    const QPointF ip = toImage(e->position());
    emit cursorMoved(int(std::floor(ip.x())), int(std::floor(ip.y())));
    hover_ = ip;

    if (stroking_) {
        QPointF target = ip;
        if (smoothing_) target = last_ + (ip - last_) * 0.4;
        stampAlong(last_, target, lastPressure_, pressure_);
        flushStroke();
        last_ = target;
        lastPressure_ = pressure_;
    } else if (shaping_) {
        shapeEnd_ = ip;
        update();
    } else if (selecting_) {
        selRect_ = QRect(selStart_, ip.toPoint()).normalized();
        update();
    } else if (moving_) {
        floatPos_ = selRect_.topLeft() + (ip - moveStart_).toPoint();
        update();
    } else if (dragPin_ >= 0 && (e->buttons() & Qt::LeftButton)) {
        pins_[dragPin_].dst = ip;
        if (warpTimer_.elapsed() > 60) {
            updateWarpPreview();
            warpTimer_.restart();
        }
        update();
    } else if (tool_ == Tool::Spline && !splinePts_.isEmpty()) {
        update();
    }
}

void CanvasWidget::mouseReleaseEvent(QMouseEvent* e) {
    if (panning_) {
        panning_ = false;
        setCursor(tool_ == Tool::Pan ? Qt::OpenHandCursor : Qt::CrossCursor);
        return;
    }
    if (e->button() != Qt::LeftButton) return;
    const QPointF ip = toImage(e->position());

    if (stroking_) {
        stroking_ = false;
        if (smoothing_ && last_ != ip) {
            stampAlong(last_, ip, lastPressure_, pressure_);
            flushStroke();
        }
        commitEdit(QStringLiteral("획 그리기"));
        pressure_ = 1.0;
        emit strokeFinished();
    } else if (shaping_) {
        shaping_ = false;
        const QPointF a = shapeStart_;
        const Tool t = tool_;
        const QPen pen(colorA_, brush_.size, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        drawViaBuffer(
            [&](QPainter& p) {
                p.setPen(pen);
                if (t == Tool::Line) p.drawLine(a, ip);
                else if (t == Tool::Rect) p.drawRect(QRectF(a, ip).normalized());
                else if (t == Tool::Circle) p.drawEllipse(QRectF(a, ip).normalized());
            },
            QStringLiteral("도형 그리기"));
        emit strokeFinished();
    } else if (selecting_) {
        selecting_ = false;
        selRect_ = QRect(selStart_, ip.toPoint()).normalized();
        selRect_ = selRect_.intersected(QRect(QPoint(0, 0), doc_->size()));
        selValid_ = selRect_.width() > 1 && selRect_.height() > 1;
    } else if (moving_) {
        moving_ = false;
        if (QImage* img = doc_->editableImage(editLayer_, doc_->currentFrame())) {
            QPainter p(img);
            p.drawImage(floatPos_, floating_);
        }
        selRect_.moveTopLeft(floatPos_);
        floating_ = QImage();
        doc_->notifyChanged();
        commitEdit(QStringLiteral("선택 영역 이동"));
    } else if (dragPin_ >= 0) {
        dragPin_ = -1;
        updateWarpPreview();
    }
    update();
}

void CanvasWidget::mouseDoubleClickEvent(QMouseEvent* e) {
    if (tool_ == Tool::Spline && e->button() == Qt::LeftButton) {
        if (splinePts_.size() >= 2 && splinePts_.last() == splinePts_[splinePts_.size() - 2]) splinePts_.removeLast();
        commitSpline();
        return;
    }
    QWidget::mouseDoubleClickEvent(e);
}

void CanvasWidget::tabletEvent(QTabletEvent* e) {
    pressure_ = std::clamp(double(e->pressure()), 0.0, 1.0);
    if (e->type() == QEvent::TabletRelease) pressure_ = 1.0;
    e->ignore(); // 마우스 이벤트로도 이어지게 한다
}

// ---------------------------------------------------------------- 스플라인
QPainterPath CanvasWidget::splinePath() const {
    QPainterPath path;
    const int n = splinePts_.size();
    if (n == 0) return path;
    path.moveTo(splinePts_[0]);
    if (n == 1) return path;
    for (int i = 0; i < n - 1; ++i) {
        const QPointF p1 = splinePts_[i], p2 = splinePts_[i + 1];
        const QPointF p0 = i > 0 ? splinePts_[i - 1] : p1;
        const QPointF p3 = i + 2 < n ? splinePts_[i + 2] : p2;
        path.cubicTo(p1 + (p2 - p0) / 6.0, p2 - (p3 - p1) / 6.0, p2);
    }
    return path;
}

void CanvasWidget::commitSpline() {
    if (splinePts_.size() < 2) {
        splinePts_.clear();
        update();
        return;
    }
    const QPainterPath path = splinePath();
    const QPen pen(colorA_, brush_.size, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    splinePts_.clear();
    drawViaBuffer(
        [&](QPainter& p) {
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawPath(path);
        },
        QStringLiteral("스플라인"));
    emit strokeFinished();
    update();
}

// ---------------------------------------------------------------- 페인트통
void CanvasWidget::bucketFill(const QPoint& seed) {
    if (!QRect(QPoint(0, 0), doc_->size()).contains(seed)) return;
    if (!beginEdit()) return;
    QImage* img = doc_->editableImage(editLayer_, doc_->currentFrame());
    const int w = img->width(), h = img->height();
    const QRgb target = img->pixel(seed);
    const QRgb fill = qPremultiply(colorA_.rgba());
    if (target == fill) {
        editLayer_ = editKey_ = -1;
        return;
    }
    auto similar = [&](QRgb c) {
        const int d = std::abs(qAlpha(c) - qAlpha(target)) + std::abs(qRed(c) - qRed(target)) +
                      std::abs(qGreen(c) - qGreen(target)) + std::abs(qBlue(c) - qBlue(target));
        return d <= 48;
    };
    QVector<uchar> seen(w * h, 0);
    QVector<QPoint> stack;
    stack.push_back(seed);
    seen[seed.y() * w + seed.x()] = 1;
    while (!stack.isEmpty()) {
        const QPoint c = stack.takeLast();
        reinterpret_cast<QRgb*>(img->scanLine(c.y()))[c.x()] = fill;
        const QPoint nb[4] = {{c.x() + 1, c.y()}, {c.x() - 1, c.y()}, {c.x(), c.y() + 1}, {c.x(), c.y() - 1}};
        for (const QPoint& n : nb) {
            if (n.x() < 0 || n.y() < 0 || n.x() >= w || n.y() >= h) continue;
            uchar& s = seen[n.y() * w + n.x()];
            if (s) continue;
            s = 1;
            if (similar(reinterpret_cast<const QRgb*>(img->constScanLine(n.y()))[n.x()])) stack.push_back(n);
        }
    }
    doc_->notifyChanged();
    commitEdit(QStringLiteral("페인트통"));
}

void CanvasWidget::ctgFill(const QPoint& seed) {
    const int lineLayer = doc_->currentLayer();
    const int colorLayer = lineLayer - 1;
    const int f = doc_->currentFrame();
    const int lk = doc_->keyIndexAt(lineLayer, f);
    if (lk < 0) {
        emit message(QStringLiteral("CTG 선화 레이어에 그림이 없습니다."));
        return;
    }
    const QImage lines = doc_->keyImage(lineLayer, lk);
    const int w = lines.width(), h = lines.height();
    if (!QRect(0, 0, w, h).contains(seed)) return;

    QVector<uchar> wall(w * h, 0);
    for (int y = 0; y < h; ++y) {
        const QRgb* s = reinterpret_cast<const QRgb*>(lines.constScanLine(y));
        for (int x = 0; x < w; ++x) wall[y * w + x] = qAlpha(s[x]) > 32;
    }
    dilateMask(wall, w, h, 2); // 2px 이하의 틈은 막힌 것으로 본다
    if (wall[seed.y() * w + seed.x()]) {
        emit message(QStringLiteral("선 위입니다. 닫힌 영역 안쪽을 클릭하세요."));
        return;
    }
    QVector<uchar> vis(w * h, 0);
    QVector<QPoint> stack;
    stack.push_back(seed);
    vis[seed.y() * w + seed.x()] = 1;
    while (!stack.isEmpty()) {
        const QPoint c = stack.takeLast();
        const QPoint nb[4] = {{c.x() + 1, c.y()}, {c.x() - 1, c.y()}, {c.x(), c.y() + 1}, {c.x(), c.y() - 1}};
        for (const QPoint& n : nb) {
            if (n.x() < 0 || n.y() < 0 || n.x() >= w || n.y() >= h) continue;
            const int i = n.y() * w + n.x();
            if (vis[i] || wall[i]) continue;
            vis[i] = 1;
            stack.push_back(n);
        }
    }
    dilateMask(vis, w, h, 3); // 선 아래까지 색이 들어가도록 확장

    if (!beginEdit(colorLayer)) return;
    QImage* img = doc_->editableImage(colorLayer, f);
    const QRgb fill = qPremultiply(colorA_.rgba());
    for (int y = 0; y < h; ++y) {
        QRgb* s = reinterpret_cast<QRgb*>(img->scanLine(y));
        for (int x = 0; x < w; ++x)
            if (vis[y * w + x]) s[x] = fill;
    }
    doc_->notifyChanged();
    commitEdit(QStringLiteral("CTG 채우기"));
}

// ---------------------------------------------------------------- 핀 워프
void CanvasWidget::updateWarpPreview() {
    if (warpKey_ < 0) return;
    const QImage res = pins_.isEmpty() ? warpBefore_ : Warp::pinWarp(warpBefore_, pins_);
    doc_->setKeyImage(warpLayer_, warpKey_, res);
}

void CanvasWidget::commitWarp() {
    if (pins_.isEmpty() || warpKey_ < 0) return;
    updateWarpPreview();
    const QImage after = doc_->keyImage(warpLayer_, warpKey_);
    doc_->pushKeyEdit(warpLayer_, warpKey_, warpBefore_, after, QStringLiteral("핀 워프"));
    pins_.clear();
    dragPin_ = -1;
    warpKey_ = -1;
    update();
}

void CanvasWidget::cancelWarp() {
    if (pins_.isEmpty() || warpKey_ < 0) return;
    doc_->setKeyImage(warpLayer_, warpKey_, warpBefore_);
    pins_.clear();
    dragPin_ = -1;
    warpKey_ = -1;
    update();
}

// ---------------------------------------------------------------- 선택
void CanvasWidget::clearSelectionArea() {
    if (!selValid_) return;
    const int l = doc_->currentLayer();
    const int k = doc_->keyIndexAt(l, doc_->currentFrame());
    if (k < 0 || doc_->layer(l).locked) return;
    if (!beginEdit()) return;
    QImage* img = doc_->editableImage(editLayer_, doc_->currentFrame());
    {
        QPainter p(img);
        p.setCompositionMode(QPainter::CompositionMode_Clear);
        p.fillRect(selRect_, Qt::black);
    }
    doc_->notifyChanged();
    commitEdit(QStringLiteral("선택 영역 지우기"));
}

void CanvasWidget::clearCurrent() {
    if (selValid_) {
        clearSelectionArea();
        return;
    }
    const int l = doc_->currentLayer();
    if (doc_->layer(l).locked) return;
    doc_->transformCurrent(
        [this](const QImage&) {
            QImage blank(doc_->size(), QImage::Format_ARGB32_Premultiplied);
            blank.fill(Qt::transparent);
            return blank;
        },
        QStringLiteral("이미지 지우기"));
}

void CanvasWidget::selectAll() {
    selRect_ = QRect(QPoint(0, 0), doc_->size());
    selValid_ = true;
    update();
}

void CanvasWidget::clearSelection() {
    selValid_ = false;
    update();
}

void CanvasWidget::wheelEvent(QWheelEvent* e) {
    const QPointF wp = e->position();
    const QPointF ip = toImage(wp);
    const qreal f = e->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
    zoom_ = std::clamp(zoom_ * f, 0.05, 32.0);
    const QSize s = doc_->size();
    const QPointF center((width() - s.width() * zoom_) / 2.0, (height() - s.height() * zoom_) / 2.0);
    pan_ = (wp - ip * zoom_) - center;
    emit zoomChanged(zoom_);
    update();
}

void CanvasWidget::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) {
        spaceDown_ = true;
        setCursor(Qt::OpenHandCursor);
    } else if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) {
        if (selValid_) clearSelectionArea();
    } else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        if (tool_ == Tool::Spline && splinePts_.size() >= 2) commitSpline();
        else if (!pins_.isEmpty()) commitWarp();
        else QWidget::keyPressEvent(e);
    } else if (e->key() == Qt::Key_Escape) {
        splinePts_.clear();
        cancelWarp();
        selValid_ = false;
        update();
    } else {
        QWidget::keyPressEvent(e);
    }
}

void CanvasWidget::keyReleaseEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) {
        spaceDown_ = false;
        setCursor(tool_ == Tool::Pan ? Qt::OpenHandCursor : Qt::CrossCursor);
    } else {
        QWidget::keyReleaseEvent(e);
    }
}
