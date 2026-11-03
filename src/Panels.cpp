#include "Panels.h"
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QFormLayout>
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
#include <QSettings>
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

// 브러시 크기를 지정한 만큼 늘리거나 줄인다 (단축키 [ ] 용)
void ToolPanel::bumpSize(int delta) {
    size_->setValue(std::clamp(size_->value() + delta, size_->minimum(), size_->maximum()));
}

// 메인 도구 패널을 만든다 (도구 버튼 격자 + 카메라/지우기/보정/기록/종이/스텐실 단추)
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
        {"pick", "색 추출 (스포이트). 다른 도구에서도 Alt+클릭"},
        {"lasso", "올가미 선택 (끌어서 그리기). Shift=더하기, Alt=빼기"},
        {"polygon", "다각형 선택 (클릭으로 점 추가, 첫 점을 다시 클릭하면 완성)"},
        {"wand", "마술봉 선택 (비슷한 색 영역). Shift=더하기, Alt=빼기"},
        {"gradient", "그라디언트 (A 색 → B 색, 끌어서 방향 지정)"},
        {"text", "글자 넣기 (도구 패널 옵션에서 내용 입력)"},
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

// 색상환의 중심 좌표를 계산한다
QPointF ColorWheel::center() const { return QPointF(width() / 2.0, height() / 2.0); }
// 색상환의 바깥 반지름(색조 고리)을 계산한다
double ColorWheel::outer() const { return std::min(width(), height()) / 2.0 - 3; }
// 색상환의 안쪽 반지름(채도/명도 삼각형 경계)을 계산한다
double ColorWheel::inner() const { return outer() * 0.78; }

// 지금 선택된 색조/채도/명도 값으로 실제 QColor 를 만든다
QColor ColorWheel::color() const { return QColor::fromHsvF(std::fmod(h_, 360.0) / 360.0, std::clamp(s_, 0.0, 1.0), std::clamp(v_, 0.0, 1.0)); }

// 외부에서 전달받은 색으로 색조/채도/명도를 맞춘다
void ColorWheel::setColor(const QColor& c) {
    float h, s, v;
    c.getHsvF(&h, &s, &v);
    if (h >= 0) h_ = h * 360.0; // 무채색이면 색조 유지
    s_ = s;
    v_ = v;
    update();
}

// 채도/명도 삼각형의 세 꼭짓점(색조/흰색/검정) 좌표를 계산한다
void ColorWheel::triPoints(QPointF& pHue, QPointF& pWhite, QPointF& pBlack) const {
    const double r = inner() - 3;
    const double a = h_ * M_PI / 180.0;
    const QPointF c = center();
    auto at = [&](double ang) { return QPointF(c.x() + r * std::cos(ang), c.y() - r * std::sin(ang)); };
    pHue = at(a);
    pWhite = at(a + 2.0 * M_PI / 3.0);
    pBlack = at(a + 4.0 * M_PI / 3.0);
}

// 색상환 전체를 이미지로 미리 그려 캐시에 저장한다 (고리 + 삼각형, 매번 다시 그리면 느리다)
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

// 크기가 바뀌면 캐시를 무효화해서 다음에 다시 그리게 한다
void ColorWheel::resizeEvent(QResizeEvent*) { cacheH_ = -1; }

// 캐시된 색상환 이미지를 그리고, 그 위에 색조/채도·명도 위치 표시를 그린다
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

// 클릭/드래그 위치가 고리인지 삼각형인지 판단해서 색조 또는 채도·명도를 갱신한다
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

// 마우스 누름: 색상환 조작을 시작한다
void ColorWheel::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) handle(e->position(), true);
}
// 마우스 이동: 누른 채 끄는 동안 계속 색을 갱신한다
void ColorWheel::mouseMoveEvent(QMouseEvent* e) {
    if (e->buttons() & Qt::LeftButton) handle(e->position(), false);
}
// 마우스 뗌: 색상환 조작을 끝낸다
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

    swatchGrid_ = new QGridLayout;
    swatchGrid_->setSpacing(2);
    swatches_ = defaultPalette();
    lay->addLayout(swatchGrid_);
    rebuildSwatches();

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

// A 색상을 설정한다
void ColorPanel::setColorA(const QColor& c) {
    a_ = c;
    refresh();
    emit colorAChanged(a_);
}

