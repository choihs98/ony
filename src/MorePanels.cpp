#include <algorithm>
#include "MorePanels.h"
#include <QSignalBlocker>
#include <QPixmap>
#include <QToolButton>
#include <QScrollArea>
#include "CanvasWidget.h"
#include "Document.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

// ---------------------------------------------------------------- 도크 제목줄
DockTitleBar::DockTitleBar(QDockWidget* dock) : QWidget(dock), dock_(dock) {
    setFixedHeight(20);
    setCursor(Qt::ArrowCursor);
    connect(dock_, &QDockWidget::windowTitleChanged, this, [this] { update(); });
}

void DockTitleBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), palette().window());
    p.setPen(palette().color(QPalette::WindowText));
    p.drawText(QRect(6, 0, width() - 70, height()), Qt::AlignVCenter | Qt::AlignLeft, dock_->windowTitle());
    // 오른쪽 위: ● (잠금) ▭ (접기) ▶ (닫기)
    const int cy = height() / 2;
    const int x0 = width() - 56;
    p.setPen(QPen(palette().color(QPalette::WindowText), 1.4));
    p.setBrush(locked_ ? palette().color(QPalette::WindowText) : Qt::NoBrush);
    p.drawEllipse(QPointF(x0 + 6, cy), 4, 4);
    p.setBrush(collapsed_ ? palette().color(QPalette::WindowText) : Qt::NoBrush);
    p.drawRect(QRectF(x0 + 22, cy - 4, 9, 8));
    p.setBrush(Qt::NoBrush);
    QPolygonF tri;
    tri << QPointF(x0 + 42, cy - 4) << QPointF(x0 + 50, cy) << QPointF(x0 + 42, cy + 4);
    p.drawPolygon(tri);
}

void DockTitleBar::mousePressEvent(QMouseEvent* e) {
    const int x = e->pos().x() - (width() - 56);
    if (x >= 0 && x < 18) {
        locked_ = !locked_;
        QDockWidget::DockWidgetFeatures f = dock_->features();
        if (locked_) f &= ~(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
        else f |= QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable;
        dock_->setFeatures(f);
        update();
        e->accept();
    } else if (x >= 18 && x < 36) {
        collapsed_ = !collapsed_;
        if (dock_->widget()) dock_->widget()->setVisible(!collapsed_);
        update();
        e->accept();
    } else if (x >= 36 && x < 56) {
        dock_->close();
        e->accept();
    } else {
        QWidget::mousePressEvent(e); // 제목을 눌러 끌면 패널 이동
        e->ignore();
    }
}

// ---------------------------------------------------------------- 내비게이터
NavigatorPanel::NavigatorPanel(Document* doc, CanvasWidget* canvas, QWidget* parent) : QWidget(parent), doc_(doc), canvas_(canvas) {
    setMinimumSize(160, 120);
    auto upd = [this] { update(); };
    connect(doc_, &Document::changed, this, upd);
    connect(doc_, &Document::currentChanged, this, upd);
    connect(doc_, &Document::structureChanged, this, upd);
    connect(canvas_, &CanvasWidget::viewChanged, this, upd);
    connect(canvas_, &CanvasWidget::zoomChanged, this, upd);
}

QRectF NavigatorPanel::thumbRect() const {
    const QSize s = doc_->size();
    const QRectF area(6, 6, width() - 12, height() - 12);
    const double sc = std::min(area.width() / s.width(), area.height() / s.height());
    const QSizeF sz(s.width() * sc, s.height() * sc);
    return QRectF(area.center().x() - sz.width() / 2, area.center().y() - sz.height() / 2, sz.width(), sz.height());
}

void NavigatorPanel::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), palette().base());
    const QRectF tr = thumbRect();
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.drawImage(tr, doc_->composite(doc_->currentFrame()));
    const double sc = tr.width() / doc_->size().width();
    const QRectF v = canvas_->visibleImageRect();
    const QRectF vr(tr.x() + v.x() * sc, tr.y() + v.y() * sc, v.width() * sc, v.height() * sc);
    p.setClipRect(tr);
    p.setPen(QPen(QColor(0xe2, 0x4a, 0x4a), 2));
    p.setBrush(Qt::NoBrush);
    p.drawRect(vr);
}

void NavigatorPanel::moveTo(const QPoint& pos) {
    const QRectF tr = thumbRect();
    const double sc = tr.width() / doc_->size().width();
    canvas_->centerOnImagePoint(QPointF((pos.x() - tr.x()) / sc, (pos.y() - tr.y()) / sc));
}
void NavigatorPanel::mousePressEvent(QMouseEvent* e) { moveTo(e->pos()); }
void NavigatorPanel::mouseMoveEvent(QMouseEvent* e) {
    if (e->buttons() & Qt::LeftButton) moveTo(e->pos());
}

