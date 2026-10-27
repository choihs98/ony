#include "TimelineWidget.h"
#include "LayerMenu.h"
#include <QContextMenuEvent>
#include <QMenu>
#include "Document.h"
#include "Style.h"

#include <QInputDialog>
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>

TimelineWidget::TimelineWidget(Document* doc, QWidget* parent) : QWidget(parent), doc_(doc) {
    setMouseTracking(false);
    auto refresh = [this] {
        updateGeometry();
        resize(sizeHint());
        update();
    };
    connect(doc_, &Document::structureChanged, this, refresh);
    connect(doc_, &Document::changed, this, [this] { update(); });
    connect(doc_, &Document::currentChanged, this, [this] { update(); });
    resize(sizeHint());
}

QSize TimelineWidget::sizeHint() const {
    return QSize(kNameW + doc_->frameCount() * cellW_ + 24, kHeader + doc_->layerCount() * rowH_ + 6);
}

void TimelineWidget::setCellWidth(int w) {
    w = std::clamp(w, 8, 96);
    if (w == cellW_) return;
    cellW_ = w;
    resize(sizeHint());
    update();
    emit zoomChanged();
}

void TimelineWidget::setCompact(bool on) {
    if (on == compact_) return;
    compact_ = on;
    rowH_ = on ? kRowCompact : kRowH;
    resize(sizeHint());
    update();
    emit zoomChanged();
}

void TimelineWidget::wheelEvent(QWheelEvent* e) {
    if (e->modifiers() & Qt::ControlModifier) {
        setCellWidth(cellW_ + (e->angleDelta().y() > 0 ? 4 : -4));
        e->accept();
        return;
    }
    QWidget::wheelEvent(e);
}

QSize TimelineWidget::minimumSizeHint() const {
    return sizeHint();
}

int TimelineWidget::rowToLayer(int row) const {
    return doc_->layerCount() - 1 - row;
}

int TimelineWidget::layerToRow(int layer) const {
    return doc_->layerCount() - 1 - layer;
}

const QPixmap& TimelineWidget::thumb(const QImage& img, QSize box) {
    const qint64 key = img.cacheKey() ^ (qint64(box.width()) << 50) ^ (qint64(box.height()) << 40);
    auto it = thumbs_.find(key);
    if (it != thumbs_.end()) return it.value();
    if (thumbs_.size() > 600) thumbs_.clear();
    QImage t(box, QImage::Format_ARGB32_Premultiplied);
    t.fill(QColor(0xf4, 0xf4, 0xf4));
    QPainter p(&t);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const QImage s = img.scaled(box, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    p.drawImage((box.width() - s.width()) / 2, (box.height() - s.height()) / 2, s);
    p.end();
    return thumbs_.insert(key, QPixmap::fromImage(t)).value();
}

// 비헤이비어 네 가지를 작은 그림으로 그린다: 없음 / 루프 / 핑퐁 / 유지
static void drawBehaviorGlyph(QPainter& p, const QRect& r, int mode, const QColor& c) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(c, 1.4));
    p.setBrush(Qt::NoBrush);
    const QPointF ctr = r.center();
    const double w = r.width() * 0.34, h = r.height() * 0.28;
    switch (mode) {
    case 1: { // 루프: 둥근 화살표
        QRectF a(ctr.x() - w, ctr.y() - w, w * 2, w * 2);
        p.drawArc(a, 40 * 16, 290 * 16);
        const QPointF t(ctr.x() + w * 0.72, ctr.y() - w * 0.72);
        p.drawLine(t, t + QPointF(-w * 0.1, -w * 0.75));
        p.drawLine(t, t + QPointF(w * 0.75, -w * 0.1));
        break;
    }
    case 2: { // 핑퐁: 양쪽 화살표
        p.drawLine(QPointF(ctr.x() - w, ctr.y()), QPointF(ctr.x() + w, ctr.y()));
        p.drawLine(QPointF(ctr.x() - w, ctr.y()), QPointF(ctr.x() - w + h, ctr.y() - h));
        p.drawLine(QPointF(ctr.x() - w, ctr.y()), QPointF(ctr.x() - w + h, ctr.y() + h));
        p.drawLine(QPointF(ctr.x() + w, ctr.y()), QPointF(ctr.x() + w - h, ctr.y() - h));
        p.drawLine(QPointF(ctr.x() + w, ctr.y()), QPointF(ctr.x() + w - h, ctr.y() + h));
        break;
    }
    case 3: { // 유지: 세로 막대 + 오른쪽 화살표
        p.drawLine(QPointF(ctr.x() - w, ctr.y() - h * 1.5), QPointF(ctr.x() - w, ctr.y() + h * 1.5));
        p.drawLine(QPointF(ctr.x() - w * 0.55, ctr.y()), QPointF(ctr.x() + w, ctr.y()));
        p.drawLine(QPointF(ctr.x() + w, ctr.y()), QPointF(ctr.x() + w - h, ctr.y() - h));
        p.drawLine(QPointF(ctr.x() + w, ctr.y()), QPointF(ctr.x() + w - h, ctr.y() + h));
        break;
    }
    default: { // 없음: 점선
        p.drawLine(QPointF(ctr.x() - w, ctr.y()), QPointF(ctr.x() - w * 0.35, ctr.y()));
        p.drawLine(QPointF(ctr.x() + w * 0.35, ctr.y()), QPointF(ctr.x() + w, ctr.y()));
        break;
    }
    }
    p.restore();
}