// 기본 색상 견본 16개를 반환한다
QVector<QColor> ColorPanel::defaultPalette() {
    return {QColor("#000000"), QColor("#ffffff"), QColor("#808080"), QColor("#c0c0c0"),
            QColor("#e53935"), QColor("#fb8c00"), QColor("#fdd835"), QColor("#43a047"),
            QColor("#00acc1"), QColor("#1e88e5"), QColor("#5e35b1"), QColor("#d81b60"),
            QColor("#6d4c41"), QColor("#ffcc80"), QColor("#a5d6a7"), QColor("#90caf9")};
}

// 색상 견본 격자를 다시 그린다 (+ 버튼 포함)
void ColorPanel::rebuildSwatches() {
    if (!swatchGrid_) return;
    while (QLayoutItem* it = swatchGrid_->takeAt(0)) {
        if (it->widget()) it->widget()->deleteLater();
        delete it;
    }
    for (int i = 0; i < swatches_.size(); ++i) {
        const QColor c = swatches_[i];
        auto* b = new QToolButton;
        b->setFixedSize(16, 16);
        b->setStyleSheet(QString("background:%1;border:1px solid #333;").arg(c.name()));
        b->setToolTip(QString("%1\n왼쪽 클릭: 이 색 쓰기 / 오른쪽 클릭: 지금 A 색으로 바꾸기").arg(c.name().toUpper()));
        b->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(b, &QToolButton::clicked, this, [this, c] { setColorA(c); });
        connect(b, &QToolButton::customContextMenuRequested, this, [this, i] {
            if (i >= 0 && i < swatches_.size()) {
                swatches_[i] = a_;
                rebuildSwatches();
            }
        });
        swatchGrid_->addWidget(b, i / 8, i % 8);
    }
    auto* add = new QToolButton;
    add->setFixedSize(16, 16);
    add->setText("+");
    add->setToolTip(QStringLiteral("지금 A 색을 견본에 더하기"));
    connect(add, &QToolButton::clicked, this, [this] {
        swatches_.push_back(a_);
        rebuildSwatches();
    });
    swatchGrid_->addWidget(add, swatches_.size() / 8, swatches_.size() % 8);
}

// 색상 견본 목록을 통째로 바꾼다 (최대 200개, 비어 있으면 기본값 사용)
void ColorPanel::setSwatches(const QVector<QColor>& cols) {
    swatches_.clear();
    for (const QColor& c : cols)
        if (c.isValid() && swatches_.size() < 200) swatches_.push_back(c);
    if (swatches_.isEmpty()) swatches_ = defaultPalette();
    rebuildSwatches();
}

// A/B 색상을 서로 바꾼다
void ColorPanel::swapColors() {
    std::swap(a_, b_);
    refresh();
    emit colorAChanged(a_);
}

