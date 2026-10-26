#include "Panels.h"
#include "Document.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QSlider>
#include <QSpinBox>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QSignalBlocker>
#include <algorithm>
#include <cmath>
#include <QPixmap>
#include <QMouseEvent>
#include <QConicalGradient>

#include "Style.h"

namespace {
QToolButton* makeBtn(const QString& iconName, const QString& tip, bool checkable = true) {
    auto* b = new QToolButton;
    b->setIcon(Style::icon(iconName, 22));
    b->setIconSize(QSize(22, 22));
    b->setToolTip(tip);
    b->setCheckable(checkable);
    b->setFixedSize(30, 28); // 글자 없이 아이콘만 쓰므로 잘릴 일이 없다
    return b;
}
}

// ---------------- MainPanel ----------------
void MainPanel::selectTool(int id) {
    if (auto* b = group_ ? group_->button(id) : nullptr) b->click();
}

void ToolPanel::bumpSize(int delta) {
    size_->setValue(std::clamp(size_->value() + delta, size_->minimum(), size_->maximum()));
}

MainPanel::MainPanel(QWidget* parent) : QWidget(parent) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);

    struct Item { const char* icon; const char* tip; };
    const Item items[] = {
        {"pen", "획 (자유 곡선)"},          {"line", "선"},
        {"rect", "사각형"},                 {"ellipse", "원"},
        {"spline", "스플라인 (클릭으로 점 추가, 더블클릭/Enter 로 확정)"}, {"fill", "페인트통 (CTG 선화 레이어에서는 CTG 채우기)"},
        {"select", "선택 / 이동"},          {"rig", "리깅 / 핀 워프 (클릭=핀, Shift+클릭=고정 핀, Enter=적용)"},
        {"crop", "자르기 / 선택"},          {"transform", "변형 (변형 대화상자 열기). 화면 이동: 스페이스 / 가운데 버튼"},
    };
    auto* grid = new QGridLayout;
    grid->setSpacing(2);
    grid->setContentsMargins(0, 0, 0, 0);
    auto* group = new QButtonGroup(this);
    group_ = group;
    group->setExclusive(true);
    int i = 0;
    for (const auto& it : items) {
        auto* b = makeBtn(QString::fromUtf8(it.icon), QString::fromUtf8(it.tip));
        group->addButton(b, i);
        grid->addWidget(b, i / 6, i % 6);
        if (i == 0) b->setChecked(true);
        ++i;
    }
    connect(group, &QButtonGroup::idClicked, this, &MainPanel::toolSelected);
    auto* cam = makeBtn("camera", "카메라 켜기/끄기");
    connect(cam, &QToolButton::toggled, this, &MainPanel::cameraToggled);
    grid->addWidget(cam, i / 6, i % 6);
    ++i;
    auto* clear = makeBtn("clear", "현재 이미지 / 선택 영역 지우기", false);
    connect(clear, &QToolButton::clicked, this, &MainPanel::clearRequested);
    grid->addWidget(clear, i / 6, i % 6);
    ++i;
    auto* smooth = makeBtn("smooth", "선 보정 켜기/끄기");
    connect(smooth, &QToolButton::toggled, this, &MainPanel::smoothingToggled);
    grid->addWidget(smooth, i / 6, i % 6);
    ++i;
    auto* hist = makeBtn("history", "브러시 기록", false);
    connect(hist, &QToolButton::clicked, this, [this, hist] { emit historyRequested(hist->mapToGlobal(QPoint(0, hist->height()))); });
    grid->addWidget(hist, i / 6, i % 6);
    ++i;
    auto* paper = makeBtn("paper", "종이 질감 켜기/끄기");
    connect(paper, &QToolButton::toggled, this, &MainPanel::paperToggled);
    grid->addWidget(paper, i / 6, i % 6);
    ++i;
    auto* stencil = makeBtn("stencil", "스텐실: 선택 영역 안에서만 그리기");
    connect(stencil, &QToolButton::toggled, this, &MainPanel::stencilToggled);
    grid->addWidget(stencil, i / 6, i % 6);
    lay->addLayout(grid);
    lay->addStretch();
}

// ---------------- ColorWheel ----------------
ColorWheel::ColorWheel(QWidget* parent) : QWidget(parent) {
    setMinimumSize(120, 120);
    setMouseTracking(false);
}

QPointF ColorWheel::center() const { return QPointF(width() / 2.0, height() / 2.0); }
double ColorWheel::outer() const { return std::min(width(), height()) / 2.0 - 3; }
double ColorWheel::inner() const { return outer() * 0.78; }

