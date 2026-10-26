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
    return QSize(kNameW + doc_->frameCount() * kCell + 24, kHeader + doc_->layerCount() * kRowH + 6);
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
    const int f0 = std::clamp((clip.left() - kNameW) / kCell - 1, 0, std::max(0, frames - 1));
    const int f1 = std::clamp((clip.right() - kNameW) / kCell + 1, 0, std::max(0, frames - 1));

    // 눈금(프레임 번호)
    p.fillRect(QRect(0, 0, width(), kHeader), hdrBg);
    p.setPen(text);
    p.drawText(QRect(8, 0, kNameW - 10, kHeader), Qt::AlignVCenter | Qt::AlignLeft,
               QString("%1 · 이미지 %2").arg(doc_->layer(curL).name).arg(curF + 1));
    QFont small = font();
    small.setPointSizeF(std::max(7.0, font().pointSizeF() - 1.0));
    p.setFont(small);
    for (int f = f0; f <= f1; ++f) {
        const int x = kNameW + f * kCell;
        if (f == curF) p.fillRect(QRect(x, 0, kCell, kHeader), accent);
        p.setPen(f == curF ? Qt::white : muted);
        if (f % 2 == 0 || f == curF) p.drawText(QRect(x + 2, 0, kCell, kHeader), Qt::AlignVCenter | Qt::AlignLeft, QString::number(f + 1));
        p.setPen(grid);
        p.drawLine(x, kHeader - 5, x, kHeader);
    }

    for (int row = 0; row < doc_->layerCount(); ++row) {
        const int l = rowToLayer(row);
        const Layer& L = doc_->layer(l);
        const int y = kHeader + row * kRowH;
        if (y > clip.bottom() || y + kRowH < clip.top()) continue;
        const bool sel = (l == curL);
        p.fillRect(QRect(0, y, width(), kRowH), sel ? selRow : (row % 2 ? rowA : rowB));

        // ---- 레이어 머리글: 눈 / 잠금 / 썸네일 / 이름 / 불투명도
        const QColor on = text, off = muted.darker(150);
        const QIcon eye = Style::icon("eye", 16, L.visible ? on : off);
        eye.paint(&p, QRect(4, y + 6, 16, 16));
        const QIcon lock = Style::icon("lock", 16, L.locked ? QColor(0xe2, 0x6a, 0x4a) : off);
        lock.paint(&p, QRect(4, y + 28, 16, 16));
        // 현재 프레임의 키 이미지
        const int kf = doc_->keyIndexAt(l, curF);
        const QRect tr(26, y + 5, 52, kRowH - 10);
        if (kf >= 0) p.drawPixmap(tr.topLeft(), thumb(L.frames[kf], tr.size()));
        else p.fillRect(tr, QColor(0xf4, 0xf4, 0xf4, 40));
        p.setPen(QPen(grid, 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(tr);
        // 이름 + 보조 정보
        QString label = L.name;
        if (L.folder >= 0 && L.folder < doc_->folderCount()) label = QString("[%1] %2").arg(doc_->folder(L.folder).name, label);
        p.setPen(L.ctgLines ? QColor(0xff, 0xcc, 0x66) : text);
        p.setFont(font());
        p.drawText(QRect(86, y + 6, kNameW - 90, 18), Qt::AlignVCenter | Qt::AlignLeft, label);
        p.setFont(small);
        p.setPen(muted);
        QString sub = QString("색상  %1%").arg(int(L.opacity * 100 + 0.5));
        if (!L.fx.isEmpty()) sub += QString("  FX %1").arg(L.fx.size());
        if (L.ctgLines) sub += QStringLiteral("  CTG");
        p.drawText(QRect(86, y + 28, kNameW - 90, 18), Qt::AlignVCenter | Qt::AlignLeft, sub);

        // ---- 프레임 칸
        int key = doc_->keyIndexAt(l, f0);
        for (int f = f0; f <= f1; ++f) {
            if (!L.frames[f].isNull()) key = f;
            const QRect c(kNameW + f * kCell, y, kCell, kRowH);
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
            p.drawLine(c.right(), y, c.right(), y + kRowH);
        }
        p.setPen(grid);
        p.drawLine(0, y + kRowH, width(), y + kRowH);
    }

    // 루프 구간 (Shift+헤더 드래그)
    if (rangeA_ >= 0) {
        const QRect r(kNameW + rangeA_ * kCell, 0, (rangeB_ - rangeA_ + 1) * kCell, height());
        p.fillRect(QRect(r.x(), 0, r.width(), kHeader), QColor(0xe2, 0xa8, 0x4a, 160));
        p.fillRect(QRect(r.x(), kHeader, r.width(), height() - kHeader), QColor(0xe2, 0xa8, 0x4a, 30));
    }

    // 북마크 (헤더 위 작은 삼각형)
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x4a, 0xa8, 0xe2));
    for (int b : doc_->bookmarks()) {
        const int bx = kNameW + b * kCell;
        const QPoint tri[3] = {QPoint(bx + 2, 1), QPoint(bx + kCell - 2, 1), QPoint(bx + kCell / 2, 9)};
        p.drawPolygon(tri, 3);
    }
    p.setBrush(Qt::NoBrush);

    // 재생 헤드
    const int px = kNameW + curF * kCell;
    p.fillRect(QRect(px, kHeader, kCell, height() - kHeader), QColor(accent.red(), accent.green(), accent.blue(), 45));
    p.setPen(accent);
    p.drawLine(px, kHeader, px, height());
    p.drawLine(px + kCell, kHeader, px + kCell, height());
    p.setPen(grid);
    p.drawLine(kNameW, 0, kNameW, height());
}

