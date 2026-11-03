#include "CurvesDialog.h"
#include "Effects.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QLineF>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------- CurveWidget
CurveWidget::CurveWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(200, 200);
    setMouseTracking(true);
    reset();
}

// 곡선을 양 끝 점만 있는 기본(직선) 상태로 되돌린다
void CurveWidget::reset() {
    pts_ = {QPointF(0, 0), QPointF(1, 1)};
    update();
    emit curveChanged();
}

// 곡선 조절점들을 통째로 지정한다 (x 기준으로 정렬해 둔다)
void CurveWidget::setPoints(const QVector<QPointF>& p) {
    pts_ = p;
    if (pts_.size() < 2) reset();
    std::sort(pts_.begin(), pts_.end(), [](const QPointF& a, const QPointF& b) { return a.x() < b.x(); });
    update();
    emit curveChanged();
}

// 곡선 좌표(0~1 범위)를 위젯 화면 좌표로 변환한다
QPointF CurveWidget::toWidget(const QPointF& p) const {
    const double m = 8;
    return QPointF(m + p.x() * (width() - 2 * m), (height() - m) - p.y() * (height() - 2 * m));
}

// 위젯 화면 좌표를 곡선 좌표(0~1 범위)로 변환한다
QPointF CurveWidget::toCurve(const QPointF& p) const {
    const double m = 8;
    return QPointF(std::clamp((p.x() - m) / std::max(1.0, width() - 2 * m), 0.0, 1.0),
                   std::clamp(((height() - m) - p.y()) / std::max(1.0, height() - 2 * m), 0.0, 1.0));
}

// 단조 증가 3차 보간(점 사이를 매끄럽게 잇되 출렁이지 않게)
QVector<int> CurveWidget::lut() const {
    QVector<int> out(256);
    const int n = pts_.size();
    for (int i = 0; i < 256; ++i) {
        const double x = i / 255.0;
        double y = x;
        if (n >= 2) {
            if (x <= pts_.first().x()) {
                y = pts_.first().y();
            } else if (x >= pts_.last().x()) {
                y = pts_.last().y();
            } else {
                for (int k = 0; k + 1 < n; ++k) {
                    if (x >= pts_[k].x() && x <= pts_[k + 1].x()) {
                        const double dx = pts_[k + 1].x() - pts_[k].x();
                        const double t = dx < 1e-9 ? 0.0 : (x - pts_[k].x()) / dx;
                        const double s = t * t * (3.0 - 2.0 * t);   // 부드러운 계단
                        y = pts_[k].y() + (pts_[k + 1].y() - pts_[k].y()) * s;
                        break;
                    }
                }
            }
        }
        out[i] = std::clamp(int(std::round(y * 255.0)), 0, 255);
    }
    return out;
}

// 화면 좌표에서 가까운 조절점을 찾는다 (없으면 -1)
int CurveWidget::pickPoint(const QPointF& wp) const {
    for (int i = 0; i < pts_.size(); ++i)
        if (QLineF(toWidget(pts_[i]), wp).length() < 9.0) return i;
    return -1;
}

// 곡선·격자·히스토그램·조절점을 그린다
void CurveWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QPalette pal = palette();
    p.fillRect(rect(), pal.color(QPalette::Base));
    // 히스토그램
    if (!hist_.isEmpty()) {
        int mx = 1;
        for (int v : hist_) mx = std::max(mx, v);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(140, 150, 160, 90));
        for (int i = 0; i < hist_.size(); ++i) {
            const QPointF a = toWidget(QPointF(double(i) / std::max(1, int(hist_.size()) - 1), 0));
            const double hgt = double(hist_[i]) / mx * (height() - 16);
            p.drawRect(QRectF(a.x(), a.y() - hgt, std::max(1.0, double(width() - 16) / hist_.size()), hgt));
        }
    }
    // 격자
    p.setPen(QPen(pal.color(QPalette::Mid), 0));
    for (int i = 0; i <= 4; ++i) {
        const double t = i / 4.0;
        p.drawLine(toWidget(QPointF(t, 0)), toWidget(QPointF(t, 1)));
        p.drawLine(toWidget(QPointF(0, t)), toWidget(QPointF(1, t)));
    }
    // 곡선
    const QVector<int> l = lut();
    QPainterPath path;
    for (int i = 0; i < 256; ++i) {
        const QPointF w = toWidget(QPointF(i / 255.0, l[i] / 255.0));
        if (i == 0) path.moveTo(w);
        else path.lineTo(w);
    }
    p.setPen(QPen(pal.color(QPalette::Highlight), 2));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
    // 점
    p.setPen(QPen(pal.color(QPalette::WindowText), 1));
    p.setBrush(Qt::white);
    for (const QPointF& pt : pts_) p.drawEllipse(toWidget(pt), 4, 4);
}