QColor ColorWheel::color() const { return QColor::fromHsvF(std::fmod(h_, 360.0) / 360.0, std::clamp(s_, 0.0, 1.0), std::clamp(v_, 0.0, 1.0)); }

void ColorWheel::setColor(const QColor& c) {
    float h, s, v;
    c.getHsvF(&h, &s, &v);
    if (h >= 0) h_ = h * 360.0; // 무채색이면 색조 유지
    s_ = s;
    v_ = v;
    update();
}

void ColorWheel::triPoints(QPointF& pHue, QPointF& pWhite, QPointF& pBlack) const {
    const double r = inner() - 3;
    const double a = h_ * M_PI / 180.0;
    const QPointF c = center();
    auto at = [&](double ang) { return QPointF(c.x() + r * std::cos(ang), c.y() - r * std::sin(ang)); };
    pHue = at(a);
    pWhite = at(a + 2.0 * M_PI / 3.0);
    pBlack = at(a + 4.0 * M_PI / 3.0);
}

void ColorWheel::rebuild() {
    cache_ = QImage(size(), QImage::Format_ARGB32_Premultiplied);
    cache_.fill(Qt::transparent);
    QPainter p(&cache_);
    p.setRenderHint(QPainter::Antialiasing);
    QConicalGradient g(center(), 0);
    for (int i = 0; i <= 12; ++i) g.setColorAt(i / 12.0, QColor::fromHsvF(std::fmod(i / 12.0, 1.0), 1, 1));
    // QConicalGradient 은 반시계 방향이므로 그대로 두면 handle() 의 각도와 일치한다
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawEllipse(center(), outer(), outer());
    p.setCompositionMode(QPainter::CompositionMode_Clear);
    p.drawEllipse(center(), inner(), inner());
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    // 삼각형: 색조 꼭짓점 / 흰색 / 검정
    QPointF ph, pw, pb;
    triPoints(ph, pw, pb);
    const QColor hue = QColor::fromHsvF(std::fmod(h_, 360.0) / 360.0, 1, 1);
    const QRectF bb = QPolygonF({ph, pw, pb}).boundingRect().adjusted(-2, -2, 2, 2);
    const double det = (pw.y() - pb.y()) * (ph.x() - pb.x()) + (pb.x() - pw.x()) * (ph.y() - pb.y());
    for (int y = int(bb.top()); y <= int(bb.bottom()); ++y)
        for (int x = int(bb.left()); x <= int(bb.right()); ++x) {
            if (x < 0 || y < 0 || x >= cache_.width() || y >= cache_.height()) continue;
            const double px = x + 0.5, py = y + 0.5;
            const double l1 = ((pw.y() - pb.y()) * (px - pb.x()) + (pb.x() - pw.x()) * (py - pb.y())) / det;
            const double l2 = ((pb.y() - ph.y()) * (px - pb.x()) + (ph.x() - pb.x()) * (py - pb.y())) / det;
            const double l3 = 1.0 - l1 - l2;
            const double e = 0.02;
            if (l1 < -e || l2 < -e || l3 < -e) continue;
            const double a1 = std::clamp(l1, 0.0, 1.0), a2 = std::clamp(l2, 0.0, 1.0), a3 = std::clamp(l3, 0.0, 1.0);
            const double cov = std::clamp(1.0 + std::min({l1, l2, l3}) / e * 0.5, 0.0, 1.0);
            const double sum = a1 + a2 + a3;
            const int r = int((hue.red() * a1 + 255 * a2) / sum), gg = int((hue.green() * a1 + 255 * a2) / sum),
                      b = int((hue.blue() * a1 + 255 * a2) / sum);
            cache_.setPixelColor(x, y, QColor(r, gg, b, int(255 * cov)));
        }
    cacheH_ = h_;
}

void ColorWheel::resizeEvent(QResizeEvent*) { cacheH_ = -1; }

void ColorWheel::paintEvent(QPaintEvent*) {
    if (cacheH_ != h_ || cache_.size() != size()) rebuild();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.drawImage(0, 0, cache_);
    const QPointF c = center();
    // 색조 표시
    const double a = h_ * M_PI / 180.0;
    const double rm = (outer() + inner()) / 2;
    const QPointF hp(c.x() + rm * std::cos(a), c.y() - rm * std::sin(a));
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(Qt::white, 2));
    p.drawEllipse(hp, 5, 5);
    p.setPen(QPen(Qt::black, 1));
    p.drawEllipse(hp, 6.5, 6.5);
    // 채도/명도 표시
    QPointF ph, pw, pb;
    triPoints(ph, pw, pb);
    const double wHue = s_ * v_, wWhite = (1 - s_) * v_, wBlack = 1 - v_;
    const QPointF sp = ph * wHue + pw * wWhite + pb * wBlack;
    p.setPen(QPen(Qt::white, 2));
    p.drawEllipse(sp, 4.5, 4.5);
    p.setPen(QPen(Qt::black, 1));
    p.drawEllipse(sp, 6, 6);
}

