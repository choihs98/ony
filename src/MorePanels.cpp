#include <algorithm>
#include "MorePanels.h"
#include <QHash>
#include <QTimer>
#include <QSettings>
#include <QMessageBox>
#include <QInputDialog>
#include <QFormLayout>
#include <QComboBox>
#include <QColorDialog>
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
    auto* rulerBox = new QComboBox;
    rulerBox->addItems({QStringLiteral("끔"), QStringLiteral("수평 · 수직"), QStringLiteral("각도 지정")});
    auto* rang = new QSpinBox; rang->setRange(-180, 180); rang->setValue(0); rang->setSuffix(QStringLiteral("°"));
    auto* r3 = new QHBoxLayout;
    r3->addWidget(new QLabel(QStringLiteral("직선자")));
    r3->addWidget(rulerBox, 1);
    r3->addWidget(rang);
    lay->addLayout(r3);
    auto* persp = new QCheckBox(QStringLiteral("원근 가이드 (소실점)"));
    auto* vcnt = new QSpinBox; vcnt->setRange(1, 3); vcnt->setValue(1); vcnt->setSuffix(QStringLiteral(" 점"));
    auto* r4 = new QHBoxLayout; r4->addWidget(persp); r4->addWidget(vcnt);
    lay->addLayout(r4);
    auto* snap = new QCheckBox(QStringLiteral("획을 소실점 방향으로 펴기"));
    lay->addWidget(snap);
    lay->addWidget(new QLabel(QStringLiteral("직선자·소실점을 켜면 획이 곧게 펴집니다.")));
    lay->addStretch();
    auto apply = [=] {
        CanvasWidget::Guides g;
        g.grid = grid->isChecked();
        g.gridSize = gsz->value();
        g.center = center->isChecked();
        g.thirds = thirds->isChecked();
        g.safe = safe->isChecked();
        g.safePct = spct->value();
        g.ruler = rulerBox->currentIndex();
        g.rulerAngle = rang->value();
        g.perspective = persp->isChecked();
        g.vanishCount = vcnt->value();
        g.snapToVanish = snap->isChecked();
        canvas->setGuides(g);
    };
    for (auto* c : {grid, center, thirds, safe, persp, snap}) connect(c, &QCheckBox::toggled, this, apply);
    for (auto* s : {gsz, spct, rang, vcnt}) connect(s, &QSpinBox::valueChanged, this, apply);
    connect(rulerBox, &QComboBox::currentIndexChanged, this, apply);
}