// 마우스 클릭 처리: 빈 곳을 클릭하면 점 추가, 점을 클릭하면 선택(드래그 시작), 오른쪽 클릭은 점 삭제
void CurveWidget::mousePressEvent(QMouseEvent* e) {
    const QPointF wp = e->position();
    int i = pickPoint(wp);
    if (e->button() == Qt::RightButton) {   // 오른쪽 클릭 = 점 지우기 (양 끝은 못 지운다)
        if (i > 0 && i < pts_.size() - 1) {
            pts_.remove(i);
            update();
            emit curveChanged();
        }
        return;
    }
    if (i < 0) {
        const QPointF c = toCurve(wp);
        pts_.push_back(c);
        std::sort(pts_.begin(), pts_.end(), [](const QPointF& a, const QPointF& b) { return a.x() < b.x(); });
        i = pts_.indexOf(c);
    }
    drag_ = i;
    update();
}

// 드래그 중인 점을 마우스 위치로 옮긴다 (양 끝 점과 이웃 점 범위 안에서만)
void CurveWidget::mouseMoveEvent(QMouseEvent* e) {
    if (drag_ < 0 || drag_ >= pts_.size()) return;
    QPointF c = toCurve(e->position());
    // 양 끝 점은 가로로 못 움직인다
    if (drag_ == 0) c.setX(0);
    else if (drag_ == pts_.size() - 1) c.setX(1);
    else {
        const double lo = pts_[drag_ - 1].x() + 0.005, hi = pts_[drag_ + 1].x() - 0.005;
        c.setX(std::clamp(c.x(), lo, hi));
    }
    pts_[drag_] = c;
    update();
    emit curveChanged();
}

// 드래그 상태를 끝낸다
void CurveWidget::mouseReleaseEvent(QMouseEvent*) { drag_ = -1; }

// 더블클릭하면 곡선을 기본 상태로 되돌린다
void CurveWidget::mouseDoubleClickEvent(QMouseEvent*) { reset(); }

// ---------------------------------------------------------------- ColorAdjustDialog
ColorAdjustDialog::ColorAdjustDialog(const QImage& src, bool levelsMode, QWidget* parent)
    : QDialog(parent), src_(src), levelsMode_(levelsMode) {
    setWindowTitle(levelsMode ? QStringLiteral("레벨") : QStringLiteral("커브"));
    auto* lay = new QHBoxLayout(this);
    auto* left = new QVBoxLayout;
    auto* right = new QVBoxLayout;
    lay->addLayout(left);
    lay->addLayout(right);

    preview_ = new QLabel;
    preview_->setFixedSize(320, 240);
    preview_->setAlignment(Qt::AlignCenter);
    preview_->setStyleSheet("background:#303030;border:1px solid #222;");
    left->addWidget(new QLabel(QStringLiteral("미리보기")));
    left->addWidget(preview_);
    left->addStretch();

    // 히스토그램
    QVector<int> hist(64, 0);
    {
        const QImage s = src_.convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < s.height(); y += 2) {
            const QRgb* r = reinterpret_cast<const QRgb*>(s.constScanLine(y));
            for (int x = 0; x < s.width(); x += 2) {
                if (qAlpha(r[x]) < 8) continue;
                const int lum = (qRed(r[x]) * 299 + qGreen(r[x]) * 587 + qBlue(r[x]) * 114) / 1000;
                hist[std::clamp(lum * 64 / 256, 0, 63)]++;
            }
        }
    }

    if (!levelsMode_) {
        channel_ = new QComboBox;
        channel_->addItems({QStringLiteral("전체 (RGB)"), QStringLiteral("빨강"), QStringLiteral("초록"), QStringLiteral("파랑")});
        right->addWidget(channel_);
        curve_ = new CurveWidget;
        curve_->setHistogram(hist);
        right->addWidget(curve_);
        right->addWidget(new QLabel(QStringLiteral("클릭: 점 추가 / 끌기: 이동 / 오른쪽 클릭: 점 삭제 / 더블클릭: 초기화")));
        ptsAll_ = ptsR_ = ptsG_ = ptsB_ = {QPointF(0, 0), QPointF(1, 1)};
        connect(curve_, &CurveWidget::curveChanged, this, [this] {
            const int ch = channel_ ? channel_->currentIndex() : 0;
            QVector<QPointF>& dst = ch == 1 ? ptsR_ : ch == 2 ? ptsG_ : ch == 3 ? ptsB_ : ptsAll_;
            dst = curve_->points();
            recompute();
        });
        connect(channel_, &QComboBox::currentIndexChanged, this, [this](int ch) {
            const QVector<QPointF>& s = ch == 1 ? ptsR_ : ch == 2 ? ptsG_ : ch == 3 ? ptsB_ : ptsAll_;
            QSignalBlocker b(curve_);
            curve_->setPoints(s);
            curve_->update();
        });
    } else {
        auto* f = new QFormLayout;
        inLow_ = new QSpinBox; inLow_->setRange(0, 254); inLow_->setValue(0);
        inHigh_ = new QSpinBox; inHigh_->setRange(1, 255); inHigh_->setValue(255);
        gamma_ = new QDoubleSpinBox; gamma_->setRange(0.05, 10.0); gamma_->setSingleStep(0.05); gamma_->setValue(1.0); gamma_->setDecimals(2);
        outLow_ = new QSpinBox; outLow_->setRange(0, 255); outLow_->setValue(0);
        outHigh_ = new QSpinBox; outHigh_->setRange(0, 255); outHigh_->setValue(255);
        f->addRow(QStringLiteral("입력 어두운 쪽"), inLow_);
        f->addRow(QStringLiteral("입력 밝은 쪽"), inHigh_);
        f->addRow(QStringLiteral("감마 (가운데 밝기)"), gamma_);
        f->addRow(QStringLiteral("출력 어두운 쪽"), outLow_);
        f->addRow(QStringLiteral("출력 밝은 쪽"), outHigh_);
        auto* hw = new CurveWidget;
        hw->setHistogram(hist);
        hw->setEnabled(false);
        hw->setFixedHeight(120);
        right->addWidget(hw);
        right->addLayout(f);
        for (QSpinBox* s : {inLow_, inHigh_, outLow_, outHigh_}) connect(s, &QSpinBox::valueChanged, this, [this] { recompute(); });
        connect(gamma_, &QDoubleSpinBox::valueChanged, this, [this] { recompute(); });
    }

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Reset);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(bb->button(QDialogButtonBox::Reset), &QPushButton::clicked, this, [this] {
        if (curve_) {
            ptsAll_ = ptsR_ = ptsG_ = ptsB_ = {QPointF(0, 0), QPointF(1, 1)};
            curve_->setPoints(ptsAll_);
        }
        if (inLow_) {
            inLow_->setValue(0);
            inHigh_->setValue(255);
            gamma_->setValue(1.0);
            outLow_->setValue(0);
            outHigh_->setValue(255);
        }
        recompute();
    });
    right->addWidget(bb);
    recompute();
}