void ColorWheel::handle(const QPointF& pos, bool press) {
    const QPointF c = center();
    const double dx = pos.x() - c.x(), dy = c.y() - pos.y();
    const double dist = std::hypot(dx, dy);
    if (press) {
        if (dist >= inner() - 1 && dist <= outer() + 4) drag_ = Drag::Ring;
        else drag_ = Drag::Tri;
    }
    if (drag_ == Drag::Ring) {
        double ang = std::atan2(dy, dx) * 180.0 / M_PI;
        if (ang < 0) ang += 360.0;
        h_ = ang;
    } else if (drag_ == Drag::Tri) {
        QPointF ph, pw, pb;
        triPoints(ph, pw, pb);
        const double det = (pw.y() - pb.y()) * (ph.x() - pb.x()) + (pb.x() - pw.x()) * (ph.y() - pb.y());
        double l1 = ((pw.y() - pb.y()) * (pos.x() - pb.x()) + (pb.x() - pw.x()) * (pos.y() - pb.y())) / det;
        double l2 = ((pb.y() - ph.y()) * (pos.x() - pb.x()) + (ph.x() - pb.x()) * (pos.y() - pb.y())) / det;
        l1 = std::clamp(l1, 0.0, 1.0);
        l2 = std::clamp(l2, 0.0, 1.0);
        if (l1 + l2 > 1.0) {
            const double t = l1 + l2;
            l1 /= t;
            l2 /= t;
        }
        v_ = l1 + l2;
        s_ = v_ > 1e-6 ? l1 / v_ : 0;
    } else {
        return;
    }
    update();
    emit colorChanged(color());
}

void ColorWheel::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) handle(e->position(), true);
}
void ColorWheel::mouseMoveEvent(QMouseEvent* e) {
    if (e->buttons() & Qt::LeftButton) handle(e->position(), false);
}
void ColorWheel::mouseReleaseEvent(QMouseEvent*) { drag_ = Drag::None; }

// ---------------- ColorPanel ----------------
ColorPanel::ColorPanel(QWidget* parent) : QWidget(parent) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(3);
    auto* row = new QHBoxLayout;
    row->setSpacing(3);
    btnA_ = new QToolButton;
    btnA_->setFixedSize(62, 26);
    btnA_->setToolTip(QStringLiteral("A 색상 (도구에 사용) — 클릭하면 색상 대화상자"));
    btnB_ = new QToolButton;
    btnB_->setFixedSize(62, 26);
    btnB_->setToolTip(QStringLiteral("B 색상 — 클릭하면 색상 대화상자"));
    auto* swap = new QToolButton;
    swap->setText(QStringLiteral("⇄"));
    swap->setFixedSize(20, 26);
    swap->setToolTip(QStringLiteral("A/B 색상 교환 (N)"));
    connect(swap, &QToolButton::clicked, this, [this] { swapColors(); });
    row->addWidget(btnA_);
    row->addWidget(swap);
    row->addWidget(btnB_);
    row->addStretch();
    lay->addLayout(row);

    auto* hdr = new QLabel(QStringLiteral(" 색상환"));
    hdr->setStyleSheet("background:#3a3a3a;color:#a0a0a0;padding:1px 2px;");
    lay->addWidget(hdr);
    wheel_ = new ColorWheel;
    lay->addWidget(wheel_, 1);
    rgb_ = new QLabel;
    rgb_->setAlignment(Qt::AlignCenter);
    lay->addWidget(rgb_);

    auto* sw = new QGridLayout;
    sw->setSpacing(2);
    const QColor palette[] = {
        QColor("#000000"), QColor("#ffffff"), QColor("#808080"), QColor("#c0c0c0"),
        QColor("#e53935"), QColor("#fb8c00"), QColor("#fdd835"), QColor("#43a047"),
        QColor("#00acc1"), QColor("#1e88e5"), QColor("#5e35b1"), QColor("#d81b60"),
        QColor("#6d4c41"), QColor("#ffcc80"), QColor("#a5d6a7"), QColor("#90caf9"),
    };
    int i = 0;
    for (const QColor& c : palette) {
        auto* b = new QToolButton;
        b->setFixedSize(16, 16);
        b->setStyleSheet(QString("background:%1;border:1px solid #333;").arg(c.name()));
        connect(b, &QToolButton::clicked, this, [this, c] { setColorA(c); });
        sw->addWidget(b, i / 8, i % 8);
        ++i;
    }
    lay->addLayout(sw);

    connect(wheel_, &ColorWheel::colorChanged, this, [this](const QColor& c) {
        if (syncing_) return;
        a_ = c;
        refresh();
        emit colorAChanged(a_);
    });
    connect(btnA_, &QToolButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(a_, this, QStringLiteral("A 색상"));
        if (c.isValid()) setColorA(c);
    });
    connect(btnB_, &QToolButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(b_, this, QStringLiteral("B 색상"));
        if (c.isValid()) {
            b_ = c;
            refresh();
        }
    });
    wheel_->setColor(a_);
    refresh();
}