// ---------------------------------------------------------------- 라이트 테이블
LightTablePanel::LightTablePanel(CanvasWidget* canvas, QWidget* parent) : QWidget(parent) {
    using P = CanvasWidget::LightTableParams;
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);
    lay->setSpacing(6);
    auto* on = new QCheckBox(QStringLiteral("라이트 테이블 켜기"));
    on->setChecked(canvas->lightTable());
    lay->addWidget(on);

    auto* mode = new QComboBox;
    mode->addItems({"인스턴스 모드 (이미지가 시작되는 프레임만)", "프레임 모드 (모든 프레임)", "북마크 모드 (북마크한 프레임)", "이미지 마크 모드 (마크한 이미지)"});
    auto* colorMode = new QComboBox;
    colorMode->addItems({"색 입히기", "원래 색", "그라데이션 (멀수록 옅게)"});
    auto* prevCol = new QPushButton;
    auto* nextCol = new QPushButton;
    prevCol->setFixedSize(34, 22);
    nextCol->setFixedSize(34, 22);
    prevCol->setToolTip("이전 이미지 색");
    nextCol->setToolTip("다음 이미지 색");
    auto* form = new QFormLayout;
    form->addRow("표시 방식", mode);
    auto* ch = new QHBoxLayout;
    ch->addWidget(colorMode, 1);
    ch->addWidget(new QLabel("이전"));
    ch->addWidget(prevCol);
    ch->addWidget(new QLabel("다음"));
    ch->addWidget(nextCol);
    form->addRow("색", ch);
    lay->addLayout(form);

    // 이전 8칸 | 현재 | 다음 8칸 : 거리별 불투명도 슬라이더 (0 = 끔)
    auto* sl = new QHBoxLayout;
    sl->setSpacing(2);
    QList<QSlider*> sliders; // [0..7] = 이전(가까운 것부터), [8..15] = 다음
    auto mkSlider = [&](int dist, bool isPrev) {
        auto* col = new QVBoxLayout;
        auto* s = new QSlider(Qt::Vertical);
        s->setRange(0, 100);
        s->setFixedHeight(84);
        s->setToolTip(QString("%1 %2번째 이미지").arg(isPrev ? "이전" : "다음").arg(dist + 1));
        auto* t = new QLabel(QString::number(isPrev ? -(dist + 1) : dist + 1));
        t->setAlignment(Qt::AlignCenter);
        QFont f = t->font();
        f.setPointSizeF(f.pointSizeF() - 1.5);
        t->setFont(f);
        col->addWidget(s, 1, Qt::AlignHCenter);
        col->addWidget(t);
        sl->addLayout(col);
        return s;
    };
    // 왼쪽부터 먼 이전 이미지(-8) ... 가까운 이전 이미지(-1) 순서로 만들고, 거리 순서(가까운 것이 0번)로 저장한다
    for (int i = P::kSlots - 1; i >= 0; --i) sliders.prepend(mkSlider(i, true));
    QList<QSlider*> prevS = sliders, nextS(P::kSlots);
    {
        auto* zero = new QLabel("0");
        zero->setAlignment(Qt::AlignHCenter | Qt::AlignBottom);
        sl->addWidget(zero);
    }
    for (int i = 0; i < P::kSlots; ++i) nextS[i] = mkSlider(i, false);
    lay->addLayout(sl);

    auto* link = new QCheckBox("슬라이더 함께 움직이기");
    auto* front = new QCheckBox("현재 그림 위에 표시");
    auto* all = new QCheckBox("보이는 모든 레이어에 적용");
    lay->addWidget(link);
    lay->addWidget(front);
    lay->addWidget(all);
    auto* btns = new QHBoxLayout;
    auto* reset = new QPushButton("초기화");
    auto* save = new QPushButton("설정 저장");
    auto* load = new QPushButton("설정 불러오기");
    btns->addWidget(reset);
    btns->addWidget(save);
    btns->addWidget(load);
    lay->addLayout(btns);
    lay->addStretch();

    auto colorStyle = [](QPushButton* b, const QColor& c) { b->setStyleSheet(QString("background:%1;border:1px solid #222;").arg(c.name())); };
    auto readParams = [=] {
        P p;
        p.mode = mode->currentIndex();
        p.colorMode = colorMode->currentIndex();
        p.prevColor = prevCol->property("c").value<QColor>();
        p.nextColor = nextCol->property("c").value<QColor>();
        for (int i = 0; i < P::kSlots; ++i) {
            p.prevOp[i] = prevS[i]->value();
            p.nextOp[i] = nextS[i]->value();
        }
        p.blendToFront = front->isChecked();
        p.allLayers = all->isChecked();
        return p;
    };
    auto showParams = [=](const P& p) {
        QSignalBlocker b1(mode), b2(colorMode), b3(front), b4(all);
        mode->setCurrentIndex(p.mode);
        colorMode->setCurrentIndex(p.colorMode);
        prevCol->setProperty("c", p.prevColor);
        nextCol->setProperty("c", p.nextColor);
        colorStyle(prevCol, p.prevColor);
        colorStyle(nextCol, p.nextColor);
        front->setChecked(p.blendToFront);
        all->setChecked(p.allLayers);
        for (int i = 0; i < P::kSlots; ++i) {
            QSignalBlocker s1(prevS[i]), s2(nextS[i]);
            prevS[i]->setValue(p.prevOp[i]);
            nextS[i]->setValue(p.nextOp[i]);
        }
    };
    auto push = [=] {
        const P p = readParams();
        canvas->setLightTableParams(p);
        QSettings st("Ony", "Ony");
        st.beginGroup("lightTable/current");
        st.setValue("mode", p.mode);
        st.setValue("colorMode", p.colorMode);
        st.setValue("prevColor", p.prevColor);
        st.setValue("nextColor", p.nextColor);
        QStringList po, no;
        for (int i = 0; i < P::kSlots; ++i) { po << QString::number(p.prevOp[i]); no << QString::number(p.nextOp[i]); }
        st.setValue("prevOp", po);
        st.setValue("nextOp", no);
        st.setValue("front", p.blendToFront);
        st.setValue("all", p.allLayers);
    };
    auto fromSettings = [=](const QString& group) {
        QSettings st("Ony", "Ony");
        st.beginGroup(group);
        P p;
        p.mode = st.value("mode", p.mode).toInt();
        p.colorMode = st.value("colorMode", p.colorMode).toInt();
        p.prevColor = st.value("prevColor", p.prevColor).value<QColor>();
        p.nextColor = st.value("nextColor", p.nextColor).value<QColor>();
        const QStringList po = st.value("prevOp").toStringList(), no = st.value("nextOp").toStringList();
        for (int i = 0; i < P::kSlots; ++i) {
            if (i < po.size()) p.prevOp[i] = po[i].toInt();
            if (i < no.size()) p.nextOp[i] = no[i].toInt();
        }
        p.blendToFront = st.value("front", false).toBool();
        p.allLayers = st.value("all", false).toBool();
        return p;
    };
    showParams(fromSettings("lightTable/current"));
    canvas->setLightTableParams(readParams());

    connect(on, &QCheckBox::toggled, this, [this, canvas](bool v) {
        canvas->setLightTable(v);
        emit enabledChanged(v);
    });
    connect(mode, &QComboBox::currentIndexChanged, this, push);
    connect(colorMode, &QComboBox::currentIndexChanged, this, push);
    connect(front, &QCheckBox::toggled, this, push);
    connect(all, &QCheckBox::toggled, this, push);
    auto pickColor = [=](QPushButton* b) {
        const QColor c = QColorDialog::getColor(b->property("c").value<QColor>(), this, "라이트 테이블 색");
        if (!c.isValid()) return;
        b->setProperty("c", c);
        colorStyle(b, c);
        push();
    };
    connect(prevCol, &QPushButton::clicked, this, [=] { pickColor(prevCol); });
    connect(nextCol, &QPushButton::clicked, this, [=] { pickColor(nextCol); });
    // 슬라이더: 링크가 켜져 있으면 모든 슬라이더가 같은 비율로 함께 움직인다
    auto all16 = QList<QSlider*>();
    all16 << prevS << nextS;
    auto* busy = new bool(false);
    for (QSlider* s : all16) {
        auto* last = new int(s->value());
        connect(s, &QSlider::valueChanged, this, [=](int v) {
            if (link->isChecked() && !*busy) {
                *busy = true;
                const int diff = v - *last;
                for (QSlider* o : all16)
                    if (o != s) o->setValue(std::clamp(o->value() + diff, 0, 100));
                *busy = false;
            }
            *last = v;
            if (!*busy) push();
        });
    }
    connect(reset, &QPushButton::clicked, this, [=] {
        showParams(P());
        push();
    });
    connect(save, &QPushButton::clicked, this, [=] {
        bool ok = false;
        const QString n = QInputDialog::getText(this, "라이트 테이블 설정 저장", "이름:", QLineEdit::Normal, "내 설정", &ok);
        if (!ok || n.trimmed().isEmpty()) return;
        push();
        QSettings st("Ony", "Ony");
        st.beginGroup("lightTable/current");
        const QStringList keys = st.allKeys();
        QVariantMap vals;
        for (const QString& k : keys) vals[k] = st.value(k);
        st.endGroup();
        st.beginGroup("lightTable/presets/" + n.trimmed());
        for (auto it = vals.cbegin(); it != vals.cend(); ++it) st.setValue(it.key(), it.value());
    });
    connect(load, &QPushButton::clicked, this, [=] {
        QSettings st("Ony", "Ony");
        st.beginGroup("lightTable/presets");
        const QStringList names = st.childGroups();
        st.endGroup();
        if (names.isEmpty()) {
            QMessageBox::information(this, "라이트 테이블", "저장된 설정이 없습니다.");
            return;
        }
        bool ok = false;
        const QString n = QInputDialog::getItem(this, "라이트 테이블 설정 불러오기", "설정:", names, 0, false, &ok);
        if (!ok) return;
        showParams(fromSettings("lightTable/presets/" + n));
        push();
    });
}