QRect TimelineWidget::behaviorButton(int row, int which) const {
    if (row < 0 || row >= doc_->layerCount()) return QRect();
    const int l = rowToLayer(row);
    const int first = doc_->firstKey(l);
    if (first < 0) return QRect();
    const int f = (which == 0) ? first - 1 : doc_->contentEnd(l);
    if (f < 0 || f >= doc_->frameCount()) return QRect();
    const int side = std::min(18, std::min(cellW_ - 2, rowH_ - 4));
    if (side < 8) return QRect();
    const int y = kHeader + row * rowH_;
    return QRect(kNameW + f * cellW_ + (cellW_ - side) / 2, y + (rowH_ - side) / 2, side, side);
}

void TimelineWidget::paintEvent(QPaintEvent* ev) {
    QPainter p(this);
    const QPalette pal = palette();
    const bool dark = pal.color(QPalette::Window).lightness() < 128;
    const QColor bg = pal.color(QPalette::Window);
    const QColor hdrBg = dark ? bg.darker(118) : bg.darker(106);
    const QColor rowA = dark ? bg.lighter(112) : bg.lighter(104);
    const QColor rowB = bg;
    const QColor selRow = dark ? QColor(0x52, 0x5a, 0x5c) : QColor(0xc4, 0xd4, 0xe4);
    const QColor grid = dark ? bg.darker(130) : bg.darker(115);
    const QColor text = pal.color(QPalette::WindowText);
    const QColor muted = text.darker(130);
    const QColor accent = pal.color(QPalette::Highlight);
    p.fillRect(rect(), bg);

    const int frames = doc_->frameCount();
    const int curF = doc_->currentFrame();
    const int curL = doc_->currentLayer();
    const QRect clip = ev->rect();
    const int f0 = std::clamp((clip.left() - kNameW) / cellW_ - 1, 0, std::max(0, frames - 1));
    const int f1 = std::clamp((clip.right() - kNameW) / cellW_ + 1, 0, std::max(0, frames - 1));

    // 눈금(프레임 번호)
    p.fillRect(QRect(0, 0, width(), kHeader), hdrBg);
    p.setPen(text);
    p.drawText(QRect(8, 0, kNameW - 10, kHeader), Qt::AlignVCenter | Qt::AlignLeft,
               QString("%1 · 이미지 %2").arg(doc_->layer(curL).name).arg(curF + 1));
    QFont small = font();
    small.setPointSizeF(std::max(7.0, font().pointSizeF() - 1.0));
    p.setFont(small);
    const int labelStep = cellW_ >= 32 ? 2 : cellW_ >= 22 ? 4 : cellW_ >= 14 ? 5 : 10;
    for (int f = f0; f <= f1; ++f) {
        const int x = kNameW + f * cellW_;
        if (f == curF) p.fillRect(QRect(x, 0, cellW_, kHeader), accent);
        p.setPen(f == curF ? Qt::white : muted);
        if (f % labelStep == 0 || f == curF) p.drawText(QRect(x + 2, 0, cellW_, kHeader), Qt::AlignVCenter | Qt::AlignLeft, QString::number(f + 1));
        p.setPen(grid);
        p.drawLine(x, kHeader - 5, x, kHeader);
    }

    for (int row = 0; row < doc_->layerCount(); ++row) {
        const int l = rowToLayer(row);
        const Layer& L = doc_->layer(l);
        const int y = kHeader + row * rowH_;
        if (y > clip.bottom() || y + rowH_ < clip.top()) continue;
        const bool sel = (l == curL);
        p.fillRect(QRect(0, y, width(), rowH_), sel ? selRow : (row % 2 ? rowA : rowB));

        // ---- 레이어 머리글: 눈 / 잠금 / 썸네일 / 이름 / 불투명도
        const QColor on = text, off = muted.darker(150);
        const QIcon eye = Style::icon("eye", 16, L.visible ? on : off);
        if (compact_) eye.paint(&p, QRect(2, y + 5, 14, 14));
        else eye.paint(&p, QRect(4, y + 6, 16, 16));
        const QIcon lock = Style::icon("lock", 16, L.locked ? QColor(0xe2, 0x6a, 0x4a) : off);
        if (compact_) lock.paint(&p, QRect(18, y + 5, 14, 14));
        else lock.paint(&p, QRect(4, y + 28, 16, 16));
        // 현재 프레임의 키 이미지
        const int kf = doc_->keyIndexAt(l, curF);
        const QRect tr(26, y + 5, 52, rowH_ - 10);
        if (!compact_) {
            if (kf >= 0) p.drawPixmap(tr.topLeft(), thumb(L.frames[kf], tr.size()));
            else p.fillRect(tr, QColor(0xf4, 0xf4, 0xf4, 40));
            p.setPen(QPen(grid, 1));
            p.setBrush(Qt::NoBrush);
            p.drawRect(tr);
        }
        // 이름 + 보조 정보
        QString label = L.name;
        if (L.folder >= 0 && L.folder < doc_->folderCount()) label = QString("[%1] %2").arg(doc_->folder(L.folder).name, label);
        p.setPen(L.ctgLines ? QColor(0xff, 0xcc, 0x66) : text);
        p.setFont(font());
        if (compact_) {
            p.drawText(QRect(38, y, kNameW - 42, rowH_), Qt::AlignVCenter | Qt::AlignLeft, label);
        } else {
            p.drawText(QRect(86, y + 6, kNameW - 90, 18), Qt::AlignVCenter | Qt::AlignLeft, label);
            p.setFont(small);
            p.setPen(muted);
            QString sub = QString("색상  %1%").arg(int(L.opacity * 100 + 0.5));
            if (!L.fx.isEmpty()) sub += QString("  FX %1").arg(L.fx.size());
            if (L.ctgLines) sub += QStringLiteral("  CTG");
            p.drawText(QRect(86, y + 28, kNameW - 90, 18), Qt::AlignVCenter | Qt::AlignLeft, sub);
        }

        // ---- 프레임 칸
        int key = doc_->keyIndexAt(l, f0);
        const int cFirst = doc_->firstKey(l);
        const int cEnd = doc_->contentEnd(l);
        for (int f = f0; f <= f1; ++f) {
            if (!L.frames[f].isNull()) key = f;
            const QRect c(kNameW + f * cellW_, y, cellW_, rowH_);
            // 레이어 내용 밖: 비헤이비어가 채우는 구간만 옅은 띠로 보여 준다
            if (cFirst >= 0 && (f < cFirst || f >= cEnd)) {
                const int mode = f < cFirst ? L.preBehavior : L.postBehavior;
                if (mode != 0) {
                    const QRect tb = c.adjusted(0, 4, 0, -4);
                    p.fillRect(tb, dark ? QColor(0x40, 0x44, 0x46, 110) : QColor(0xb8, 0xbc, 0xc2, 110));
                }
                p.setPen(grid);
                p.drawLine(c.right(), y, c.right(), y + rowH_);
                continue;
            }
            if (key >= 0) {
                if (key == f) {
                    const QRect tb = c.adjusted(2, 4, -2, -4);
                    p.drawPixmap(tb.topLeft(), thumb(L.frames[f], tb.size()));
                    p.setPen(QPen(QColor(0x20, 0x20, 0x20), 1));
                    p.setBrush(Qt::NoBrush);
                    p.drawRect(tb);
                    const int mk = L.imageMarks.value(f, -1);
                    if (mk >= 0 && mk < doc_->markColors().size()) {
                        p.fillRect(QRect(tb.left(), tb.top(), 9, 9), doc_->markColors()[mk].color);
                    }
                    const QString inst = L.instanceNames.value(f);
                    if (!inst.isEmpty()) {
                        p.setPen(QColor(0xff, 0xff, 0xff));
                        p.setFont(small);
                        p.drawText(QRect(tb.left() + 2, tb.bottom() - 13, tb.width() - 2, 12), Qt::AlignLeft | Qt::AlignVCenter, inst);
                    }
                } else {
                    const QRect tb = c.adjusted(0, 4, 0, -4);
                    p.fillRect(tb, dark ? QColor(0x40, 0x44, 0x46) : QColor(0xb8, 0xbc, 0xc2));
                    p.setPen(grid);
                    p.drawLine(tb.left() + 6, c.center().y(), tb.right() - 6, c.center().y());
                }
            }
            p.setPen(grid);
            p.drawLine(c.right(), y, c.right(), y + rowH_);
        }
        // ---- 프리/포스트 비헤이비어 단추 (TVPaint 처럼 내용의 앞뒤 칸에)
        for (int which = 0; which < 2; ++which) {
            const QRect br = behaviorButton(row, which);
            if (br.isNull() || br.right() < clip.left() - 4 || br.left() > clip.right() + 4) continue;
            const int mode = which == 0 ? L.preBehavior : L.postBehavior;
            p.setPen(QPen(grid, 1));
            p.setBrush(dark ? bg.lighter(128) : bg.darker(108));
            p.drawRoundedRect(br, 3, 3);
            p.setBrush(Qt::NoBrush);
            drawBehaviorGlyph(p, br, mode, mode == 0 ? muted : text);
        }
        p.setPen(grid);
        p.drawLine(0, y + rowH_, width(), y + rowH_);
        // ---- 퍼펫 키 표시 (위쪽 가장자리의 작은 마름모)
        if (L.puppet) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0xff, 0xaa, 0x33));
            for (auto it = L.pkeys.cbegin(); it != L.pkeys.cend(); ++it) {
                const int cx = kNameW + it.key() * cellW_ + cellW_ / 2;
                if (cx < clip.left() - 20 || cx > clip.right() + 20) continue;
                const QPoint dia[4] = {QPoint(cx, y + 1), QPoint(cx + 5, y + 6), QPoint(cx, y + 11), QPoint(cx - 5, y + 6)};
                p.drawPolygon(dia, 4);
            }
        }

    }

    // 루프 구간 (Shift+헤더 드래그)
    if (rangeA_ >= 0) {
        const QRect r(kNameW + rangeA_ * cellW_, 0, (rangeB_ - rangeA_ + 1) * cellW_, height());
        p.fillRect(QRect(r.x(), 0, r.width(), kHeader), QColor(0xe2, 0xa8, 0x4a, 160));
        p.fillRect(QRect(r.x(), kHeader, r.width(), height() - kHeader), QColor(0xe2, 0xa8, 0x4a, 30));
    }

    // 북마크 (헤더 위 작은 삼각형)
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x4a, 0xa8, 0xe2));
    for (int b : doc_->bookmarks()) {
        const int bx = kNameW + b * cellW_;
        const QPoint tri[3] = {QPoint(bx + 2, 1), QPoint(bx + cellW_ - 2, 1), QPoint(bx + cellW_ / 2, 9)};
        p.drawPolygon(tri, 3);
    }
    p.setBrush(Qt::NoBrush);

    // 재생 헤드
    const int px = kNameW + curF * cellW_;
    p.fillRect(QRect(px, kHeader, cellW_, height() - kHeader), QColor(accent.red(), accent.green(), accent.blue(), 45));
    p.setPen(accent);
    p.drawLine(px, kHeader, px, height());
    p.drawLine(px + cellW_, kHeader, px + cellW_, height());
    p.setPen(grid);
    p.drawLine(kNameW, 0, kNameW, height());
}