void ColorPanel::setColorA(const QColor& c) {
    a_ = c;
    refresh();
    emit colorAChanged(a_);
}

void ColorPanel::swapColors() {
    std::swap(a_, b_);
    refresh();
    emit colorAChanged(a_);
}

void ColorPanel::refresh() {
    btnA_->setStyleSheet(QString("background:%1;border:2px solid #d8d8d8;").arg(a_.name()));
    btnB_->setStyleSheet(QString("background:%1;border:1px solid #222;").arg(b_.name()));
    syncing_ = true;
    if (wheel_->color() != a_) wheel_->setColor(a_);
    syncing_ = false;
    rgb_->setText(QString("R:%1 G:%2 B:%3").arg(a_.red()).arg(a_.green()).arg(a_.blue()));
}

// ---------------- ToolPanel ----------------
ToolPanel::ToolPanel(QWidget* parent) : QWidget(parent) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(3);
    list_ = new QListWidget(this);
    for (int i = 0; i < brushPresetCount(); ++i) list_->addItem(QString::fromUtf8(brushPreset(i).name));
    list_->setCurrentRow(0);
    list_->hide(); // 모델로만 사용

    // 브러시 아이콘 격자 (이름은 툴팁)
    auto* grid = new QGridLayout;
    grid->setSpacing(2);
    for (int i = 0; i < brushPresetCount(); ++i) {
        const BrushPreset& pr = brushPreset(i);
        BrushParams bp;
        bp.size = pr.size;
        bp.opacity = pr.opacity;
        bp.hardness = pr.hardness;
        bp.pressure = pr.pressure;
        bp.preset = i;
        QImage st = brushPreview(bp, pr.eraser ? QColor(0xe0, 0x70, 0x70) : QColor(0xe6, 0xe6, 0xe6), QSize(44, 20));
        auto* b = new QToolButton;
        b->setIcon(QIcon(QPixmap::fromImage(st)));
        b->setIconSize(QSize(44, 20));
        b->setCheckable(true);
        b->setAutoExclusive(true);
        b->setFixedSize(48, 26);
        b->setToolTip(QString::fromUtf8(pr.name));
        connect(b, &QToolButton::clicked, this, [this, i] { list_->setCurrentRow(i); });
        grid->addWidget(b, i / 4, i % 4);
        icons_.push_back(b);
    }
    lay->addLayout(grid);

    auto* hdr = new QLabel(QStringLiteral(" 옵션"));
    hdr->setStyleSheet("background:#3a3a3a;color:#a0a0a0;padding:1px 2px;");
    lay->addWidget(hdr);

    auto makeRow = [&](const QString& label, int lo, int hi, int val, QSlider*& s, QSpinBox*& v, const QString& suffix) {
        auto* row = new QHBoxLayout;
        row->setSpacing(4);
        auto* l = new QLabel(label);
        l->setFixedWidth(44);
        l->setStyleSheet("color:#a0a0a0;");
        s = new QSlider(Qt::Horizontal);
        s->setRange(lo, hi);
        s->setValue(val);
        v = new QSpinBox;
        v->setRange(lo, hi);
        v->setValue(val);
        v->setSuffix(suffix);
        v->setButtonSymbols(QAbstractSpinBox::NoButtons);
        v->setFixedWidth(52);
        v->setAlignment(Qt::AlignRight);
        row->addWidget(l);
        row->addWidget(s, 1);
        row->addWidget(v);
        lay->addLayout(row);
    };
    makeRow(QStringLiteral("크기"), 1, 200, 6, size_, sizeV_, QString());
    makeRow(QStringLiteral("경도"), 0, 100, 85, hardness_, hardnessV_, QStringLiteral("%"));
    makeRow(QStringLiteral("불투명도"), 1, 100, 100, opacity_, opacityV_, QStringLiteral("%"));
    pressure_ = new QCheckBox(QStringLiteral("필압 사용"));
    lay->addWidget(pressure_);
    lay->addStretch();

    // 슬라이더 <-> 숫자 칸 동기화
    auto link = [this](QSlider* s, QSpinBox* v) {
        connect(s, &QSlider::valueChanged, this, [this, v](int x) {
            QSignalBlocker b(v);
            v->setValue(x);
            emitParams();
        });
        connect(v, &QSpinBox::valueChanged, this, [s](int x) { s->setValue(x); });
    };
    link(size_, sizeV_);
    link(opacity_, opacityV_);
    link(hardness_, hardnessV_);
    connect(pressure_, &QCheckBox::toggled, this, [this](bool) { emitParams(); });
    connect(list_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0) return;
        // 프리셋의 기본 크기/불투명도/경도/필압을 적용한다
        const BrushPreset& pr = brushPreset(row);
        {
            QSignalBlocker b1(opacity_), b2(hardness_), b3(size_), b4(pressure_), b5(sizeV_), b6(opacityV_), b7(hardnessV_);
            size_->setValue(pr.size);
            sizeV_->setValue(pr.size);
            opacity_->setValue(pr.opacity);
            opacityV_->setValue(pr.opacity);
            hardness_->setValue(pr.hardness);
            hardnessV_->setValue(pr.hardness);
            pressure_->setChecked(pr.pressure);
        }
        syncIcons(row);
        emit paramsChanged(params());
        emit brushSelected(row);
    });
    syncIcons(0);
}

