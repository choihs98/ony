#include "TimelineWidget.h"
#include "Document.h"

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

void TimelineWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const QColor bg(0x25, 0x27, 0x2c), grid(0x35, 0x39, 0x44), text(0xea, 0xec, 0xef), muted(0x9a, 0xa0, 0xa6);
    p.fillRect(rect(), bg);

    const int frames = doc_->frameCount();
    const int curF = doc_->currentFrame();
    const int curL = doc_->currentLayer();

    // 헤더 왼쪽: 현재 레이어 이름과 이미지 번호
    p.setPen(text);
    p.drawText(QRect(6, 0, kNameW - 10, kHeader), Qt::AlignVCenter | Qt::AlignLeft,
               QString("%1 · 이미지 %2").arg(doc_->layer(curL).name).arg(curF + 1));
    // 헤더(프레임 번호)
    p.setPen(muted);
    QFont small = font();
    small.setPointSizeF(std::max(7.0, font().pointSizeF() - 1.5));
    p.setFont(small);
    for (int f = 0; f < frames; ++f) {
        const int x = kNameW + f * kCell;
        if (f % 5 == 0 || f == curF) {
            p.setPen(f == curF ? QColor(0x4a, 0x90, 0xe2) : muted);
            p.drawText(QRect(x - 6, 0, kCell + 12, kHeader), Qt::AlignCenter, QString::number(f + 1));
        }
    }

    for (int row = 0; row < doc_->layerCount(); ++row) {
        const int l = rowToLayer(row);
        const Layer& L = doc_->layer(l);
        const int y = kHeader + row * kRowH;
        const bool sel = (l == curL);
        p.fillRect(QRect(0, y, width(), kRowH), sel ? QColor(0x2f, 0x35, 0x44) : (row % 2 ? QColor(0x22, 0x24, 0x29) : bg));

        // 이름 열: 눈(보임) / 자물쇠(잠금) / 이름
        p.setPen(L.visible ? text : QColor(0x55, 0x58, 0x60));
        p.drawText(QRect(3, y, 16, kRowH), Qt::AlignCenter, L.visible ? QStringLiteral("눈") : QStringLiteral("－"));
        p.setPen(L.locked ? QColor(0xe2, 0x4a, 0x4a) : QColor(0x55, 0x58, 0x60));
        p.drawText(QRect(21, y, 16, kRowH), Qt::AlignCenter, L.locked ? QStringLiteral("잠") : QStringLiteral("·"));
        p.setPen(text);
        QString label = L.name;
        if (L.folder >= 0 && L.folder < doc_->folderCount()) label = QString("[%1] %2").arg(doc_->folder(L.folder).name, label);
        if (L.ctgLines) p.setPen(QColor(0xff, 0xcc, 0x66));
        if (!L.fx.isEmpty()) label += QStringLiteral(" ✦");
        p.drawText(QRect(42, y, kNameW - 46, kRowH), Qt::AlignVCenter | Qt::AlignLeft, label);

        int key = -1;
        for (int f = 0; f < frames; ++f) {
            if (!L.frames[f].isNull()) key = f;
            const QRect c(kNameW + f * kCell, y, kCell, kRowH);
            if (key >= 0) {
                if (key == f) {
                    p.fillRect(c.adjusted(1, 3, 0, -3), QColor(0x5b, 0x8b, 0xd6));
                    p.setPen(Qt::NoPen);
                    p.setBrush(QColor(0x1b, 0x2a, 0x4a));
                    p.drawEllipse(c.center(), 2, 2);
                } else {
                    p.fillRect(c.adjusted(0, 3, 0, -3), QColor(0x3d, 0x5a, 0x8a));
                }
            }
            p.setPen(f % 6 == 5 ? QColor(0x4a, 0x50, 0x60) : grid);
            p.drawLine(c.right(), y, c.right(), y + kRowH);
        }
        p.setPen(grid);
        p.drawLine(0, y + kRowH, width(), y + kRowH);
    }

    // 루프 구간 (Shift+헤더 드래그)
    if (rangeA_ >= 0) {
        const QRect r(kNameW + rangeA_ * kCell, 0, (rangeB_ - rangeA_ + 1) * kCell, height());
        p.fillRect(QRect(r.x(), 0, r.width(), kHeader), QColor(0xe2, 0xa8, 0x4a, 140));
        p.fillRect(QRect(r.x(), kHeader, r.width(), height() - kHeader), QColor(0xe2, 0xa8, 0x4a, 28));
    }

    // 재생 헤드
    const int px = kNameW + curF * kCell;
    p.fillRect(QRect(px, 0, kCell, height()), QColor(0x4a, 0x90, 0xe2, 55));
    p.setPen(QColor(0x4a, 0x90, 0xe2));
    p.drawLine(px, 0, px, height());
    p.drawLine(px + kCell, 0, px + kCell, height());
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
    if (pos.x() < 20) {
        doc_->layer(l).visible = !doc_->layer(l).visible;
        doc_->notifyStructure();
    } else if (pos.x() < 40) {
        doc_->layer(l).locked = !doc_->layer(l).locked;
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