// ---------------------------------------------------------------- 플립 패널
FlipPanel::FlipPanel(Document* doc, QWidget* parent) : QWidget(parent), doc_(doc) {
    setMinimumSize(220, 220);
    setMouseTracking(false);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);
    lay->addStretch(1); // 위쪽은 미리보기 그림
    auto* row1 = new QHBoxLayout;
    from_ = new QSpinBox;
    to_ = new QSpinBox;
    from_->setRange(1, doc_->frameCount());
    to_->setRange(1, doc_->frameCount());
    from_->setValue(1);
    to_->setValue(doc_->frameCount());
    row1->addWidget(new QLabel("범위"));
    row1->addWidget(from_);
    row1->addWidget(new QLabel("~"));
    row1->addWidget(to_);
    lay->addLayout(row1);
    auto* row2 = new QHBoxLayout;
    fps_ = new QSpinBox;
    fps_->setRange(1, 120);
    fps_->setValue(doc_->fps());
    fps_->setSuffix(" fps");
    mode_ = new QComboBox;
    mode_->addItems({"반복", "왕복", "한 번"});
    play_ = new QPushButton("플립");
    play_->setCheckable(true);
    row2->addWidget(fps_);
    row2->addWidget(mode_);
    row2->addWidget(play_);
    lay->addLayout(row2);
    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, [this] { tick(); });
    connect(fps_, &QSpinBox::valueChanged, this, [this](int v) { timer_->setInterval(1000 / std::max(1, v)); });
    timer_->setInterval(1000 / std::max(1, doc_->fps()));
    connect(play_, &QPushButton::toggled, this, [this](bool on) {
        play_->setText(on ? "정지" : "플립");
        if (on) {
            flipFrame_ = doc_->currentFrame();
            dir_ = 1;
            timer_->start();
        } else {
            timer_->stop();
        }
    });
    connect(doc_, &Document::currentChanged, this, [this] {
        if (!timer_->isActive()) flipFrame_ = doc_->currentFrame();
        update();
    });
    connect(doc_, &Document::structureChanged, this, [this] {
        cache_.clear();
        from_->setMaximum(doc_->frameCount());
        to_->setMaximum(doc_->frameCount());
        update();
    });
    connect(doc_, &Document::changed, this, [this] { cache_.clear(); update(); });
}