void ToolPanel::syncIcons(int row) {
    for (int i = 0; i < icons_.size(); ++i) {
        QSignalBlocker b(icons_[i]);
        icons_[i]->setChecked(i == row);
    }
}

BrushParams ToolPanel::params() const {
    BrushParams p;
    p.size = size_->value();
    p.opacity = opacity_->value();
    p.hardness = hardness_->value();
    p.pressure = pressure_->isChecked();
    p.preset = std::max(0, list_->currentRow());
    return p;
}

void ToolPanel::setParams(const BrushParams& p) {
    QSignalBlocker b1(size_), b2(opacity_), b3(hardness_), b4(pressure_), b5(sizeV_), b6(opacityV_), b7(hardnessV_);
    size_->setValue(p.size);
    sizeV_->setValue(p.size);
    opacity_->setValue(p.opacity);
    opacityV_->setValue(p.opacity);
    hardness_->setValue(p.hardness);
    hardnessV_->setValue(p.hardness);
    pressure_->setChecked(p.pressure);
}

void ToolPanel::setRow(int row) {
    QSignalBlocker b(list_);
    list_->setCurrentRow(row);
    syncIcons(row);
}

void ToolPanel::selectPreset(int row) {
    if (list_->currentRow() == row) { // 이미 선택된 프리셋이어도 도구(펜/지우개)는 다시 맞춘다
        emit brushSelected(row);
        return;
    }
    list_->setCurrentRow(row); // currentRowChanged 로 크기/불투명도 등도 함께 바뀐다
}

void ToolPanel::emitParams() {
    emit paramsChanged(params());
}