void TimelineWidget::pick(const QPoint& pos, bool pressed) {
    const int frames = doc_->frameCount();
    if (pos.x() >= kNameW) {
        const int f = std::clamp((pos.x() - kNameW) / kCell, 0, frames - 1);
        if (pos.y() >= kHeader) {
            const int row = (pos.y() - kHeader) / kRowH;
            if (row >= 0 && row < doc_->layerCount()) doc_->setCurrentLayer(rowToLayer(row));
        }
        doc_->setCurrentFrame(f);
        return;
    }
    if (!pressed || pos.y() < kHeader) return;
    const int row = (pos.y() - kHeader) / kRowH;
    if (row < 0 || row >= doc_->layerCount()) return;
    const int l = rowToLayer(row);
    const int ry = (pos.y() - kHeader) % kRowH;
    if (pos.x() < 24) {
        if (ry < kRowH / 2) doc_->layer(l).visible = !doc_->layer(l).visible;
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
        anchor_ = std::clamp((e->pos().x() - kNameW) / kCell, 0, doc_->frameCount() - 1);
        rangeA_ = rangeB_ = anchor_;
        rangeDrag_ = true;
        update();
        return;
    }
    pick(e->pos(), true);
}

void TimelineWidget::mouseMoveEvent(QMouseEvent* e) {
    if (rangeDrag_) {
        const int f = std::clamp((e->pos().x() - kNameW) / kCell, 0, doc_->frameCount() - 1);
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
    const int row = (e->pos().y() - kHeader) / kRowH;
    if (row < 0 || row >= doc_->layerCount()) return;
    const int l = rowToLayer(row);
    doc_->setCurrentLayer(l);
    QMenu menu(this);
    LayerMenu::populate(&menu, doc_, [l] { return l; }, [this] { return doc_->currentFrame(); });
    menu.exec(e->globalPos());
}

void TimelineWidget::mouseDoubleClickEvent(QMouseEvent* e) {
    // 레이어 이름 칸 더블클릭 → 이름 변경
    if (e->pos().x() >= 80 && e->pos().x() < kNameW && e->pos().y() >= kHeader) {
        const int row = (e->pos().y() - kHeader) / kRowH;
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