// ---------------------------------------------------------------- 가이드라인
GuidelinesPanel::GuidelinesPanel(CanvasWidget* canvas, QWidget* parent) : QWidget(parent) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);
    auto* grid = new QCheckBox(QStringLiteral("격자"));
    auto* gsz = new QSpinBox; gsz->setRange(4, 1024); gsz->setValue(64); gsz->setSuffix(" px");
    auto* center = new QCheckBox(QStringLiteral("중심선"));
    auto* thirds = new QCheckBox(QStringLiteral("3분할선"));
    auto* safe = new QCheckBox(QStringLiteral("안전 영역"));
    auto* spct = new QSpinBox; spct->setRange(10, 100); spct->setValue(90); spct->setSuffix(" %");
    auto* r1 = new QHBoxLayout; r1->addWidget(grid); r1->addWidget(gsz);
    auto* r2 = new QHBoxLayout; r2->addWidget(safe); r2->addWidget(spct);
    lay->addLayout(r1);
    lay->addLayout(r2);
    lay->addWidget(center);
    lay->addWidget(thirds);
    lay->addStretch();
    auto apply = [=] {
        CanvasWidget::Guides g;
        g.grid = grid->isChecked();
        g.gridSize = gsz->value();
        g.center = center->isChecked();
        g.thirds = thirds->isChecked();
        g.safe = safe->isChecked();
        g.safePct = spct->value();
        canvas->setGuides(g);
    };
    for (auto* c : {grid, center, thirds, safe}) connect(c, &QCheckBox::toggled, this, apply);
    connect(gsz, &QSpinBox::valueChanged, this, apply);
    connect(spct, &QSpinBox::valueChanged, this, apply);
}

// ---------------------------------------------------------------- 라이트 테이블
LightTablePanel::LightTablePanel(CanvasWidget* canvas, QWidget* parent) : QWidget(parent) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);
    auto* on = new QCheckBox(QStringLiteral("라이트 테이블 켜기 (L)"));
    on->setChecked(canvas->lightTable());
    auto* prev = new QSpinBox; prev->setRange(0, 8); prev->setValue(1);
    auto* next = new QSpinBox; next->setRange(0, 8); next->setValue(1);
    auto* op = new QSlider(Qt::Horizontal); op->setRange(5, 100); op->setValue(35);
    auto row = [&](const QString& t, QWidget* w) {
        auto* h = new QHBoxLayout;
        h->addWidget(new QLabel(t));
        h->addWidget(w, 1);
        lay->addLayout(h);
    };
    lay->addWidget(on);
    row(QStringLiteral("이전 이미지 수 (빨강)"), prev);
    row(QStringLiteral("다음 이미지 수 (파랑)"), next);
    row(QStringLiteral("불투명도"), op);
    lay->addStretch();
    connect(on, &QCheckBox::toggled, this, [this, canvas](bool v) {
        canvas->setLightTable(v);
        emit enabledChanged(v);
    });
    auto apply = [=] {
        CanvasWidget::LightTableParams p;
        p.prev = prev->value();
        p.next = next->value();
        p.opacity = op->value();
        canvas->setLightTableParams(p);
    };
    connect(prev, &QSpinBox::valueChanged, this, apply);
    connect(next, &QSpinBox::valueChanged, this, apply);
    connect(op, &QSlider::valueChanged, this, apply);
}

// ---------------------------------------------------------------- 리모컨
RemotePanel::RemotePanel(QWidget* parent) : QWidget(parent) {
    auto* g = new QGridLayout(this);
    g->setContentsMargins(6, 6, 6, 6);
    struct B { const char* t; const char* tip; int r, c; };
    auto mk = [&](const QString& t, const QString& tip, int r, int c, auto sig) {
        auto* b = new QPushButton(t);
        b->setToolTip(tip);
        b->setFixedHeight(32);
        connect(b, &QPushButton::clicked, this, sig);
        g->addWidget(b, r, c);
    };
    mk("▶", "재생", 0, 0, [this] { emit play(); });
    mk("■", "정지", 0, 1, [this] { emit stop(); });
    mk("|◀", "첫 프레임", 1, 0, [this] { emit first(); });
    mk("▶|", "마지막 프레임", 1, 1, [this] { emit last(); });
    mk("◀", "이전 프레임", 2, 0, [this] { emit prevFrame(); });
    mk("▶", "다음 프레임", 2, 1, [this] { emit nextFrame(); });
    mk("◀◆", "이전 이미지", 3, 0, [this] { emit prevImage(); });
    mk("◆▶", "다음 이미지", 3, 1, [this] { emit nextImage(); });
    g->setRowStretch(4, 1);
}