// ---------------- LayerStackPanel ----------------
LayerStackPanel::LayerStackPanel(Document* doc, QWidget* parent) : QWidget(parent), doc_(doc) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    list_ = new QListWidget;
    lay->addWidget(list_, 1);

    auto* orow = new QHBoxLayout;
    orow->addWidget(new QLabel(QStringLiteral("불투명도")));
    opacity_ = new QSlider(Qt::Horizontal);
    opacity_->setRange(0, 100);
    opacity_->setValue(100);
    orow->addWidget(opacity_);
    lay->addLayout(orow);

    auto* row = new QHBoxLayout;
    auto add = [&](const QString& t, const QString& tip, auto fn) {
        auto* b = new QToolButton;
        b->setText(t);
        b->setToolTip(tip);
        connect(b, &QToolButton::clicked, this, fn);
        row->addWidget(b);
    };
    add("새", "새 레이어", [this] { doc_->addLayer(QString("레이어 %1").arg(doc_->layerCount() + 1)); });
    add("복제", "레이어 복제", [this] { doc_->duplicateLayer(doc_->currentLayer()); });
    add("삭제", "레이어 삭제", [this] { doc_->removeLayer(doc_->currentLayer()); });
    add("▲", "위로", [this] { doc_->moveLayer(doc_->currentLayer(), +1); });
    add("▼", "아래로", [this] { doc_->moveLayer(doc_->currentLayer(), -1); });
    lay->addLayout(row);

    connect(doc_, &Document::structureChanged, this, &LayerStackPanel::rebuild);
    connect(doc_, &Document::currentChanged, this, &LayerStackPanel::syncSelection);
    connect(list_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0) return;
        doc_->setCurrentLayer(doc_->layerCount() - 1 - row);
    });
    connect(list_, &QListWidget::itemChanged, this, [this](QListWidgetItem* it) {
        const int l = it->data(Qt::UserRole).toInt();
        if (l < 0 || l >= doc_->layerCount()) return;
        Layer& L = doc_->layer(l);
        L.name = it->text();
        L.visible = it->checkState() == Qt::Checked;
        doc_->notifyChanged();
    });
    connect(opacity_, &QSlider::valueChanged, this, [this](int v) {
        doc_->layer(doc_->currentLayer()).opacity = v / 100.0;
        doc_->notifyChanged();
    });
    rebuild();
}

void LayerStackPanel::rebuild() {
    QSignalBlocker b1(list_);
    list_->clear();
    for (int i = doc_->layerCount() - 1; i >= 0; --i) {
        const Layer& L = doc_->layer(i);
        auto* it = new QListWidgetItem(L.name);
        it->setData(Qt::UserRole, i);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEditable);
        it->setCheckState(L.visible ? Qt::Checked : Qt::Unchecked);
        QString tip;
        if (L.folder >= 0 && L.folder < doc_->folderCount()) tip += QString("폴더: %1\n").arg(doc_->folder(L.folder).name);
        if (L.ctgLines) tip += QStringLiteral("CTG 선화 (아래 레이어에 색 채우기)\n");
        if (!L.fx.isEmpty()) tip += QString("FX %1개").arg(L.fx.size());
        it->setToolTip(tip.trimmed());
        if (L.ctgLines) it->setForeground(QColor(0xff, 0xcc, 0x66));
        list_->addItem(it);
    }
    syncSelection();
}

void LayerStackPanel::syncSelection() {
    QSignalBlocker b1(list_);
    QSignalBlocker b2(opacity_);
    list_->setCurrentRow(doc_->layerCount() - 1 - doc_->currentLayer());
    opacity_->setValue(int(doc_->layer(doc_->currentLayer()).opacity * 100 + 0.5));
}

// ---------------- TimesheetPanel ----------------
TimesheetPanel::TimesheetPanel(Document* doc, QWidget* parent) : QWidget(parent), doc_(doc) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(2, 2, 2, 2);
    table_ = new QTableWidget;
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    table_->verticalHeader()->setDefaultSectionSize(18);
    table_->horizontalHeader()->setDefaultSectionSize(70);
    lay->addWidget(table_);

    debounce_.setSingleShot(true);
    debounce_.setInterval(150);
    connect(&debounce_, &QTimer::timeout, this, &TimesheetPanel::rebuild);
    connect(doc_, &Document::structureChanged, this, [this] { debounce_.start(); });
    connect(doc_, &Document::changed, this, [this] { debounce_.start(); });
    connect(doc_, &Document::currentChanged, this, [this] { debounce_.start(); });
    connect(table_, &QTableWidget::cellClicked, this, [this](int r, int c) {
        if (syncing_) return;
        doc_->setCurrentFrame(r);
        doc_->setCurrentLayer(doc_->layerCount() - 1 - c);
    });
    rebuild();
}

void TimesheetPanel::rebuild() {
    syncing_ = true;
    const int frames = doc_->frameCount();
    const int layers = doc_->layerCount();
    table_->clear();
    table_->setRowCount(frames);
    table_->setColumnCount(layers);
    QStringList heads;
    for (int c = 0; c < layers; ++c) heads << doc_->layer(layers - 1 - c).name;
    table_->setHorizontalHeaderLabels(heads);
    QStringList rows;
    for (int r = 0; r < frames; ++r) rows << QString::number(r + 1);
    table_->setVerticalHeaderLabels(rows);
    for (int c = 0; c < layers; ++c) {
        const int l = layers - 1 - c;
        int key = -1;
        for (int r = 0; r < frames; ++r) {
            if (!doc_->layer(l).frames[r].isNull()) key = r;
            auto* it = new QTableWidgetItem(key < 0 ? QString() : (key == r ? QString::number(key + 1) : QStringLiteral("│")));
            it->setTextAlignment(Qt::AlignCenter);
            if (r == doc_->currentFrame()) it->setBackground(QColor(0x2f, 0x45, 0x70));
            table_->setItem(r, c, it);
        }
    }
    syncing_ = false;
}