// A/B 색상 버튼·색상환·RGB 표시를 지금 값으로 갱신한다
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
    pressure_->setToolTip(QStringLiteral("태블릿 펜을 누르는 세기를 굵기·진하기에 반영합니다."));
    lay->addWidget(pressure_);

    // ---- 필압 조절: 무엇에 작용할지, 제일 약할 때의 값, 감도 ----
    prBox_ = new QWidget;
    auto* pl = new QVBoxLayout(prBox_);
    pl->setContentsMargins(14, 2, 0, 2);
    pl->setSpacing(3);
    auto* whatRow = new QHBoxLayout;
    whatRow->setSpacing(8);
    prSize_ = new QCheckBox(QStringLiteral("굵기"));
    prOpacity_ = new QCheckBox(QStringLiteral("진하기"));
    prSize_->setChecked(true);
    prOpacity_->setChecked(true);
    whatRow->addWidget(prSize_);
    whatRow->addWidget(prOpacity_);
    whatRow->addStretch();
    pl->addLayout(whatRow);

    auto prRow = [&](const QString& label, int lo, int hi, int val, QSlider*& s, QSpinBox*& v,
                     const QString& suffix, const QString& tip) {
        auto* row = new QHBoxLayout;
        row->setSpacing(4);
        auto* l = new QLabel(label);
        l->setMinimumWidth(l->fontMetrics().horizontalAdvance(QStringLiteral("최소 진하기")) + 4);
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
        s->setToolTip(tip);
        v->setToolTip(tip);
        l->setToolTip(tip);
        row->addWidget(l);
        row->addWidget(s, 1);
        row->addWidget(v);
        pl->addLayout(row);
    };
    prRow(QStringLiteral("최소 굵기"), 0, 100, 14, prMinSize_, prMinSizeV_, QStringLiteral("%"),
          QStringLiteral("가장 약하게 눌렀을 때의 굵기입니다. 0% 면 선이 점으로 사라지고, 100% 면 필압이 굵기에 영향을 주지 않습니다."));
    prRow(QStringLiteral("최소 진하기"), 0, 100, 12, prMinOp_, prMinOpV_, QStringLiteral("%"),
          QStringLiteral("가장 약하게 눌렀을 때의 진하기입니다."));
    prRow(QStringLiteral("감도"), -100, 100, -35, prCurve_, prCurveV_, QString(),
          QStringLiteral("음수로 갈수록 살짝만 눌러도 굵고 진하게 나오고, 양수로 갈수록 꾹 눌러야 나옵니다. 0 이 그대로입니다."));

    prPreview_ = new PressurePreview;
    prPreview_->setToolTip(QStringLiteral("왼쪽은 살짝 누른 쪽, 오른쪽은 꾹 누른 쪽입니다."));
    pl->addWidget(prPreview_);
    lay->addWidget(prBox_);
    prBox_->setEnabled(false);
    connect(pressure_, &QCheckBox::toggled, prBox_, &QWidget::setEnabled);
    // 지난번에 맞춰 둔 값을 되살린다 (프리셋을 바꿔도 이 설정은 그대로 둔다)
    {
        QSettings st("Ony", "Ony");
        // 한때 최소 굵기·진하기 기본값이 너무 낮아서 약하게 그으면 선이 끊겨 보였다.
        // 그때 값이 저장돼 있으면 한 번만 지금 기본값으로 되돌린다.
        if (st.value("pressure/ver", 1).toInt() < 2) {
            st.remove("pressure/minSize");
            st.remove("pressure/minOpacity");
            st.setValue("pressure/ver", 2);
        }
        QSignalBlocker d1(prSize_), d2(prOpacity_), d3(prMinSize_), d4(prMinSizeV_), d5(prMinOp_), d6(prMinOpV_),
            d7(prCurve_), d8(prCurveV_);
        prSize_->setChecked(st.value("pressure/size", true).toBool());
        prOpacity_->setChecked(st.value("pressure/opacity", true).toBool());
        const int ms = std::clamp(st.value("pressure/minSize", 14).toInt(), 0, 100);
        const int mo = std::clamp(st.value("pressure/minOpacity", 12).toInt(), 0, 100);
        const int cv = std::clamp(st.value("pressure/curve", -35).toInt(), -100, 100);
        prMinSize_->setValue(ms);
        prMinSizeV_->setValue(ms);
        prMinOp_->setValue(mo);
        prMinOpV_->setValue(mo);
        prCurve_->setValue(cv);
        prCurveV_->setValue(cv);
    }
    for (QCheckBox* c : {prSize_, prOpacity_})
        connect(c, &QCheckBox::toggled, this, [this](bool) { emitParams(); });
    const QVector<QPair<QSlider*, QSpinBox*>> prPairs{
        {prMinSize_, prMinSizeV_}, {prMinOp_, prMinOpV_}, {prCurve_, prCurveV_}};
    for (const auto& pr : prPairs) {
        connect(pr.first, &QSlider::valueChanged, pr.second, &QSpinBox::setValue);
        connect(pr.second, QOverload<int>::of(&QSpinBox::valueChanged), pr.first, &QSlider::setValue);
        connect(pr.first, &QSlider::valueChanged, this, [this](int) { emitParams(); });
    }
    auto* smoothRow = new QHBoxLayout;
    smoothLabel_ = new QLabel(QStringLiteral("보정 세기"));
    smooth_ = new QSlider(Qt::Horizontal);
    smooth_->setRange(1, 99);
    smooth_->setValue(60);
    smoothV_ = new QSpinBox;
    smoothV_->setRange(1, 99);
    smoothV_->setValue(60);
    smooth_->setToolTip(QStringLiteral("손떨림 보정 세기. 클수록 선이 매끄럽지만 커서를 늦게 따라옵니다.\n메인 패널의 보정 버튼을 켜야 효과가 있습니다."));
    smoothRow->addWidget(smoothLabel_);
    smoothRow->addWidget(smooth_, 1);
    smoothRow->addWidget(smoothV_);
    lay->addLayout(smoothRow);
    tilt_ = new QCheckBox(QStringLiteral("펜 기울기 사용 (붓이 납작해짐)"));
    tilt_->setToolTip(QStringLiteral("기울기를 지원하는 태블릿에서만 효과가 있습니다."));
    lay->addWidget(tilt_);
    customRow_ = new QComboBox;
    customRow_->addItem(QStringLiteral("브러시 모양: 기본"), -1);
    lay->addWidget(customRow_);
    // 그림 여러 장짜리(애니메이션) 브러시를 어떤 순서로 찍을지. 한 장짜리면 쓰이지 않는다.
    animMode_ = new QComboBox;
    animMode_->addItem(QStringLiteral("여러 장 브러시: 차례대로"), 0);
    animMode_->addItem(QStringLiteral("여러 장 브러시: 무작위"), 1);
    animMode_->setToolTip(QStringLiteral("그림 여러 장으로 만든 브러시를 찍을 때 다음 장을 고르는 방법입니다."));
    animMode_->setEnabled(false);
    lay->addWidget(animMode_);
    connect(animMode_, &QComboBox::currentIndexChanged, this, [this](int) { emitParams(); });
    connect(customRow_, &QComboBox::currentIndexChanged, this, [this](int) {
        const int id = customRow_->currentData().toInt();
        animMode_->setEnabled(customBrushFrameCount(id) > 1);
    });
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
    connect(tilt_, &QCheckBox::toggled, this, [this](bool) { emitParams(); });
    connect(smooth_, &QSlider::valueChanged, this, [this](int v) {
        QSignalBlocker b(smoothV_);
        smoothV_->setValue(v);
        emit smoothingStrengthChanged(v);
    });
    connect(smoothV_, &QSpinBox::valueChanged, this, [this](int v) { smooth_->setValue(v); });
    connect(customRow_, &QComboBox::currentIndexChanged, this, [this](int) { emitParams(); });
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
        prBox_->setEnabled(pr.pressure);   // 신호를 막아 둔 사이에도 같이 따라가게 한다
        syncIcons(row);
        emitParams();
        emit brushSelected(row);
    });
    syncIcons(0);
}