// ---------------------------------------------------------------- 공유 관리자
ShareManagerPanel::ShareManagerPanel(QWidget* parent) : QWidget(parent) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);
    auto add = [&](const QString& t, auto sig) {
        auto* b = new QPushButton(t);
        connect(b, &QPushButton::clicked, this, sig);
        lay->addWidget(b);
    };
    add(QStringLiteral("현재 프레임 → PNG"), [this] { emit exportCurrentPng(); });
    add(QStringLiteral("동영상 (MP4/MOV/WebM/GIF)"), [this] { emit exportVideo(); });
    add(QStringLiteral("전체 → PNG 시퀀스"), [this] { emit exportPngSequence(); });
    add(QStringLiteral("타임시트 → PDF"), [this] { emit exportTimesheetPdf(); });
    add(QStringLiteral("타임시트 → XDTS"), [this] { emit exportXdts(); });
    lay->addStretch();
}

// ---------------------------------------------------------------- 브러시 패널
BrushPanel::BrushPanel(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* body = new QWidget;
    auto* lay = new QVBoxLayout(body);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(2);
    scroll->setWidget(body);
    outer->addWidget(scroll);

    struct Group { const char* name; QList<int> items; };
    const Group groups[] = {
        {"펜 · 잉크", {0, 5, 6, 4, 14}},
        {"연필 · 색연필", {1, 10, 11, 12}},
        {"붓 · 수채", {7, 8, 9, 15}},
        {"에어 · 스프레이", {3, 13, 17}},
        {"도장", {16, 18, 19}},
        {"지우개", {2, 20, 21}},
    };
    buttons_.fill(nullptr, brushPresetCount());
    for (const Group& g : groups) {
        auto* hdr = new QLabel(QString("  ◉  ") + QString::fromUtf8(g.name));
        hdr->setStyleSheet("background:#3f3f3f;color:#b0b0b0;padding:2px 0;");
        lay->addWidget(hdr);
        auto* grid = new QGridLayout;
        grid->setSpacing(2);
        QList<QToolButton*> gb;
        int k = 0;
        for (int i : g.items) {
            const BrushPreset& pr = brushPreset(i);
            BrushParams p;
            p.size = pr.size;
            p.opacity = pr.opacity;
            p.hardness = pr.hardness;
            p.pressure = pr.pressure;
            p.preset = i;
            QImage st = brushPreview(p, pr.eraser ? QColor(0xe0, 0x70, 0x70) : QColor(0xee, 0xee, 0xee), QSize(56, 24));
            auto* b = new QToolButton;
            b->setIcon(QIcon(QPixmap::fromImage(st)));
            b->setIconSize(QSize(56, 24));
            b->setCheckable(true);
            b->setAutoExclusive(true);
            b->setMinimumSize(60, 30);
            b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            b->setToolTip(QString::fromUtf8(pr.name));
            connect(b, &QToolButton::clicked, this, [this, i] { emit presetSelected(i); });
            grid->addWidget(b, k / 3, k % 3);
            gb.push_back(b);
            buttons_[i] = b;
            ++k;
        }
        lay->addLayout(grid);
        grids_.push_back({grid, gb});
    }
    lay->addStretch();
    if (buttons_[0]) buttons_[0]->setChecked(true);
}

void BrushPanel::rebuild() {}

// 패널 너비에 맞춰 한 줄에 놓는 프리셋 수를 바꾼다 (넓히면 자연스럽게 더 많이 보인다)
void BrushPanel::reflow() {
    const int cols = std::max(2, (width() - 14) / 64);
    if (cols == cols_) return;
    cols_ = cols;
    for (auto& g : grids_) {
        for (QToolButton* b : g.second) g.first->removeWidget(b);
        int k = 0;
        for (QToolButton* b : g.second) {
            g.first->addWidget(b, k / cols_, k % cols_);
            ++k;
        }
        for (int c = 0; c < 12; ++c) g.first->setColumnStretch(c, c < cols_ ? 1 : 0);
    }
}

void BrushPanel::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    reflow();
}

void BrushPanel::setCurrent(int row) {
    if (row < 0 || row >= buttons_.size() || !buttons_[row]) return;
    QSignalBlocker b(buttons_[row]);
    buttons_[row]->setChecked(true);
}

void BrushPanel::setColor(const QColor& c) {
    color_ = c;
}