bool TimelineWidget::behaviorMenuAt(const QPoint& pos) {
    if (pos.y() < kHeader) return false;
    const int row = (pos.y() - kHeader) / rowH_;
    if (row < 0 || row >= doc_->layerCount()) return false;
    for (int which = 0; which < 2; ++which) {
        const QRect br = behaviorButton(row, which);
        if (br.isNull() || !br.contains(pos)) continue;
        const int l = rowToLayer(row);
        doc_->setCurrentLayer(l);
        static const char* names[] = {"없음", "루프", "핑퐁", "유지"};
        QMenu m(this);
        m.setTitle(which == 0 ? QStringLiteral("첫 이미지 앞") : QStringLiteral("마지막 이미지 뒤"));
        QAction* hdr = m.addAction(m.title());
        hdr->setEnabled(false);
        m.addSeparator();
        const int cur = which == 0 ? doc_->layer(l).preBehavior : doc_->layer(l).postBehavior;
        QVector<QAction*> acts;
        for (int i = 0; i < 4; ++i) {
            QAction* a = m.addAction(QString::fromUtf8(names[i]));
            a->setCheckable(true);
            a->setChecked(i == cur);
            acts.push_back(a);
        }
        QAction* chosen = m.exec(mapToGlobal(pos));
        const int idx = acts.indexOf(chosen);
        if (idx >= 0 && idx != cur) {
            (which == 0 ? doc_->layer(l).preBehavior : doc_->layer(l).postBehavior) = idx;
            doc_->notifyStructure();
        }
        return true;
    }
    return false;
}