// 프리셋 목록 선택에 맞춰 아이콘 버튼들의 선택 표시를 맞춘다
void ToolPanel::syncIcons(int row) {
    for (int i = 0; i < icons_.size(); ++i) {
        QSignalBlocker b(icons_[i]);
        icons_[i]->setChecked(i == row);
    }
}

// 지금 화면에 설정된 브러시 값들을 모아 BrushParams 로 반환한다
BrushParams ToolPanel::params() const {
    BrushParams p;
    p.size = size_->value();
    p.opacity = opacity_->value();
    p.hardness = hardness_->value();
    p.pressure = pressure_->isChecked();
    p.prSize = prSize_->isChecked();
    p.prOpacity = prOpacity_->isChecked();
    p.prMinSize = prMinSize_->value();
    p.prMinOpacity = prMinOp_->value();
    p.prCurve = prCurve_->value();
    p.tilt = tilt_->isChecked();
    p.customBrush = customRow_ ? customRow_->currentData().toInt() : -1;
    p.animMode = animMode_ ? animMode_->currentData().toInt() : 0;
    p.preset = std::max(0, list_->currentRow());
    return p;
}

// 외부에서 받은 브러시 값으로 화면의 모든 조절 항목을 맞춘다
void ToolPanel::setParams(const BrushParams& p) {
    QSignalBlocker b1(size_), b2(opacity_), b3(hardness_), b4(pressure_), b5(sizeV_), b6(opacityV_), b7(hardnessV_);
    size_->setValue(p.size);
    sizeV_->setValue(p.size);
    opacity_->setValue(p.opacity);
    opacityV_->setValue(p.opacity);
    hardness_->setValue(p.hardness);
    hardnessV_->setValue(p.hardness);
    pressure_->setChecked(p.pressure);
    {
        QSignalBlocker c1(prSize_), c2(prOpacity_), c3(prMinSize_), c4(prMinSizeV_), c5(prMinOp_), c6(prMinOpV_),
            c7(prCurve_), c8(prCurveV_);
        prSize_->setChecked(p.prSize);
        prOpacity_->setChecked(p.prOpacity);
        prMinSize_->setValue(p.prMinSize);
        prMinSizeV_->setValue(p.prMinSize);
        prMinOp_->setValue(p.prMinOpacity);
        prMinOpV_->setValue(p.prMinOpacity);
        prCurve_->setValue(p.prCurve);
        prCurveV_->setValue(p.prCurve);
    }
    prBox_->setEnabled(p.pressure);
    if (prPreview_) prPreview_->setParams(p);
    QSignalBlocker b8(tilt_), b9(customRow_);
    tilt_->setChecked(p.tilt);
    const int ci = customRow_->findData(p.customBrush);
    customRow_->setCurrentIndex(ci >= 0 ? ci : 0);
}