// ---------------- FxPanel ----------------
FxPanel::FxPanel(Document* doc, QWidget* parent) : QWidget(parent), doc_(doc) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    title_ = new QLabel;
    lay->addWidget(title_);
    list_ = new QListWidget;
    lay->addWidget(list_, 1);
    auto* row = new QHBoxLayout;
    type_ = new QComboBox;
    for (auto t : {Effects::Type::Invert, Effects::Type::Grayscale, Effects::Type::Brightness, Effects::Type::Blur})
        type_->addItem(Effects::typeName(t), int(t));
    param_ = new QSpinBox;
    param_->setRange(-100, 100);
    param_->setValue(4);
    param_->setToolTip(QStringLiteral("밝기: -100~100 / 블러: 반경 1~16"));
    row->addWidget(type_);
    row->addWidget(param_);
    lay->addLayout(row);
    auto* row2 = new QHBoxLayout;
    auto* add = new QToolButton;
    add->setText(QStringLiteral("추가"));
    auto* del = new QToolButton;
    del->setText(QStringLiteral("삭제"));
    row2->addWidget(add);
    row2->addWidget(del);
    row2->addStretch();
    lay->addLayout(row2);

    connect(add, &QToolButton::clicked, this, [this] {
        Effects::Item item;
        item.type = Effects::Type(type_->currentData().toInt());
        item.param = param_->value();
        if (item.type == Effects::Type::Blur) item.param = std::clamp(item.param, 1, 16);
        doc_->addFx(doc_->currentLayer(), item);
    });
    connect(del, &QToolButton::clicked, this, [this] {
        doc_->removeFx(doc_->currentLayer(), list_->currentRow());
    });
    connect(doc_, &Document::structureChanged, this, &FxPanel::rebuild);
    connect(doc_, &Document::currentChanged, this, &FxPanel::rebuild);
    rebuild();
}

void FxPanel::rebuild() {
    const Layer& L = doc_->layer(doc_->currentLayer());
    title_->setText(QString("레이어: %1").arg(L.name));
    list_->clear();
    for (const auto& fx : L.fx) {
        QString s = Effects::typeName(fx.type);
        if (fx.type == Effects::Type::Brightness || fx.type == Effects::Type::Blur) s += QString(" (%1)").arg(fx.param);
        list_->addItem(s);
    }
}

// ---------------- CameraPanel ----------------
CameraPanel::CameraPanel(Document* doc, QWidget* parent) : QWidget(parent), doc_(doc) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    enable_ = new QCheckBox(QStringLiteral("카메라 사용"));
    lay->addWidget(enable_);
    auto* form = new QGridLayout;
    auto mk = [&](const QString& label, double lo, double hi, double step, QDoubleSpinBox*& sb, int row) {
        sb = new QDoubleSpinBox;
        sb->setRange(lo, hi);
        sb->setSingleStep(step);
        sb->setDecimals(1);
        form->addWidget(new QLabel(label), row, 0);
        form->addWidget(sb, row, 1);
    };
    mk("X 이동", -8192, 8192, 10, x_, 0);
    mk("Y 이동", -8192, 8192, 10, y_, 1);
    mk("배율(%)", 10, 1000, 5, scale_, 2);
    mk("회전(°)", -720, 720, 1, rot_, 3);
    scale_->setValue(100);
    lay->addLayout(form);
    info_ = new QLabel;
    info_->setStyleSheet("color:#9aa0a6;");
    lay->addWidget(info_);
    auto* del = new QToolButton;
    del->setText(QStringLiteral("현재 프레임 키 삭제"));
    lay->addWidget(del);
    lay->addStretch();

    connect(enable_, &QCheckBox::toggled, this, [this](bool on) {
        if (!syncing_) doc_->setCameraEnabled(on);
    });
    for (QDoubleSpinBox* sb : {x_, y_, scale_, rot_})
        connect(sb, &QDoubleSpinBox::valueChanged, this, [this](double) { apply(); });
    connect(del, &QToolButton::clicked, this, [this] { doc_->removeCameraKey(doc_->currentFrame()); });
    connect(doc_, &Document::cameraChanged, this, &CameraPanel::sync);
    connect(doc_, &Document::currentChanged, this, &CameraPanel::sync);
    connect(doc_, &Document::structureChanged, this, &CameraPanel::sync);
    sync();
}