static QVector<int> lutFrom(const QVector<QPointF>& pts, const QVector<int>& base) {
    CurveWidget w;
    w.setPoints(pts);
    const QVector<int> l = w.lut();
    QVector<int> out(256);
    for (int i = 0; i < 256; ++i) out[i] = l[std::clamp(base[i], 0, 255)];
    return out;
}

// 빨강 채널에 적용할 변환표(LUT)를 계산한다
QVector<int> ColorAdjustDialog::lutR() const {
    QVector<int> base(256);
    for (int i = 0; i < 256; ++i) base[i] = i;
    if (!curve_) return base;
    CurveWidget a;
    a.setPoints(ptsAll_);
    return lutFrom(ptsR_, a.lut());
}

// 초록 채널에 적용할 변환표(LUT)를 계산한다
QVector<int> ColorAdjustDialog::lutG() const {
    QVector<int> base(256);
    for (int i = 0; i < 256; ++i) base[i] = i;
    if (!curve_) return base;
    CurveWidget a;
    a.setPoints(ptsAll_);
    return lutFrom(ptsG_, a.lut());
}

// 파랑 채널에 적용할 변환표(LUT)를 계산한다
QVector<int> ColorAdjustDialog::lutB() const {
    QVector<int> base(256);
    for (int i = 0; i < 256; ++i) base[i] = i;
    if (!curve_) return base;
    CurveWidget a;
    a.setPoints(ptsAll_);
    return lutFrom(ptsB_, a.lut());
}

// 현재 설정(레벨/커브)으로 결과 이미지를 다시 계산하고 미리보기를 갱신한다
void ColorAdjustDialog::recompute() {
    if (levelsMode_) {
        result_ = Effects::levels(src_, inLow_->value(), inHigh_->value(), gamma_->value(), outLow_->value(), outHigh_->value());
    } else {
        result_ = Effects::curves(src_, lutR(), lutG(), lutB());
    }
    if (preview_) {
        QImage shown(preview_->size(), QImage::Format_ARGB32_Premultiplied);
        shown.fill(QColor(0x30, 0x30, 0x30));
        QPainter p(&shown);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        const QImage sc = result_.scaled(preview_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        p.drawImage((shown.width() - sc.width()) / 2, (shown.height() - sc.height()) / 2, sc);
        p.end();
        preview_->setPixmap(QPixmap::fromImage(shown));
    }
}