// 사용자 브러시 목록을 다시 만든다 (브러시를 더하거나 지운 뒤 호출)
void ToolPanel::refreshCustomBrushes() {
    if (!customRow_) return;
    const int keep = customRow_->currentData().toInt();
    QSignalBlocker b(customRow_);
    customRow_->clear();
    customRow_->addItem(QStringLiteral("브러시 모양: 기본"), -1);
    for (int i = 0; i < customBrushCount(); ++i) customRow_->addItem(QStringLiteral("모양: ") + customBrushName(i), i);
    const int ci = customRow_->findData(keep);
    customRow_->setCurrentIndex(ci >= 0 ? ci : 0);
}

// 프리셋 목록에서 지정한 행을 선택 상태로 만든다 (아이콘도 함께 맞춘다)
void ToolPanel::setRow(int row) {
    QSignalBlocker b(list_);
    list_->setCurrentRow(row);
    syncIcons(row);
}

// 프리셋을 선택한다 (이미 선택된 프리셋이면 도구만 다시 맞춘다)
void ToolPanel::selectPreset(int row) {
    if (list_->currentRow() == row) { // 이미 선택된 프리셋이어도 도구(펜/지우개)는 다시 맞춘다
        emit brushSelected(row);
        return;
    }
    list_->setCurrentRow(row); // currentRowChanged 로 크기/불투명도 등도 함께 바뀐다
}

// 지금 값으로 신호를 보내고, 필압 설정은 다음에 켤 때도 쓰도록 저장해 둔다
void ToolPanel::emitParams() {
    const BrushParams p = params();
    if (prPreview_) prPreview_->setParams(p);
    if (prSize_) {   // 필압 설정은 다음에 켤 때도 그대로 쓰도록 적어 둔다
        QSettings st("Ony", "Ony");
        st.setValue("pressure/size", p.prSize);
        st.setValue("pressure/opacity", p.prOpacity);
        st.setValue("pressure/minSize", p.prMinSize);
        st.setValue("pressure/minOpacity", p.prMinOpacity);
        st.setValue("pressure/curve", p.prCurve);
        st.setValue("pressure/ver", 2);
    }
    emit paramsChanged(p);
}

// ---------------- PressurePreview ----------------
PressurePreview::PressurePreview(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(36);
}

// 미리보기에 쓸 브러시 값을 설정한다
void PressurePreview::setParams(const BrushParams& p) {
    p_ = p;
    update();
}

