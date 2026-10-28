#include <functional>
#include "CanvasWidget.h"
#include <QFontMetricsF>
#include <QGradient>
#include <QLineF>
#include <QPolygonF>
#include <QRegion>
#include <QVariantAnimation>
#include <QEasingCurve>
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
        if (!stroking_) partialDirty_ = QRect();
        ltCache_.clear();
        ltOverlay_ = QImage();
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
    ltCache_.clear();
    ltOverlay_ = QImage();
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

// 화면 한가운데를 축으로 도는 변환. 각도가 0 이면 아무 것도 하지 않는 변환이라 예전과 같은 길이다.
QTransform CanvasWidget::viewRotation(bool inverse) const {
    QTransform t;
    if (viewAngle_ == 0) return t;
    const QPointF c(width() / 2.0, height() / 2.0);
    t.translate(c.x(), c.y());
    t.rotate(inverse ? -viewAngle_ : viewAngle_);
    t.translate(-c.x(), -c.y());
    return t;
}

void CanvasWidget::setViewAngle(qreal deg) {
    // -180 ~ 180 으로 접어 둔다
    while (deg <= -180.0) deg += 360.0;
    while (deg > 180.0) deg -= 360.0;
    if (std::abs(deg) < 1e-9) deg = 0;
    if (std::abs(deg - viewAngle_) < 1e-9) return;
    viewAngle_ = deg;
    update();
    emit viewAngleChanged(viewAngle_);
    emit viewChanged();
}