QRect FlipPanel::previewRect() const {
    const QRect avail(6, 6, width() - 12, height() - 6 - 74);
    if (avail.width() < 20 || avail.height() < 20) return QRect();
    const QSize s = doc_->size().scaled(avail.size(), Qt::KeepAspectRatio);
    return QRect(QPoint(avail.x() + (avail.width() - s.width()) / 2, avail.y()), s);
}

QImage FlipPanel::frameImage(int f) {
    if (!cache_.contains(f)) {
        if (cache_.size() > 96) cache_.clear();
        const QSize box(360, 360);
        QImage full = doc_->composite(f, true, false);
        cache_.insert(f, full.size().width() > box.width() ? full.scaled(box, Qt::KeepAspectRatio, Qt::SmoothTransformation) : full);
    }
    return cache_.value(f);
}

void FlipPanel::tick() {
    const int a = std::min(from_->value(), to_->value()) - 1;
    const int b = std::max(from_->value(), to_->value()) - 1;
    int next = flipFrame_ + dir_;
    if (next > b) {
        if (mode_->currentIndex() == 0) next = a;
        else if (mode_->currentIndex() == 1) { dir_ = -1; next = std::max(a, b - 1); }
        else { play_->setChecked(false); return; }
    } else if (next < a) {
        dir_ = 1;
        next = std::min(b, a + 1);
    }
    flipFrame_ = std::clamp(next, 0, doc_->frameCount() - 1);
    update();
}

void FlipPanel::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const QRect r = previewRect();
    if (r.isEmpty()) return;
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.drawImage(r, frameImage(std::clamp(flipFrame_, 0, doc_->frameCount() - 1)));
    p.setPen(palette().color(QPalette::WindowText));
    p.drawRect(r.adjusted(0, 0, -1, -1));
    // 아래쪽 진행 막대 (클릭/드래그로 프레임 넘기기)
    const QRect bar(r.left(), r.bottom() + 4, r.width(), 8);
    p.fillRect(bar, QColor(0, 0, 0, 90));
    const int n = std::max(1, doc_->frameCount() - 1);
    const int x = bar.left() + int(double(flipFrame_) / n * bar.width());
    p.fillRect(QRect(x - 2, bar.top() - 2, 5, bar.height() + 4), palette().color(QPalette::Highlight));
    p.drawText(QRect(r.left(), bar.bottom() + 2, r.width(), 16), Qt::AlignCenter, QString("프레임 %1 / %2").arg(flipFrame_ + 1).arg(doc_->frameCount()));
}

void FlipPanel::scrubTo(const QPoint& pos) {
    const QRect r = previewRect();
    if (r.isEmpty()) return;
    const double t = std::clamp(double(pos.x() - r.left()) / std::max(1, r.width()), 0.0, 1.0);
    const int a = std::min(from_->value(), to_->value()) - 1;
    const int b = std::max(from_->value(), to_->value()) - 1;
    flipFrame_ = a + int(t * (b - a) + 0.5); // 미리보기 너비 전체가 범위에 대응한다 (빠르게 훑어보기)
    update();
}

void FlipPanel::mousePressEvent(QMouseEvent* e) {
    if (play_->isChecked()) play_->setChecked(false);
    scrubTo(e->pos());
}
void FlipPanel::mouseMoveEvent(QMouseEvent* e) {
    if (e->buttons() & Qt::LeftButton) scrubTo(e->pos());
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
        {"도장 · 점선", {16, 18, 19, 22, 23, 24}},
        {"지우개", {2, 20, 21}},
        {"컷", {25, 26}},
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