// 필압에 따라 굵기·진하기가 어떻게 바뀌는지 살짝→꾹 순서로 미리보기를 그린다
void PressurePreview::paintEvent(QPaintEvent*) {
    QPainter g(this);
    g.setRenderHint(QPainter::Antialiasing, true);
    const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
    g.setPen(QPen(QColor(0x50, 0x54, 0x58), 1));
    g.setBrush(QColor(0xf2, 0xf2, 0xf2));
    g.drawRoundedRect(r, 3, 3);
    if (r.width() < 12) return;

    // 왼쪽 끝(살짝 누름)에서 오른쪽 끝(꾹 누름)까지, 설정대로 굵기와 진하기를 바꿔 가며 칠한다
    const double cy = r.center().y();
    const double maxR = std::max(1.0, std::min(r.height() * 0.40, 11.0));
    const int n = int(r.width());
    g.setPen(Qt::NoPen);
    for (int i = 0; i < n; ++i) {
        const double t = n > 1 ? double(i) / (n - 1) : 1.0;
        double sm = 1.0, om = 1.0;
        pressureFactors(p_, t, &sm, &om);
        const double rad = std::max(0.4, maxR * sm);
        QColor c(0x20, 0x20, 0x20);
        c.setAlphaF(std::clamp(om * (p_.opacity / 100.0), 0.0, 1.0));
        g.setBrush(c);
        g.drawEllipse(QPointF(r.left() + i + 0.5, cy), rad, rad);
    }
    // 양 끝에 무엇을 뜻하는지 적어 둔다
    QFont f = g.font();
    f.setPointSizeF(std::max(7.0, f.pointSizeF() - 2.0));
    g.setFont(f);
    g.setPen(QColor(0x80, 0x80, 0x80));
    g.drawText(QRectF(r.left() + 3, r.top(), r.width() / 2, r.height()), Qt::AlignLeft | Qt::AlignTop,
               QStringLiteral("살짝"));
    g.drawText(QRectF(r.center().x(), r.top(), r.width() / 2 - 3, r.height()), Qt::AlignRight | Qt::AlignTop,
               QStringLiteral("꾹"));
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

// 레이어 목록을 문서 상태에 맞춰 다시 만든다
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

// 지금 선택된 레이어·불투명도 값에 맞춰 목록 선택과 슬라이더를 맞춘다
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

// 타임시트 표를 프레임·레이어 수에 맞춰 다시 만든다
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
    auto* cat = new QComboBox;
    cat->addItems(Effects::categories());
    type_ = new QComboBox;
    param_ = new QSpinBox;
    fxColor_ = new QToolButton;
    fxColor_->setFixedSize(24, 24);
    fxColor_->setToolTip(QStringLiteral("효과에 쓸 색"));
    fxColorValue_ = QColor(0, 200, 0);
    auto paintSwatch = [this] {
        fxColor_->setStyleSheet(QString("background:%1;border:1px solid #333;").arg(fxColorValue_.name()));
    };
    paintSwatch();
    connect(fxColor_, &QToolButton::clicked, this, [this, paintSwatch] {
        const QColor c = QColorDialog::getColor(fxColorValue_, this, QStringLiteral("효과에 쓸 색"));
        if (!c.isValid()) return;
        fxColorValue_ = c;
        paintSwatch();
    });
    auto syncType = [this] {
        const auto t = Effects::Type(type_->currentData().toInt());
        const auto pi = Effects::paramInfo(t);
        fxColor_->setVisible(Effects::usesColorParam(t));
        param_->setEnabled(pi.has);
        if (pi.has) {
            param_->setRange(pi.lo, pi.hi);
            param_->setValue(pi.def);
            param_->setToolTip(pi.label);
            param_->setPrefix(pi.label + QStringLiteral(" "));
        } else {
            param_->setPrefix(QString());
        }
    };
    auto fillTypes = [this, cat, syncType] {
        type_->clear();
        for (auto t : Effects::typesIn(cat->currentText())) type_->addItem(Effects::typeName(t), int(t));
        syncType();
    };
    connect(cat, &QComboBox::currentIndexChanged, this, fillTypes);
    connect(type_, &QComboBox::currentIndexChanged, this, syncType);
    fillTypes();
    row->addWidget(cat);
    row->addWidget(type_);
    row->addWidget(fxColor_);
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
        item.param = Effects::usesColorParam(item.type) ? Effects::packColorParam(fxColorValue_, param_->value())
                                                        : param_->value();
        doc_->addFx(doc_->currentLayer(), item);
    });
    connect(del, &QToolButton::clicked, this, [this] {
        doc_->removeFx(doc_->currentLayer(), list_->currentRow());
    });
    connect(doc_, &Document::structureChanged, this, &FxPanel::rebuild);
    connect(doc_, &Document::currentChanged, this, &FxPanel::rebuild);
    rebuild();
}