void TimelineWidget::pick(const QPoint& pos, bool pressed) {
    const int frames = doc_->frameCount();
    if (pos.x() >= kNameW) {
        const int f = std::clamp((pos.x() - kNameW) / cellW_, 0, frames - 1);
        if (pos.y() >= kHeader) {
            const int row = (pos.y() - kHeader) / rowH_;
            if (row >= 0 && row < doc_->layerCount()) doc_->setCurrentLayer(rowToLayer(row));
        }
        doc_->setCurrentFrame(f);
        return;
    }
    if (!pressed || pos.y() < kHeader) return;
    const int row = (pos.y() - kHeader) / rowH_;
    if (row < 0 || row >= doc_->layerCount()) return;
    const int l = rowToLayer(row);
    const int ry = (pos.y() - kHeader) % rowH_;
    if (pos.x() < (compact_ ? 34 : 24)) {
        const bool eyeHit = compact_ ? pos.x() < 17 : ry < rowH_ / 2;
        if (eyeHit) doc_->layer(l).visible = !doc_->layer(l).visible;
        else doc_->layer(l).locked = !doc_->layer(l).locked;
        doc_->notifyStructure();
    } else {
        doc_->setCurrentLayer(l);
    }
}

void TimelineWidget::clearRange() {
    rangeA_ = rangeB_ = -1;
    update();
    emit rangeChanged(-1, -1);
}