void CameraPanel::setEnabledChecked(bool on) {
    if (enable_->isChecked() != on) enable_->setChecked(on);
}

void CameraPanel::apply() {
    if (syncing_) return;
    if (!doc_->cameraEnabled()) doc_->setCameraEnabled(true);
    CameraKey k;
    k.x = x_->value();
    k.y = y_->value();
    k.scale = scale_->value() / 100.0;
    k.rotation = rot_->value();
    doc_->setCameraKey(doc_->currentFrame(), k);
}

void CameraPanel::sync() {
    syncing_ = true;
    const CameraKey k = doc_->cameraAt(doc_->currentFrame());
    enable_->setChecked(doc_->cameraEnabled());
    x_->setValue(k.x);
    y_->setValue(k.y);
    scale_->setValue(k.scale * 100.0);
    rot_->setValue(k.rotation);
    info_->setText(QString("키 %1개 · 현재 프레임: %2")
                       .arg(doc_->cameraKeys().size())
                       .arg(doc_->hasCameraKey(doc_->currentFrame()) ? "키" : "보간"));
    syncing_ = false;
}

// ---------------- LibraryPanel ----------------
LibraryPanel::LibraryPanel(Document* doc, QWidget* parent) : QWidget(parent), doc_(doc) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    list_ = new QListWidget;
    list_->setViewMode(QListView::IconMode);
    list_->setIconSize(QSize(64, 48));
    list_->setResizeMode(QListView::Adjust);
    list_->setMovement(QListView::Static);
    lay->addWidget(list_, 1);
    auto* row = new QHBoxLayout;
    auto add = [&](const QString& t, const QString& tip, auto fn) {
        auto* b = new QToolButton;
        b->setText(t);
        b->setToolTip(tip);
        connect(b, &QToolButton::clicked, this, fn);
        row->addWidget(b);
    };
    add("추가", "현재 이미지를 라이브러리에 추가", [this] {
        const int k = doc_->keyIndexAt(doc_->currentLayer(), doc_->currentFrame());
        if (k < 0) return;
        doc_->addToLibrary(QString("이미지 %1").arg(doc_->libraryCount() + 1), doc_->keyImage(doc_->currentLayer(), k));
    });
    add("삽입", "선택한 이미지를 현재 프레임에 삽입", [this] {
        const int i = list_->currentRow();
        if (i < 0) return;
        const int l = doc_->currentLayer(), f = doc_->currentFrame();
        if (doc_->layer(l).locked) return;
        QImage img = doc_->libraryImage(i);
        if (img.size() != doc_->size()) img = img.scaled(doc_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QImage full(doc_->size(), QImage::Format_ARGB32_Premultiplied);
        full.fill(Qt::transparent);
        {
            QPainter p(&full);
            p.drawImage((full.width() - img.width()) / 2, (full.height() - img.height()) / 2, img);
        }
        doc_->pushKeyEdit(l, f, doc_->layer(l).frames[f], full, QStringLiteral("라이브러리 이미지 삽입"));
    });
    add("삭제", "라이브러리에서 삭제", [this] { doc_->removeFromLibrary(list_->currentRow()); });
    row->addStretch();
    lay->addLayout(row);
    connect(doc_, &Document::libraryChanged, this, &LibraryPanel::rebuild);
    rebuild();
}

void LibraryPanel::rebuild() {
    list_->clear();
    for (int i = 0; i < doc_->libraryCount(); ++i) {
        QImage thumb = doc_->libraryImage(i).scaled(64, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QImage bg(thumb.size(), QImage::Format_ARGB32_Premultiplied);
        bg.fill(Qt::white);
        QPainter p(&bg);
        p.drawImage(0, 0, thumb);
        p.end();
        auto* it = new QListWidgetItem(QIcon(QPixmap::fromImage(bg)), doc_->libraryName(i));
        list_->addItem(it);
    }
}

// ---------------- PlaceholderPanel ----------------
PlaceholderPanel::PlaceholderPanel(const QString& note, QWidget* parent) : QWidget(parent) {
    auto* lay = new QVBoxLayout(this);
    auto* l = new QLabel(note);
    l->setWordWrap(true);
    l->setStyleSheet("color:#9aa0a6;");
    lay->addWidget(l);
    lay->addStretch();
}