// 현재 레이어에 걸린 효과(FX) 목록을 다시 보여준다
void FxPanel::rebuild() {
    const Layer& L = doc_->layer(doc_->currentLayer());
    title_->setText(QString("레이어: %1").arg(L.name));
    list_->clear();
    for (const auto& fx : L.fx) {
        QString s = Effects::typeName(fx.type);
        if (Effects::usesColorParam(fx.type))
            s += QString(" (%1, 오차 %2)").arg(Effects::colorFromParam(fx.param).name().toUpper()).arg(Effects::strengthFromParam(fx.param));
        else if (Effects::paramInfo(fx.type).has)
            s += QString(" (%1)").arg(fx.param);
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
    ease_ = new QComboBox;
    ease_->addItems({"직선 (일정한 속도)", "느리게 시작", "느리게 끝", "느리게 시작 · 느리게 끝", "계단 (다음 키에서 한 번에 이동)"});
    ease_->setToolTip("현재 프레임의 키에서 다음 키까지 카메라가 움직이는 속도 곡선");
    auto* easeRow = new QFormLayout;
    easeRow->addRow(QStringLiteral("속도 곡선"), ease_);
    lay->addLayout(easeRow);
    keys_ = new QListWidget;
    keys_->setToolTip(QStringLiteral("카메라 키프레임 목록 (더블클릭: 그 프레임으로 이동)"));
    keys_->setMaximumHeight(110);
    lay->addWidget(keys_);
    lay->addStretch();
    connect(ease_, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (syncing_ || !doc_->hasCameraKey(doc_->currentFrame())) return;
        CameraKey k = doc_->cameraAt(doc_->currentFrame());
        k.ease = i;
        doc_->setCameraKey(doc_->currentFrame(), k);
    });
    connect(keys_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) { doc_->setCurrentFrame(it->data(Qt::UserRole).toInt()); });

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

// 켜짐 상태 체크박스를 외부 값에 맞춘다 (신호가 중복으로 안 나가게)
void CameraPanel::setEnabledChecked(bool on) {
    if (enable_->isChecked() != on) enable_->setChecked(on);
}

// 화면의 X/Y/배율/회전 값으로 현재 프레임에 카메라 키를 만든다
void CameraPanel::apply() {
    if (syncing_) return;
    if (!doc_->cameraEnabled()) doc_->setCameraEnabled(true);
    CameraKey k;
    k.x = x_->value();
    k.y = y_->value();
    k.scale = scale_->value() / 100.0;
    k.rotation = rot_->value();
    if (doc_->hasCameraKey(doc_->currentFrame())) k.ease = doc_->cameraAt(doc_->currentFrame()).ease; // 속도 곡선은 유지
    doc_->setCameraKey(doc_->currentFrame(), k);
}

// 현재 프레임의 카메라 값으로 화면 전체(조절값·키 목록)를 맞춘다
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
    ease_->setEnabled(doc_->hasCameraKey(doc_->currentFrame()));
    ease_->setCurrentIndex(doc_->hasCameraKey(doc_->currentFrame()) ? k.ease : 0);
    keys_->clear();
    static const char* easeName[] = {"직선", "느리게 시작", "느리게 끝", "느리게 시작·끝", "계단"};
    for (auto it = doc_->cameraKeys().cbegin(); it != doc_->cameraKeys().cend(); ++it) {
        auto* item = new QListWidgetItem(QString("프레임 %1   %2").arg(it.key() + 1).arg(QString::fromUtf8(easeName[std::clamp(it.value().ease, 0, 4)])));
        item->setData(Qt::UserRole, it.key());
        keys_->addItem(item);
        if (it.key() == doc_->currentFrame()) keys_->setCurrentItem(item);
    }
    syncing_ = false;
}