void TimelineWidget::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    if (e->pos().y() < kHeader && e->pos().x() >= kNameW && (e->modifiers() & Qt::ShiftModifier)) {
        anchor_ = std::clamp((e->pos().x() - kNameW) / cellW_, 0, doc_->frameCount() - 1);
        rangeA_ = rangeB_ = anchor_;
        rangeDrag_ = true;
        update();
        return;
    }
    if (behaviorMenuAt(e->pos())) return;
    pick(e->pos(), true);
}

void TimelineWidget::mouseMoveEvent(QMouseEvent* e) {
    if (rangeDrag_) {
        const int f = std::clamp((e->pos().x() - kNameW) / cellW_, 0, doc_->frameCount() - 1);
        rangeA_ = std::min(anchor_, f);
        rangeB_ = std::max(anchor_, f);
        update();
        return;
    }
    if (e->buttons() & Qt::LeftButton) pick(e->pos(), false);
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent*) {
    if (!rangeDrag_) return;
    rangeDrag_ = false;
    emit rangeChanged(rangeA_, rangeB_);
}

void TimelineWidget::contextMenuEvent(QContextMenuEvent* e) {
    if (e->pos().y() < kHeader) return;
    const int row = (e->pos().y() - kHeader) / rowH_;
    if (row < 0 || row >= doc_->layerCount()) return;
    const int l = rowToLayer(row);
    doc_->setCurrentLayer(l);
    QMenu menu(this);
    menu.addAction(QStringLiteral("프레임 추가 (현재 프레임 뒤)"), this, [this] { doc_->insertFrame(doc_->currentFrame() + 1); });
    menu.addAction(QStringLiteral("프레임 삽입 (현재 프레임 앞)"), this, [this] { doc_->insertFrame(doc_->currentFrame()); });
    menu.addAction(QStringLiteral("프레임 삭제"), this, [this] { doc_->deleteFrame(doc_->currentFrame()); });
    menu.addSeparator();
    LayerMenu::populate(&menu, doc_, [l] { return l; }, [this] { return doc_->currentFrame(); });
    menu.exec(e->globalPos());
}