QPointF CanvasWidget::toImage(const QPointF& p) const {
    // 화면 좌표를 먼저 거꾸로 돌린 뒤, 예전과 같은 셈을 한다
    const QPointF q = viewAngle_ == 0 ? p : viewRotation(true).map(p);
    return (q - imageTopLeft()) / zoom_;
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
    p.fillRect(rect(), backdrop_);
    const bool sizeOk = cache_.size() == doc_->size();
    if (cacheDirty_ || !sizeOk) {
        // 획을 그리는 중이면 바뀐 곳만 다시 합성한다 (큰 캔버스·여러 레이어에서 크게 빨라진다)
        bool done = false;
        if (sizeOk && stroking_ && !partialDirty_.isNull() && !cameraView_ && !(!ref_.isNull() && !refOnTop_))
            done = doc_->compositeRegion(cache_, partialDirty_, doc_->currentFrame(), true);
        // 참조 이미지를 캔버스 뒤에 둘 때는 종이를 깔지 않아야 비쳐 보인다
        const bool paper = !(!ref_.isNull() && !refOnTop_);
        if (!done) cache_ = doc_->composite(doc_->currentFrame(), paper, cameraView_);
        cacheDirty_ = false;
        partialDirty_ = QRect();
    }
    // 보기 각도가 있으면 여기서부터 화면 한가운데를 축으로 돌려 그린다.
    // 아래 그리기 코드는 돌지 않은 좌표를 그대로 쓰므로 손댈 것이 없다.
    if (viewAngle_ != 0) p.setTransform(viewRotation());
    const QSize s = doc_->size();
    const QRectF r(imageTopLeft(), QSizeF(s.width() * zoom_, s.height() * zoom_));
    p.setRenderHint(QPainter::SmoothPixmapTransform, zoom_ < 1.0 || viewAngle_ != 0);
    // 참조 이미지를 캔버스 자리에 비율을 지켜 맞춘다
    auto drawReference = [&] {
        if (ref_.isNull() || refOpacity_ <= 0.001) return;
        const double sc = std::min(r.width() / ref_.width(), r.height() / ref_.height());
        const QSizeF ds(ref_.width() * sc, ref_.height() * sc);
        const QRectF dst(r.x() + (r.width() - ds.width()) / 2, r.y() + (r.height() - ds.height()) / 2,
                         ds.width(), ds.height());
        p.save();
        p.setOpacity(refOpacity_);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.drawImage(dst, ref_);
        p.restore();
    };
    if (!refOnTop_) drawReference();
    // 화면에 보이는 부분만 그린다 (크게 확대했을 때 특히 빠르다)
    // 돌려 놓았으면 화면 사각형을 거꾸로 돌린 뒤 그 테두리로 본다
    const QRectF screenInView =
        viewAngle_ == 0 ? QRectF(rect()) : viewRotation(true).map(QRectF(rect())).boundingRect();
    const QRectF visible = screenInView.intersected(r);
    if (visible.isEmpty()) {
        // 캔버스가 화면 밖이면 그릴 것이 없다
    } else if (visible == r) {
        p.drawImage(r, cache_);
    } else {
        const QRectF src((visible.x() - r.x()) / zoom_, (visible.y() - r.y()) / zoom_, visible.width() / zoom_,
                         visible.height() / zoom_);
        p.drawImage(visible, cache_, src);
    }

    // 라이트 테이블: 앞뒤 이미지를 색을 입혀 흐리게 표시 (앞뒤 그림을 비쳐 보는 기능)
    // 여러 장을 매번 겹쳐 그리면 느리므로, 한 장으로 미리 합쳐 두고 그것만 그린다.
    if (lightTable_ && ltOverlayFrame_ == doc_->currentFrame() && !ltOverlay_.isNull() &&
        ltOverlay_.size() == doc_->size()) {
        p.drawImage(r, ltOverlay_);
    } else if (lightTable_) {
        ltOverlay_ = QImage(doc_->size(), QImage::Format_ARGB32_Premultiplied);
        ltOverlay_.fill(Qt::transparent);
        QPainter op_(&ltOverlay_);
        const auto& P = ltParams_;
        const int curF = doc_->currentFrame();
        QVector<int> layers;
        if (P.allLayers) {
            for (int l = 0; l < doc_->layerCount(); ++l)
                if (doc_->layer(l).visible) layers << l;
        } else {
            layers << doc_->currentLayer();
        }
        // 색 입힌 그림은 매번 다시 만들지 않고 캐시에 둔다 (그림이 바뀌면 캐시가 비워진다)
        auto tinted = [&](const QImage& src, const QColor& c, int dist, int cnt) {
            if (P.colorMode == 1) return src;
            QColor col = c;
            if (P.colorMode == 2) { // 멀어질수록 흰색 쪽으로 옅어진다
                const double t = cnt > 1 ? double(dist) / (cnt - 1) : 0.0;
                col = QColor::fromRgbF(c.redF() + (1 - c.redF()) * 0.6 * t, c.greenF() + (1 - c.greenF()) * 0.6 * t,
                                       c.blueF() + (1 - c.blueF()) * 0.6 * t);
            }
            const quint64 key = quint64(src.cacheKey()) ^ (quint64(col.rgba()) << 1) ^ (quint64(P.colorMode) << 33);
            auto it = ltCache_.constFind(key);
            if (it != ltCache_.constEnd()) return it.value();
            QImage t = src;
            QPainter tp(&t);
            tp.setCompositionMode(QPainter::CompositionMode_SourceIn);
            tp.fillRect(t.rect(), col);
            tp.end();
            if (ltCache_.size() > 48) ltCache_.clear();
            ltCache_.insert(key, t);
            return t;
        };
        for (int l : layers) {
            // 이전/다음 이미지가 놓인 프레임 번호 목록 (가까운 순서)
            QVector<int> prevF, nextF;
            auto matches = [&](int f) {
                if (f < 0 || f >= doc_->frameCount()) return false;
                switch (P.mode) {
                case LightTableParams::Frame: return doc_->resolveKey(l, f) >= 0;
                case LightTableParams::Bookmark: return doc_->hasBookmark(f) && doc_->resolveKey(l, f) >= 0;
                case LightTableParams::ImageMark: return doc_->layer(l).imageMarks.contains(f);
                default: return !doc_->layer(l).frames[f].isNull();
                }
            };
            const int curKey = doc_->keyIndexAt(l, curF);
            for (int f = (P.mode == LightTableParams::Instance && curKey >= 0 ? curKey : curF) - 1; f >= 0 && prevF.size() < LightTableParams::kSlots; --f)
                if (matches(f)) prevF << f;
            for (int f = curF + 1; f < doc_->frameCount() && nextF.size() < LightTableParams::kSlots; ++f)
                if (matches(f)) nextF << f;
            auto drawOne = [&](int f, const QColor& col, int op, int dist, int cnt) {
                if (op <= 0) return;
                const int k = P.mode == LightTableParams::Frame || P.mode == LightTableParams::Bookmark ? doc_->resolveKey(l, f) : f;
                if (k < 0) return;
                op_.setOpacity(op / 100.0 * doc_->layer(l).opacity);
                op_.drawImage(0, 0, tinted(doc_->keyImage(l, k), col, dist, cnt));
            };
            for (int i = 0; i < prevF.size(); ++i) drawOne(prevF[i], P.prevColor, P.prevOp[i], i, prevF.size());
            for (int i = 0; i < nextF.size(); ++i) drawOne(nextF[i], P.nextColor, P.nextOp[i], i, nextF.size());
            // 기본은 현재 그림 뒤에 보이도록, 현재 레이어의 그림을 다시 위에 얹는다
            if (!P.blendToFront && l == doc_->currentLayer()) {
                const int ck = doc_->resolveKey(l, curF);
                if (ck >= 0) {
                    op_.setOpacity(doc_->layer(l).opacity);
                    op_.drawImage(0, 0, doc_->keyImage(l, ck));
                }
            }
        }
        op_.end();
        ltOverlayFrame_ = doc_->currentFrame();
        p.setOpacity(1.0);
        p.drawImage(r, ltOverlay_);
    }
    if (refOnTop_) drawReference();

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
        if (!selMask_.isNull() && !moving_) {
            // 사각형이 아닌 선택: 마스크를 옅게 덮고 경계 상자를 점선으로 표시
            p.setOpacity(0.22);
            p.drawImage(0, 0, selMask_);
            p.setOpacity(1.0);
        }
        QPen dash(Qt::white, 0, Qt::DashLine);
        p.setPen(dash);
        p.drawRect(sr);
        QPen dash2(Qt::black, 0, Qt::DashLine);
        dash2.setDashOffset(3);
        p.setPen(dash2);
        p.drawRect(sr);
    }
    if (doc_->isVectorLayer(doc_->currentLayer()) && (tool_ == Tool::Select || vecEditStroke_ >= 0)) {
        const QVector<VecStroke>* list = doc_->vectorStrokes(doc_->currentLayer(), doc_->currentFrame());
        if (list) {
            const double r = 3.0 / zoom_;
            for (int si = 0; si < list->size(); ++si) {
                const VecStroke& s = (*list)[si];
                const bool sel = si == vecSel_;
                if (sel && s.pts.size() > 1) {   // 고른 획은 밝은 선으로 덧그린다
                    p.setBrush(Qt::NoBrush);
                    p.setPen(QPen(QColor(0x4a, 0x90, 0xe2, 200), 0, Qt::DashLine));
                    p.drawPolyline(QPolygonF(s.pts));
                }
                p.setPen(QPen(sel ? QColor(0x4a, 0x90, 0xe2) : QColor(0x30, 0xc8, 0xa0), 0));
                p.setBrush(sel ? QColor(0xff, 0xee, 0x88) : QColor(Qt::white));
                for (const QPointF& pt : s.pts) p.drawEllipse(pt, sel ? r * 1.4 : r, sel ? r * 1.4 : r);
            }
        }
    }
    if (!lassoPts_.isEmpty()) {
        QPolygonF poly(lassoPts_);
        if (tool_ == Tool::Polygon) poly << hover_;
        p.setBrush(QColor(0x4a, 0x90, 0xe2, 40));
        p.setPen(QPen(QColor(0x4a, 0x90, 0xe2), 0, Qt::DashLine));
        p.drawPolygon(poly);
        if (tool_ == Tool::Polygon) {
            p.setBrush(Qt::white);
            p.setPen(QPen(QColor(0x4a, 0x90, 0xe2), 0));
            for (const QPointF& pt : lassoPts_) p.drawEllipse(pt, 3.0 / zoom_, 3.0 / zoom_);
        }
    }
    if (grading_) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(0xff, 0xcc, 0x33), 0));
        p.drawLine(gradStart_, gradEnd_);
        p.drawEllipse(gradStart_, 4.0 / zoom_, 4.0 / zoom_);
    }
    if (sym_.on) {
        const double cx = sym_.axisX < 0 ? s.width() / 2.0 : sym_.axisX;
        const double cy = sym_.axisY < 0 ? s.height() / 2.0 : sym_.axisY;
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(0xff, 0x7a, 0xc8, 170), 0, Qt::DashLine));
        if (sym_.vertical) p.drawLine(QPointF(cx, 0), QPointF(cx, s.height()));
        if (sym_.horizontal) p.drawLine(QPointF(0, cy), QPointF(s.width(), cy));
        if (sym_.radial > 1) p.drawEllipse(QPointF(cx, cy), 8.0 / zoom_, 8.0 / zoom_);
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
        if (guides_.perspective) {
            p.setPen(QPen(QColor(255, 150, 90, 95), 0));
            for (int i = 0; i < std::clamp(guides_.vanishCount, 1, 3); ++i) {
                const QPointF v = vanishPoint(i);
                for (int k = 0; k <= 16; ++k) {
                    const double t = k / 16.0;
                    p.drawLine(v, QPointF(w * t, 0));
                    p.drawLine(v, QPointF(w * t, h));
                }
                p.setPen(QPen(QColor(255, 110, 60), 0));
                p.drawEllipse(v, 6.0 / zoom_, 6.0 / zoom_);
                p.setPen(QPen(QColor(255, 150, 90, 95), 0));
            }
        }
        if (guides_.ruler == 2) {
            p.setPen(QPen(QColor(120, 220, 180, 130), 0, Qt::DashLine));
            const double a = guides_.rulerAngle * M_PI / 180.0;
            const QPointF c(w / 2, h / 2), d(std::cos(a), std::sin(a));
            p.drawLine(c - d * (w + h), c + d * (w + h));
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

void CanvasWidget::markDirty(const QRectF& r) {
    const double m = brush_.size * 1.2 + 8;
    dirty_ = dirty_.united(r.adjusted(-m, -m, m, m).toAlignedRect());
}

QPointF CanvasWidget::vanishPoint(int i) const {
    const QSizeF s = doc_->size();
    if (i < 0 || i > 2) i = 0;
    if (guides_.vp[i][0] >= 0 || guides_.vp[i][1] >= 0) return QPointF(guides_.vp[i][0], guides_.vp[i][1]);
    // 기본 위치: 눈높이(가운데 높이)에 좌·우·가운데
    const double y = s.height() / 2.0;
    const double xs[3] = {-s.width() * 0.4, s.width() * 1.4, s.width() / 2.0};
    return QPointF(xs[i], y);
}

// 대칭 그리기: 원본 + 거울상/방사 변환 목록 (대칭이 꺼져 있으면 단위 변환 하나)
QVector<QTransform> CanvasWidget::symmetryTransforms() const {
    QVector<QTransform> v{QTransform()};
    if (!sym_.on) return v;
    const double cx = sym_.axisX < 0 ? doc_->size().width() / 2.0 : sym_.axisX;
    const double cy = sym_.axisY < 0 ? doc_->size().height() / 2.0 : sym_.axisY;
    const int n = std::clamp(sym_.radial, 1, 24);
    QVector<QTransform> base{QTransform()};
    for (int i = 1; i < n; ++i) {
        QTransform t;
        t.translate(cx, cy);
        t.rotate(360.0 * i / n);
        t.translate(-cx, -cy);
        base.push_back(t);
    }
    v = base;
    if (sym_.vertical) {
        const int m = v.size();
        for (int i = 0; i < m; ++i) {
            QTransform t;
            t.translate(cx, 0);
            t.scale(-1, 1);
            t.translate(-cx, 0);
            v.push_back(v[i] * t);
        }
    }
    if (sym_.horizontal) {
        const int m = v.size();
        for (int i = 0; i < m; ++i) {
            QTransform t;
            t.translate(0, cy);
            t.scale(1, -1);
            t.translate(0, -cy);
            v.push_back(v[i] * t);
        }
    }
    return v;
}

void CanvasWidget::stamp(const QPointF& pos, double pr) {
    const QColor c = tool_ == Tool::Eraser ? QColor(Qt::black) : colorA_;
    for (const QTransform& t : symmetryTransforms()) {
        const QPointF p = t.map(pos);
        markDirty(QRectF(p, p));
        brushStampAt(strokeBuf_, brush_, c, p, pr, rng_, dabCache_);
    }
}

void CanvasWidget::stampAlong(const QPointF& a, const QPointF& b, double pa, double pb) {
    const QColor c = tool_ == Tool::Eraser ? QColor(Qt::black) : colorA_;
    const QVector<QTransform> ts = symmetryTransforms();
    if (ts.size() == 1) {
        markDirty(QRectF(a, b).normalized());
        brushStampAlong(strokeBuf_, brush_, c, a, b, pa, pb, carry_, rng_, dabCache_);
        return;
    }
    double carry = carry_;
    for (int i = 0; i < ts.size(); ++i) {
        const QPointF p = ts[i].map(a), q = ts[i].map(b);
        markDirty(QRectF(p, q).normalized());
        double cc = carry_;
        brushStampAlong(strokeBuf_, brush_, c, p, q, pa, pb, cc, rng_, dabCache_);
        if (i == 0) carry = cc;
    }
    carry_ = carry;
}

// 획이 바뀐 영역(dirty_)만 다시 합성한다. 캔버스가 커도 마우스를 움직일 때마다 드는 비용이 획 크기에만 비례한다.
void CanvasWidget::flushStroke() {
    QImage* img = doc_->editableImage(editLayer_, doc_->currentFrame());
    if (!img) return;
    const QRect full(QPoint(0, 0), strokeBuf_.size());
    const QRect r = dirty_.isNull() ? QRect() : dirty_.intersected(full);
    if (r.isEmpty() || strokeBase_.size() != img->size()) {
        if (strokeBase_.size() == img->size()) return;   // 바뀐 곳이 없다
        *img = strokeBase_;
        doc_->notifyChanged();
        return;
    }
    {   // 바뀐 영역만 원본으로 되돌린다
        QPainter rp(img);
        rp.setCompositionMode(QPainter::CompositionMode_Source);
        rp.drawImage(r.topLeft(), strokeBase_, r);
    }
    QImage buf = strokeBuf_.copy(r);
    if (tool_ == Tool::Pen || tool_ == Tool::Pencil || tool_ == Tool::Eraser) brushApplyGrain(buf, brushPreset(brush_.preset).grain, r.topLeft());
    if (paper_) {
        QPainter bp(&buf);
        bp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        QBrush pb(paperTexture());
        pb.setTransform(QTransform::fromTranslate(-r.x(), -r.y()));
        bp.fillRect(buf.rect(), pb);
    }
    // 스텐실: 다른 레이어의 불투명한 곳에만 그릴 수 있다
    if (!stencilMask_.isNull()) {
        QPainter mp(&buf);
        mp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        mp.drawImage(-r.x(), -r.y(), stencilMask_);
    }
    // 가장자리를 부드럽게 한 선택: 마스크의 투명도대로 깎는다
    if (stencil_ && selValid_ && selFeathered_ && !selMask_.isNull()) {
        QPainter mp(&buf);
        mp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        mp.drawImage(-r.x(), -r.y(), selMask_);
    }
    QPainter p(img);
    if (stencil_ && selValid_ && !selFeathered_) applySelectionClip(p);
    p.setOpacity(brush_.opacity / 100.0);
    if (tool_ == Tool::Eraser) p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
    else if (doc_->layer(editLayer_).preserveAlpha) p.setCompositionMode(QPainter::CompositionMode_SourceAtop); // 알파 채널 보존
    p.drawImage(r.topLeft(), buf);
    p.end();
    partialDirty_ = partialDirty_.isNull() ? r : partialDirty_.united(r);
    doc_->notifyChanged();
}

void CanvasWidget::beginStroke(const QPointF& ip) {
    if (!beginEdit()) return;
    strokeBase_ = doc_->keyImage(editLayer_, editKey_);
    stencilMask_ = doc_->stencilMaskFor(editLayer_, doc_->currentFrame());
    strokeBuf_ = QImage(doc_->size(), QImage::Format_ARGB32_Premultiplied);
    strokeBuf_.fill(Qt::transparent);
    dirty_ = QRect();
    carry_ = 0.0;
    last_ = ip;
    strokeOrigin_ = ip;
    lastPressure_ = pressure_;
    stroking_ = true;
    vecPts_.clear();
    vecPr_.clear();
    if (doc_->isVectorLayer(editLayer_)) {
        vecPts_.push_back(ip);
        vecPr_.push_back(float(pressure_));
    }
    if (doc_->recording()) {
        rec_ = StrokeRec();
        rec_.layer = editLayer_;
        rec_.frame = doc_->currentFrame();
        rec_.brush = brush_;
        rec_.color = colorA_;
        rec_.eraser = tool_ == Tool::Eraser;
        recClock_.start();
        recActive_ = true;
        rec_.pts.push_back(ip);
        rec_.pressure.push_back(float(pressure_));
        rec_.ms.push_back(0);
    }
    stamp(ip, pressure_);
    flushStroke();
}

void CanvasWidget::drawViaBuffer(const std::function<void(QPainter&)>& fn, const QString& text) {
    if (!beginEdit()) return;
    strokeBase_ = doc_->keyImage(editLayer_, editKey_);
    stencilMask_ = doc_->stencilMaskFor(editLayer_, doc_->currentFrame());
    strokeBuf_ = QImage(doc_->size(), QImage::Format_ARGB32_Premultiplied);
    strokeBuf_.fill(Qt::transparent);
    {
        QPainter p(&strokeBuf_);
        p.setRenderHint(QPainter::Antialiasing, true);
        fn(p);
    }
    dirty_ = QRect(QPoint(0, 0), strokeBuf_.size());
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

    // Alt 를 누른 채 클릭하면 어떤 도구에서든 색을 집는다 (임시 스포이트).
    // 다만 벡터 레이어에서 선택 도구일 때는 Alt+클릭이 "점 지우기" 라서 비켜 준다.
    const bool vecEditing = tool_ == Tool::Select && doc_->isVectorLayer(doc_->currentLayer()) && !selValid_;
    if ((e->modifiers() & Qt::AltModifier) && !vecEditing && tool_ != Tool::Lasso && tool_ != Tool::Polygon &&
        tool_ != Tool::Wand) {
        pickColorAt(ip);
        return;
    }

    switch (tool_) {
    case Tool::Pick:
        pickColorAt(ip);
        break;
    case Tool::Lasso:
        lassoing_ = true;
        lassoPts_.clear();
        lassoPts_.push_back(ip);
        break;
    case Tool::Polygon:
        if (!lassoPts_.isEmpty() && QLineF(lassoPts_.first(), ip).length() < 8.0 / zoom_ && lassoPts_.size() >= 3) {
            QPainterPath path;
            path.addPolygon(QPolygonF(lassoPts_));
            path.closeSubpath();
            setSelectionFromPath(path, e->modifiers());
            lassoPts_.clear();
        } else {
            lassoPts_.push_back(ip);
        }
        update();
        break;
    case Tool::Wand:
        wandSelect(ip.toPoint(), e->modifiers());
        break;
    case Tool::Gradient: {
        const Layer& L = doc_->layer(doc_->currentLayer());
        if (L.locked || !L.visible) {
            emit message(QStringLiteral("잠겼거나 숨겨진 레이어에는 그릴 수 없습니다."));
            return;
        }
        grading_ = true;
        gradStart_ = gradEnd_ = ip;
        break;
    }
    case Tool::Text:
        commitText(ip);
        break;
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
        // 벡터 레이어: 점 끌기 · 점 추가·삭제 · 획 고르기 · 획 통째로 옮기기
        if (doc_->isVectorLayer(doc_->currentLayer()) && !selValid_) {
            const QVector<VecStroke>* list = doc_->vectorStrokes(doc_->currentLayer(), doc_->currentFrame());
            if (list) {
                int hs = -1;
                const int hp = vecHitPoint(ip, &hs);
                if (hp >= 0) {
                    if (e->modifiers() & Qt::AltModifier) {   // Alt+클릭 = 점 지우기
                        QVector<VecStroke> w = *list;
                        if (w[hs].pts.size() <= 2) {
                            emit message(QStringLiteral("점이 두 개뿐이라 지울 수 없습니다. 획을 지우려면 레이어 메뉴를 쓰세요."));
                        } else {
                            w[hs].pts.remove(hp);
                            if (hp < w[hs].pressure.size()) w[hs].pressure.remove(hp);
                            doc_->setVectorStrokes(doc_->currentLayer(), doc_->currentFrame(), w);
                            emit message(QStringLiteral("점을 지웠습니다."));
                        }
                        setSelectedVectorStroke(hs);
                        return;
                    }
                    vecEditStroke_ = hs;
                    vecEditPoint_ = hp;
                    vecWork_ = *list;
                    setSelectedVectorStroke(hs);
                    return;
                }
                int seg = -1;
                QPointF at;
                const int st = vecHitStroke(ip, &seg, &at);
                if (st >= 0) {
                    if (e->modifiers() & Qt::ControlModifier) {   // Ctrl+클릭 = 선 위에 점 추가
                        QVector<VecStroke> w = *list;
                        w[st].pts.insert(seg + 1, at);
                        w[st].pressure.insert(std::min(seg + 1, int(w[st].pressure.size())), 1.0f);
                        doc_->setVectorStrokes(doc_->currentLayer(), doc_->currentFrame(), w);
                        setSelectedVectorStroke(st);
                        vecEditStroke_ = st;
                        vecEditPoint_ = seg + 1;
                        vecWork_ = *doc_->vectorStrokes(doc_->currentLayer(), doc_->currentFrame());
                        emit message(QStringLiteral("점을 넣었습니다."));
                        return;
                    }
                    if (e->modifiers() & Qt::ShiftModifier) {     // Shift+끌기 = 획 통째로 옮기기
                        setSelectedVectorStroke(st);
                        vecEditStroke_ = st;
                        vecEditPoint_ = -1;
                        vecMoveWhole_ = true;
                        vecMoveFrom_ = ip;
                        vecWork_ = *list;
                        return;
                    }
                    setSelectedVectorStroke(st);
                    emit message(QString("획 %1 을 골랐습니다. Shift+끌기로 옮기고, Ctrl+클릭으로 점을 넣고, Alt+클릭으로 점을 지웁니다.").arg(st + 1));
                    return;
                }
                setSelectedVectorStroke(-1);
            }
        }
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

    if (lassoing_) {
        if (lassoPts_.isEmpty() || QLineF(lassoPts_.last(), ip).length() > 1.5 / zoom_) lassoPts_.push_back(ip);
        update();
        return;
    }
    if (grading_) {
        gradEnd_ = ip;
        update();
        return;
    }
    if (stroking_) {
        QPointF target = ip;
        if (smoothing_) target = last_ + (ip - last_) * (1.0 - smoothStrength_ / 100.0);
        // 직선자 / 소실점: 획 시작점에서 정해진 방향으로만 나가게 한다
        if (guides_.ruler > 0 || guides_.snapToVanish) {
            const QPointF o = strokeOrigin_;
            const QPointF d = target - o;
            QPointF dir;
            if (guides_.snapToVanish) {
                double best = 1e18;
                for (int i = 0; i < std::clamp(guides_.vanishCount, 1, 3); ++i) {
                    QPointF v = o - vanishPoint(i);
                    const double len = std::hypot(v.x(), v.y());
                    if (len < 1e-6) continue;
                    v /= len;
                    const double proj = d.x() * v.x() + d.y() * v.y();
                    const QPointF perp = d - v * proj;
                    const double err = std::hypot(perp.x(), perp.y());
                    if (err < best) {
                        best = err;
                        dir = v;
                    }
                }
            } else if (guides_.ruler == 1) {
                dir = std::abs(d.x()) >= std::abs(d.y()) ? QPointF(1, 0) : QPointF(0, 1);
            } else if (guides_.ruler == 2) {
                const double a = guides_.rulerAngle * M_PI / 180.0;
                dir = QPointF(std::cos(a), std::sin(a));
            }
            if (!dir.isNull()) {
                const double proj = d.x() * dir.x() + d.y() * dir.y();
                target = o + dir * proj;
            }
        }
        stampAlong(last_, target, lastPressure_, pressure_);
        flushStroke();
        if (recActive_) {
            rec_.pts.push_back(target);
            rec_.pressure.push_back(float(pressure_));
            rec_.ms.push_back(int(recClock_.elapsed()));
        }
        if (!vecPts_.isEmpty() && QLineF(vecPts_.last(), target).length() > 2.5) {
            vecPts_.push_back(target);
            vecPr_.push_back(float(pressure_));
        }
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
    } else if (vecEditStroke_ >= 0 && (e->buttons() & Qt::LeftButton)) {
        if (vecMoveWhole_ && vecEditStroke_ < vecWork_.size()) {
            const QPointF d = ip - vecMoveFrom_;
            QVector<VecStroke> w = vecWork_;
            for (QPointF& pt : w[vecEditStroke_].pts) pt += d;
            doc_->setVectorStrokes(doc_->currentLayer(), doc_->currentFrame(), w);
        } else if (vecEditPoint_ >= 0 && vecEditStroke_ < vecWork_.size() && vecEditPoint_ < vecWork_[vecEditStroke_].pts.size()) {
            vecWork_[vecEditStroke_].pts[vecEditPoint_] = ip;
            doc_->setVectorStrokes(doc_->currentLayer(), doc_->currentFrame(), vecWork_);
        }
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

    if (lassoing_) {
        lassoing_ = false;
        if (lassoPts_.size() >= 3) {
            QPainterPath path;
            path.addPolygon(QPolygonF(lassoPts_));
            path.closeSubpath();
            setSelectionFromPath(path, e->modifiers());
        }
        lassoPts_.clear();
        update();
        return;
    }
    if (grading_) {
        grading_ = false;
        commitGradient(gradStart_, ip);
        update();
        return;
    }
    if (stroking_) {
        stroking_ = false;
        if (smoothing_ && last_ != ip) {
            stampAlong(last_, ip, lastPressure_, pressure_);
            flushStroke();
            if (!vecPts_.isEmpty() && QLineF(vecPts_.last(), ip).length() > 0.5) {
                vecPts_.push_back(ip);
                vecPr_.push_back(float(pressure_));
            }
            if (recActive_) {
                rec_.pts.push_back(ip);
                rec_.pressure.push_back(float(pressure_));
                rec_.ms.push_back(int(recClock_.elapsed()));
            }
        }
        if (recActive_) {
            recActive_ = false;
            if (rec_.pts.size() == 1) { // 점 하나: 아주 짧은 선으로 만들어 재생 때 보이게 한다
                rec_.pts.push_back(rec_.pts[0] + QPointF(0.01, 0));
                rec_.pressure.push_back(rec_.pressure[0]);
                rec_.ms.push_back(rec_.ms[0] + 30);
            }
            doc_->addStroke(rec_);
        }
        if (doc_->isVectorLayer(editLayer_) && !vecPts_.isEmpty()) {
            // 벡터 레이어: 래스터 대신 획을 저장하고 다시 그린다
            VecStroke vs;
            vs.pts = vecPts_;
            vs.pressure = vecPr_;
            vs.color = colorA_;
            vs.width = brush_.size;
            vs.eraser = tool_ == Tool::Eraser;
            if (vs.pts.size() == 1) vs.pts.push_back(vs.pts[0] + QPointF(0.01, 0));
            *doc_->editableImage(editLayer_, doc_->currentFrame()) = strokeBase_;   // 미리보기 지우고
            doc_->addVectorStroke(editLayer_, doc_->currentFrame(), vs);
            vecPts_.clear();
            vecPr_.clear();
            editLayer_ = editKey_ = -1;
            editBefore_ = QImage();
            emit strokeFinished();
            update();
            return;
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
    } else if (vecEditStroke_ >= 0) {
        vecEditStroke_ = -1;
        vecEditPoint_ = -1;
        vecMoveWhole_ = false;
        emit strokeFinished();
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
    // 펜 기울기 (xTilt/yTilt 는 -60~60도). 붓이 납작해지는 데에 쓴다.
    const double tx = e->xTilt(), ty = e->yTilt();
    const double mag = std::min(1.0, std::hypot(tx, ty) / 60.0);
    tiltAmount_ = mag;
    if (mag > 0.02) tiltAngle_ = std::atan2(ty, tx) * 180.0 / M_PI;
    brush_.tiltAngle = tiltAngle_;
    brush_.tiltAmount = tiltAmount_;
    e->ignore(); // 마우스 이벤트로도 이어지게 한다
}

void CanvasWidget::setPenTilt(double angleDeg, double amount) {
    tiltAngle_ = angleDeg;
    tiltAmount_ = std::clamp(amount, 0.0, 1.0);
    brush_.tiltAngle = tiltAngle_;
    brush_.tiltAmount = tiltAmount_;
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
    // 자동 채색(일괄)에서 그대로 다시 쓸 수 있게 적어 둔다
    doc_->recordFill(seed, colorA_);
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

// ---------------------------------------------------------------- 그라디언트 · 텍스트
void CanvasWidget::commitGradient(const QPointF& a, const QPointF& b) {
    if (QLineF(a, b).length() < 1.0) return;
    const QColor c0 = colorA_;
    QColor c1 = grad_.toTransparent ? QColor(colorA_.red(), colorA_.green(), colorA_.blue(), 0) : colorB_;
    QGradient* g = nullptr;
    QLinearGradient lin(a, b);
    QRadialGradient rad(a, QLineF(a, b).length());
    QConicalGradient con(a, -QLineF(a, b).angle());
    if (grad_.type == 1) g = &rad;
    else if (grad_.type == 2) g = &con;
    else g = &lin;
    g->setColorAt(0.0, c0);
    g->setColorAt(1.0, c1);
    const QRect area = selValid_ ? selRect_ : QRect(QPoint(0, 0), doc_->size());
    drawViaBuffer(
        [&](QPainter& p) {
            if (selValid_) applySelectionClip(p);
            p.fillRect(area, QBrush(*g));
        },
        QStringLiteral("그라디언트"));
    emit strokeFinished();
}

void CanvasWidget::commitText(const QPointF& at) {
    if (text_.text.isEmpty()) {
        emit message(QStringLiteral("넣을 글자가 없습니다 (도구 옵션에서 글자를 입력하세요)."));
        return;
    }
    QFont f;
    if (!text_.family.isEmpty()) f.setFamily(text_.family);
    f.setPixelSize(std::max(4, text_.size));
    f.setBold(text_.bold);
    f.setItalic(text_.italic);
    QPainterPath path;
    const QStringList lines = text_.text.split('\n');
    QFontMetricsF fm(f);
    for (int i = 0; i < lines.size(); ++i)
        path.addText(at.x(), at.y() + i * fm.height(), f, lines[i]);
    drawViaBuffer(
        [&](QPainter& p) {
            if (selValid_) applySelectionClip(p);
            p.setRenderHint(QPainter::Antialiasing, true);
            if (text_.outline) {
                p.setPen(QPen(colorB_, std::max(1, text_.outlineWidth) * 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                p.setBrush(Qt::NoBrush);
                p.drawPath(path);
            }
            p.setPen(Qt::NoPen);
            p.setBrush(colorA_);
            p.drawPath(path);
        },
        QStringLiteral("글자 넣기"));
    textPos_ = at;
    emit strokeFinished();
}

// ---------------------------------------------------------------- 선택
void CanvasWidget::applySelectionClip(QPainter& p) const {
    if (!selValid_) return;
    if (selMask_.isNull()) {
        p.setClipRect(selRect_);
        return;
    }
    // 마스크의 불투명한 부분만 남긴다
    QRegion rgn;
    const int w = selMask_.width(), h = selMask_.height();
    for (int y = 0; y < h; ++y) {
        const QRgb* s = reinterpret_cast<const QRgb*>(selMask_.constScanLine(y));
        int x = 0;
        while (x < w) {
            while (x < w && qAlpha(s[x]) < 128) ++x;
            const int a = x;
            while (x < w && qAlpha(s[x]) >= 128) ++x;
            if (x > a) rgn += QRect(a, y, x - a, 1);
        }
    }
    p.setClipRegion(rgn);
}

void CanvasWidget::setSelectionMask(const QImage& mask) {
    if (mask.isNull()) {
        selValid_ = false;
        selMask_ = QImage();
        update();
        emit selectionChanged();
        return;
    }
    selMask_ = mask.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    selFeathered_ = false;
    // 경계 상자 계산
    int x0 = selMask_.width(), y0 = selMask_.height(), x1 = -1, y1 = -1;
    for (int y = 0; y < selMask_.height(); ++y) {
        const QRgb* s = reinterpret_cast<const QRgb*>(selMask_.constScanLine(y));
        for (int x = 0; x < selMask_.width(); ++x)
            if (qAlpha(s[x]) >= 128) {
                x0 = std::min(x0, x); x1 = std::max(x1, x);
                y0 = std::min(y0, y); y1 = std::max(y1, y);
            }
    }
    if (x1 < 0) {
        selValid_ = false;
        selMask_ = QImage();
    } else {
        selRect_ = QRect(QPoint(x0, y0), QPoint(x1, y1));
        selValid_ = true;
    }
    update();
    emit selectionChanged();
}

void CanvasWidget::setSelectionRect(const QRect& r) {
    const QRect c = r.intersected(QRect(QPoint(0, 0), doc_->size()));
    selMask_ = QImage();
    selFeathered_ = false;
    selRect_ = c;
    selValid_ = c.width() > 1 && c.height() > 1;
    update();
    emit selectionChanged();
}

// 선택 영역을 n 픽셀 키우거나(양수) 줄인다(음수)
void CanvasWidget::growSelection(int px) {
    if (!selValid_ || px == 0) return;
    QImage m = selMask_;
    if (m.isNull()) {
        m = QImage(doc_->size(), QImage::Format_ARGB32_Premultiplied);
        m.fill(Qt::transparent);
        QPainter p(&m);
        p.fillRect(selRect_, Qt::white);
    }
    const bool grow = px > 0;
    const int r = std::min(std::abs(px), 200);
    QImage src = grow ? m : Effects::invert(m);
    if (!grow) {   // 줄이기는 "바깥을 키우기"와 같다. 반전 후 알파만 남긴다.
        QImage inv(m.size(), QImage::Format_ARGB32_Premultiplied);
        inv.fill(Qt::white);
        QPainter ip(&inv);
        ip.setCompositionMode(QPainter::CompositionMode_DestinationOut);
        ip.drawImage(0, 0, m);
        ip.end();
        src = inv;
    }
    // 원 모양으로 퍼뜨린다 (여러 방향으로 겹쳐 그리기)
    QImage out(m.size(), QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter p(&out);
    const int steps = std::clamp(r * 2, 8, 64);
    for (int i = 0; i < steps; ++i) {
        const double a = i * 2 * M_PI / steps;
        p.drawImage(QPointF(std::cos(a) * r, std::sin(a) * r), src);
    }
    p.drawImage(0, 0, src);
    p.end();
    if (!grow) {   // 다시 뒤집어 안쪽만 남긴다
        QImage back(m.size(), QImage::Format_ARGB32_Premultiplied);
        back.fill(Qt::white);
        QPainter bp(&back);
        bp.setCompositionMode(QPainter::CompositionMode_DestinationOut);
        bp.drawImage(0, 0, out);
        bp.end();
        out = back;
    }
    setSelectionMask(out);
}

// 선택 영역의 가장자리를 부드럽게 (반투명 띠를 만든다)
void CanvasWidget::featherSelection(int px) {
    if (!selValid_ || px <= 0) return;
    QImage m = selMask_;
    if (m.isNull()) {
        m = QImage(doc_->size(), QImage::Format_ARGB32_Premultiplied);
        m.fill(Qt::transparent);
        QPainter p(&m);
        p.fillRect(selRect_, Qt::white);
    }
    selMask_ = Effects::gaussBlur(m, std::clamp(px, 1, 100));
    selFeathered_ = true;
    update();
    emit selectionChanged();
}

void CanvasWidget::invertSelection() {
    if (!selValid_) {
        selectAll();
        return;
    }
    QImage inv(doc_->size(), QImage::Format_ARGB32_Premultiplied);
    inv.fill(Qt::white);
    QPainter p(&inv);
    p.setCompositionMode(QPainter::CompositionMode_Clear);
    if (selMask_.isNull()) p.fillRect(selRect_, Qt::black);
    else {
        p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
        p.drawImage(0, 0, selMask_);
    }
    p.end();
    setSelectionMask(inv);
}

// 올가미·다각형이 끝났을 때: 경로를 마스크로 바꾼다. Shift = 더하기, Alt = 빼기.
void CanvasWidget::setSelectionFromPath(const QPainterPath& path, Qt::KeyboardModifiers mods) {
    QImage m(doc_->size(), QImage::Format_ARGB32_Premultiplied);
    m.fill(Qt::transparent);
    {
        QPainter p(&m);
        p.setRenderHint(QPainter::Antialiasing, false);
        p.fillPath(path, Qt::white);
    }
    const bool add = mods & Qt::ShiftModifier, sub = mods & Qt::AltModifier;
    if ((add || sub) && selValid_) {
        QImage cur = selMask_;
        if (cur.isNull()) {
            cur = QImage(doc_->size(), QImage::Format_ARGB32_Premultiplied);
            cur.fill(Qt::transparent);
            QPainter cp(&cur);
            cp.fillRect(selRect_, Qt::white);
        }
        QPainter cp(&cur);
        cp.setCompositionMode(sub ? QPainter::CompositionMode_DestinationOut : QPainter::CompositionMode_SourceOver);
        cp.drawImage(0, 0, m);
        cp.end();
        setSelectionMask(cur);
        return;
    }
    setSelectionMask(m);
}

// 마술봉: 비슷한 색이 이어지는 영역을 선택한다
void CanvasWidget::wandSelect(const QPoint& seed, Qt::KeyboardModifiers mods) {
    const QSize sz = doc_->size();
    if (!QRect(QPoint(0, 0), sz).contains(seed)) return;
    QImage src;
    if (wandAll_) src = doc_->composite(doc_->currentFrame(), false, false);
    else {
        const int l = doc_->currentLayer();
        const int k = doc_->resolveKey(l, doc_->currentFrame());
        if (k < 0) {
            emit message(QStringLiteral("현재 프레임에 이미지가 없습니다."));
            return;
        }
        src = doc_->keyImage(l, k);
    }
    src = src.convertToFormat(QImage::Format_ARGB32);
    const int w = src.width(), h = src.height();
    const QRgb target = src.pixel(seed);
    const int tol = wandTol_;
    auto similar = [&](QRgb c) {
        if (qAlpha(c) == 0 && qAlpha(target) == 0) return true;
        return std::abs(qAlpha(c) - qAlpha(target)) <= tol && std::abs(qRed(c) - qRed(target)) <= tol &&
               std::abs(qGreen(c) - qGreen(target)) <= tol && std::abs(qBlue(c) - qBlue(target)) <= tol;
    };
    QImage m(sz, QImage::Format_ARGB32_Premultiplied);
    m.fill(Qt::transparent);
    QVector<uchar> seen(w * h, 0);
    QVector<QPoint> stack{seed};
    seen[seed.y() * w + seed.x()] = 1;
    while (!stack.isEmpty()) {
        const QPoint c = stack.takeLast();
        reinterpret_cast<QRgb*>(m.scanLine(c.y()))[c.x()] = qRgba(255, 255, 255, 255);
        const QPoint nb[4] = {{c.x() + 1, c.y()}, {c.x() - 1, c.y()}, {c.x(), c.y() + 1}, {c.x(), c.y() - 1}};
        for (const QPoint& n : nb) {
            if (n.x() < 0 || n.y() < 0 || n.x() >= w || n.y() >= h) continue;
            uchar& s = seen[n.y() * w + n.x()];
            if (s) continue;
            s = 1;
            if (similar(reinterpret_cast<const QRgb*>(src.constScanLine(n.y()))[n.x()])) stack.push_back(n);
        }
    }
    const bool add = mods & Qt::ShiftModifier, sub = mods & Qt::AltModifier;
    if ((add || sub) && selValid_) {
        QImage cur = selMask_;
        if (cur.isNull()) {
            cur = QImage(sz, QImage::Format_ARGB32_Premultiplied);
            cur.fill(Qt::transparent);
            QPainter cp(&cur);
            cp.fillRect(selRect_, Qt::white);
        }
        QPainter cp(&cur);
        cp.setCompositionMode(sub ? QPainter::CompositionMode_DestinationOut : QPainter::CompositionMode_SourceOver);
        cp.drawImage(0, 0, m);
        cp.end();
        setSelectionMask(cur);
        return;
    }
    setSelectionMask(m);
}

// ---------------- 벡터 획 고르기 ----------------
void CanvasWidget::setSelectedVectorStroke(int i) {
    const QVector<VecStroke>* list = doc_->vectorStrokes(doc_->currentLayer(), doc_->currentFrame());
    vecSel_ = (list && i >= 0 && i < list->size()) ? i : -1;
    update();
    emit vectorStrokeSelected(vecSel_);
}

int CanvasWidget::vecHitPoint(const QPointF& ip, int* strokeOut) const {
    const QVector<VecStroke>* list = doc_->vectorStrokes(doc_->currentLayer(), doc_->currentFrame());
    if (!list) return -1;
    const double r = 8.0 / zoom_;
    for (int s = 0; s < list->size(); ++s)
        for (int k = 0; k < (*list)[s].pts.size(); ++k)
            if (QLineF((*list)[s].pts[k], ip).length() < r) {
                if (strokeOut) *strokeOut = s;
                return k;
            }
    return -1;
}

// 선분 위를 눌렀는지. segOut 에는 몇 번째 점 뒤인지, atOut 에는 선 위의 가장 가까운 지점
int CanvasWidget::vecHitStroke(const QPointF& ip, int* segOut, QPointF* atOut) const {
    const QVector<VecStroke>* list = doc_->vectorStrokes(doc_->currentLayer(), doc_->currentFrame());
    if (!list) return -1;
    double best = 1e18;
    int bestS = -1, bestSeg = -1;
    QPointF bestAt;
    for (int s = 0; s < list->size(); ++s) {
        const VecStroke& st = (*list)[s];
        const double tol = std::max(6.0 / zoom_, st.width * 0.6 + 3.0 / zoom_);
        for (int k = 0; k + 1 < st.pts.size(); ++k) {
            const QPointF a = st.pts[k], b = st.pts[k + 1];
            const QPointF ab = b - a;
            const double len2 = ab.x() * ab.x() + ab.y() * ab.y();
            double t = len2 < 1e-9 ? 0.0 : ((ip.x() - a.x()) * ab.x() + (ip.y() - a.y()) * ab.y()) / len2;
            t = std::clamp(t, 0.0, 1.0);
            const QPointF c = a + ab * t;
            const double d = QLineF(c, ip).length();
            if (d < tol && d < best) {
                best = d;
                bestS = s;
                bestSeg = k;
                bestAt = c;
            }
        }
    }
    if (bestS >= 0) {
        if (segOut) *segOut = bestSeg;
        if (atOut) *atOut = bestAt;
    }
    return bestS;
}

// 색 추출(스포이트): 보이는 그림에서 색을 집는다
void CanvasWidget::pickColorAt(const QPointF& ip) {
    const QPoint p = ip.toPoint();
    if (!QRect(QPoint(0, 0), doc_->size()).contains(p)) return;
    const QImage img = doc_->composite(doc_->currentFrame(), false, false);
    if (img.isNull()) return;
    const QColor c = QColor::fromRgba(img.pixel(p));
    if (c.alpha() == 0) {
        emit message(QStringLiteral("투명한 곳입니다 (색을 집지 않았습니다)."));
        return;
    }
    QColor out = c;
    out.setAlpha(255);
    colorA_ = out;
    emit colorPicked(out);
    emit message(QString("색 추출: %1").arg(out.name().toUpper()));
}

void CanvasWidget::clearSelectionArea() {
    if (!selValid_) return;
    const int l = doc_->currentLayer();
    const int k = doc_->keyIndexAt(l, doc_->currentFrame());
    if (k < 0 || doc_->layer(l).locked) return;
    if (!beginEdit()) return;
    QImage* img = doc_->editableImage(editLayer_, doc_->currentFrame());
    {
        QPainter p(img);
        if (selMask_.isNull()) {
            p.setCompositionMode(QPainter::CompositionMode_Clear);
            p.fillRect(selRect_, Qt::black);
        } else {
            // 마스크의 투명도 그대로 지운다 (페더를 준 가장자리는 반쯤 지워진다)
            p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
            p.drawImage(0, 0, selMask_);
        }
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
    selMask_ = QImage();
    selValid_ = true;
    update();
    emit selectionChanged();
}

void CanvasWidget::clearSelection() {
    selValid_ = false;
    selMask_ = QImage();
    selFeathered_ = false;
    lassoPts_.clear();
    update();
    emit selectionChanged();
}

void CanvasWidget::wheelEvent(QWheelEvent* e) {
    const QPointF wp = e->position();
    const QPointF ip = toImage(wp);
    const qreal f = e->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
    const bool running = zoomAnim_ && zoomAnim_->state() == QAbstractAnimation::Running;
    zoomTarget_ = std::clamp((running ? zoomTarget_ : zoom_) * f, 0.05, 32.0);
    if (!zoomAnim_) {
        zoomAnim_ = new QVariantAnimation(this);
        zoomAnim_->setEasingCurve(QEasingCurve::OutCubic);
        zoomAnim_->setDuration(130);
        connect(zoomAnim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            // 커서 아래의 그림 지점이 움직이지 않도록 팬을 맞춘다
            const QPointF a = zoomAnchorWidget_, i = zoomAnchorImage_;
            zoom_ = v.toReal();
            const QSize s = doc_->size();
            const QPointF center((width() - s.width() * zoom_) / 2.0, (height() - s.height() * zoom_) / 2.0);
            pan_ = (a - i * zoom_) - center;
            emit zoomChanged(zoom_);
            update();
        });
    }
    zoomAnchorWidget_ = wp;
    zoomAnchorImage_ = ip;
    zoomAnim_->stop();
    zoomAnim_->setStartValue(zoom_);
    zoomAnim_->setEndValue(zoomTarget_);
    zoomAnim_->start();
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