// ---------------- LibraryPanel ----------------
LibraryPanel::LibraryPanel(Document* doc, QWidget* parent) : QWidget(parent), doc_(doc) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    auto* top = new QHBoxLayout;
    search_ = new QLineEdit;
    search_->setPlaceholderText(QStringLiteral("이름 · 분류로 찾기"));
    search_->setClearButtonEnabled(true);
    cat_ = new QComboBox;
    cat_->setMinimumWidth(90);
    top->addWidget(search_, 1);
    top->addWidget(cat_);
    lay->addLayout(top);
    list_ = new QListWidget;
    list_->setViewMode(QListView::IconMode);
    list_->setIconSize(QSize(64, 48));
    list_->setResizeMode(QListView::Adjust);
    list_->setMovement(QListView::Static);
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    lay->addWidget(list_, 1);
    connect(search_, &QLineEdit::textChanged, this, [this] { rebuild(); });
    connect(cat_, &QComboBox::currentIndexChanged, this, [this] {
        if (!syncing_) rebuild();
    });
    connect(list_, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QListWidgetItem* it = list_->itemAt(pos);
        if (!it) return;
        const int idx = it->data(Qt::UserRole).toInt();
        QMenu menu(this);
        menu.addAction(QStringLiteral("이름 바꾸기..."), this, [this, idx] {
            bool ok = false;
            const QString n = QInputDialog::getText(this, QStringLiteral("이름 바꾸기"), QStringLiteral("이름:"),
                                                    QLineEdit::Normal, doc_->libraryName(idx), &ok);
            if (ok) doc_->setLibraryName(idx, n);
        });
        menu.addAction(QStringLiteral("분류 정하기..."), this, [this, idx] {
            QStringList cats = doc_->libraryCategories();
            cats.prepend(QStringLiteral("(분류 없음)"));
            bool ok = false;
            const QString cur = doc_->libraryCategory(idx);
            const QString s = QInputDialog::getItem(this, QStringLiteral("분류"),
                                                    QStringLiteral("분류 (직접 입력해도 됩니다):"), cats,
                                                    int(std::max<qsizetype>(0, cats.indexOf(cur))), true, &ok);
            if (!ok) return;
            doc_->setLibraryCategory(idx, s == QStringLiteral("(분류 없음)") ? QString() : s);
        });
        menu.addSeparator();
        menu.addAction(QStringLiteral("삭제"), this, [this, idx] { doc_->removeFromLibrary(idx); });
        menu.exec(list_->mapToGlobal(pos));
    });
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
        QListWidgetItem* it = list_->currentItem();
        if (!it) return;
        const int i = it->data(Qt::UserRole).toInt();
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
    add("삭제", "라이브러리에서 삭제", [this] {
        QListWidgetItem* it = list_->currentItem();
        if (it) doc_->removeFromLibrary(it->data(Qt::UserRole).toInt());
    });
    add("분류", "선택한 이미지의 분류를 정합니다", [this] {
        QListWidgetItem* it = list_->currentItem();
        if (!it) return;
        const int idx = it->data(Qt::UserRole).toInt();
        QStringList cats = doc_->libraryCategories();
        cats.prepend(QStringLiteral("(분류 없음)"));
        bool ok = false;
        const QString s = QInputDialog::getItem(this, QStringLiteral("분류"), QStringLiteral("분류 (직접 입력해도 됩니다):"),
                                                cats, int(std::max<qsizetype>(0, cats.indexOf(doc_->libraryCategory(idx)))), true, &ok);
        if (ok) doc_->setLibraryCategory(idx, s == QStringLiteral("(분류 없음)") ? QString() : s);
    });
    row->addStretch();
    lay->addLayout(row);
    connect(doc_, &Document::libraryChanged, this, &LibraryPanel::rebuild);
    rebuild();
}

// 검색어·분류 조건에 맞춰 라이브러리 목록을 다시 만든다
void LibraryPanel::rebuild() {
    const QString q = search_ ? search_->text().trimmed() : QString();
    const QString pickedCat = (cat_ && cat_->currentIndex() > 0) ? cat_->currentText() : QString();
    // 분류 목록 다시 만들기 (고른 분류는 유지)
    if (cat_) {
        syncing_ = true;
        const QString keep = cat_->currentIndex() > 0 ? cat_->currentText() : QString();
        cat_->clear();
        cat_->addItem(QStringLiteral("분류: 전체"));
        for (const QString& c : doc_->libraryCategories()) cat_->addItem(c);
        const int ki = keep.isEmpty() ? 0 : cat_->findText(keep);
        cat_->setCurrentIndex(ki >= 0 ? ki : 0);
        syncing_ = false;
    }
    list_->clear();
    int shown = 0;
    for (int i = 0; i < doc_->libraryCount(); ++i) {
        const QString name = doc_->libraryName(i);
        const QString c = doc_->libraryCategory(i);
        if (!pickedCat.isEmpty() && c != pickedCat) continue;
        if (!q.isEmpty() && !name.contains(q, Qt::CaseInsensitive) && !c.contains(q, Qt::CaseInsensitive)) continue;
        const QImage thumb = doc_->libraryImage(i).scaled(64, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        auto* item = new QListWidgetItem(QIcon(QPixmap::fromImage(thumb)), c.isEmpty() ? name : name + "\n[" + c + "]");
        item->setData(Qt::UserRole, i);
        item->setToolTip(c.isEmpty() ? name : QString("%1\n분류: %2").arg(name, c));
        list_->addItem(item);
        ++shown;
    }
    if (shown == 0 && doc_->libraryCount() > 0) {
        auto* item = new QListWidgetItem(QStringLiteral("(찾는 이미지가 없습니다)"));
        item->setFlags(Qt::NoItemFlags);
        list_->addItem(item);
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