void TimelineWidget::mouseDoubleClickEvent(QMouseEvent* e) {
    // 레이어 이름 칸 더블클릭 → 이름 변경
    if (e->pos().x() >= 80 && e->pos().x() < kNameW && e->pos().y() >= kHeader) {
        const int row = (e->pos().y() - kHeader) / rowH_;
        if (row < 0 || row >= doc_->layerCount()) return;
        const int l = rowToLayer(row);
        bool ok = false;
        const QString n = QInputDialog::getText(this, QStringLiteral("레이어 이름"), QStringLiteral("이름:"), QLineEdit::Normal, doc_->layer(l).name, &ok);
        if (ok && !n.isEmpty()) {
            doc_->layer(l).name = n;
            doc_->notifyStructure();
        }
    }
}

// ---------------------------------------------------------------- ScrubBar
ScrubBar::ScrubBar(Document* doc, QWidget* parent) : QWidget(parent), doc_(doc) {
    setFixedHeight(22);
    connect(doc_, &Document::structureChanged, this, [this] { update(); });
    connect(doc_, &Document::currentChanged, this, [this] { update(); });
}

int ScrubBar::frameAt(int x) const {
    const int n = doc_->frameCount();
    const double cw = double(width() - 8) / n;
    return std::clamp(int((x - 4) / cw), 0, n - 1);
}

void ScrubBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const QPalette pal = palette();
    const bool dark = pal.color(QPalette::Window).lightness() < 128;
    p.fillRect(rect(), dark ? pal.color(QPalette::Window).darker(125) : pal.color(QPalette::Window).darker(105));
    const int n = doc_->frameCount();
    const double cw = double(width() - 8) / n;
    const QColor accent = pal.color(QPalette::Highlight);
    if (rangeA_ >= 0) p.fillRect(QRectF(4 + rangeA_ * cw, 0, (rangeB_ - rangeA_ + 1) * cw, height()), QColor(0xe2, 0xa8, 0x4a, 110));
    const int cur = doc_->currentFrame();
    p.fillRect(QRectF(4 + cur * cw, 0, cw, height()), accent);
    QFont small = font();
    small.setPointSizeF(std::max(7.0, font().pointSizeF() - 1.5));
    p.setFont(small);
    const int step = cw >= 30 ? 1 : (cw >= 14 ? 2 : (cw >= 7 ? 5 : 10));
    for (int f = 0; f < n; ++f) {
        const double x = 4 + f * cw;
        const bool major = (f % step == 0);
        p.setPen(f == cur ? Qt::white : pal.color(QPalette::WindowText).darker(major ? 120 : 170));
        p.drawLine(QPointF(x, height() - (major ? 7 : 4)), QPointF(x, height()));
        if (major) p.drawText(QRectF(x + 2, 0, 40, height() - 4), Qt::AlignVCenter | Qt::AlignLeft, QString::number(f + 1));
    }
}

void ScrubBar::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) doc_->setCurrentFrame(frameAt(e->pos().x()));
}

void ScrubBar::mouseMoveEvent(QMouseEvent* e) {
    if (e->buttons() & Qt::LeftButton) doc_->setCurrentFrame(frameAt(e->pos().x()));
}


// ---------------------------------------------------------------- WaveformBar
WaveformBar::WaveformBar(Document* doc, QWidget* parent) : QWidget(parent), doc_(doc) {
    setFixedHeight(34);
    setToolTip(QStringLiteral("소리 파형 — 클릭하면 그 위치로 재생 헤드가 갑니다"));
    connect(doc_, &Document::currentChanged, this, [this] { update(); });
    connect(doc_, &Document::structureChanged, this, [this] { update(); });
}

void WaveformBar::setPeaks(const QVector<float>& peaks, const QString& label) {
    peaks_ = peaks;
    label_ = label;
    update();
}

void WaveformBar::clearPeaks() {
    peaks_.clear();
    label_.clear();
    update();
}

void WaveformBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const QPalette pal = palette();
    const bool dark = pal.color(QPalette::Window).lightness() < 128;
    p.fillRect(rect(), dark ? pal.color(QPalette::Window).darker(130) : pal.color(QPalette::Window).darker(108));
    const QColor accent = pal.color(QPalette::Highlight);
    if (peaks_.isEmpty()) {
        p.setPen(pal.color(QPalette::WindowText).darker(140));
        p.drawText(rect().adjusted(6, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft,
                   QStringLiteral("소리 파형: 소리 파일을 불러오면 여기에 보입니다"));
        return;
    }
    const int w = width(), h = height();
    const int mid = h / 2;
    p.setPen(QPen(QColor(0x5c, 0xc8, 0xa8), 1));
    for (int x = 0; x < w; ++x) {
        const int i = int(qint64(x) * peaks_.size() / std::max(1, w));
        const float v = peaks_.value(std::clamp(i, 0, int(peaks_.size()) - 1));
        const int a = std::max(1, int(v * (h - 6) / 2));
        p.drawLine(x, mid - a, x, mid + a);
    }
    // 재생 헤드
    const int n = std::max(1, doc_->frameCount());
    const int px = int(qint64(doc_->currentFrame()) * w / n);
    p.setPen(QPen(accent, 2));
    p.drawLine(px, 0, px, h);
    if (!label_.isEmpty()) {
        p.setPen(pal.color(QPalette::WindowText).darker(130));
        QFont f = font();
        f.setPointSizeF(std::max(7.0, font().pointSizeF() - 1.5));
        p.setFont(f);
        p.drawText(rect().adjusted(6, 1, -6, 0), Qt::AlignTop | Qt::AlignLeft, label_);
    }
}

void WaveformBar::mousePressEvent(QMouseEvent* e) {
    const int n = std::max(1, doc_->frameCount());
    doc_->setCurrentFrame(std::clamp(int(qint64(e->pos().x()) * n / std::max(1, width())), 0, n - 1));
}
