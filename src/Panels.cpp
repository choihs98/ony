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

namespace {
QToolButton* makeBtn(const QString& text, const QString& tip, bool checkable = true) {
    auto* b = new QToolButton;
    b->setText(text);
    b->setToolTip(tip);
    b->setCheckable(checkable);
    b->setFixedSize(36, 34);
    return b;
}
}

// ---------------- MainPanel ----------------
MainPanel::MainPanel(QWidget* parent) : QWidget(parent) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);

    struct Item { const char* label; const char* tip; };
    const Item items[] = {
        {"획", "획 (자유 곡선)"},          {"선", "선"},
        {"□", "사각형"},                   {"○", "원"},
        {"스플", "스플라인 (클릭으로 점 추가, 더블클릭/Enter 로 확정)"}, {"채우", "페인트통 (CTG 선화 레이어에서는 CTG 채우기)"},
        {"선택", "선택 / 이동"},            {"리깅", "리깅 / 핀 워프 (클릭=핀, Shift+클릭=고정 핀, Enter=적용)"},
        {"자르", "자르기 / 선택"},          {"변형", "변형 (변형 대화상자 열기). 화면 이동: 스페이스 / 가운데 버튼"},
    };
    auto* grid = new QGridLayout;
    grid->setSpacing(2);
    auto* group = new QButtonGroup(this);
    group->setExclusive(true);
    int i = 0;
    for (const auto& it : items) {
        auto* b = makeBtn(QString::fromUtf8(it.label), QString::fromUtf8(it.tip));
        group->addButton(b, i);
        grid->addWidget(b, i / 3, i % 3);
        if (i == 0) b->setChecked(true);
        ++i;
    }
    connect(group, &QButtonGroup::idClicked, this, &MainPanel::toolSelected);
    auto* cam = makeBtn("카메", "카메라 켜기/끄기");
    connect(cam, &QToolButton::toggled, this, &MainPanel::cameraToggled);
    grid->addWidget(cam, i / 3, i % 3);
    lay->addLayout(grid);

    auto* row = new QGridLayout;
    row->setSpacing(2);
    auto* clear = makeBtn("지움", "현재 이미지 / 선택 영역 지우기", false);
    connect(clear, &QToolButton::clicked, this, &MainPanel::clearRequested);
    row->addWidget(clear, 0, 0);
    auto* smooth = makeBtn("보정", "선 보정 켜기/끄기");
    connect(smooth, &QToolButton::toggled, this, &MainPanel::smoothingToggled);
    row->addWidget(smooth, 0, 1);
    auto* hist = makeBtn("기록", "브러시 기록", false);
    connect(hist, &QToolButton::clicked, this, [this, hist] { emit historyRequested(hist->mapToGlobal(QPoint(0, hist->height()))); });
    row->addWidget(hist, 0, 2);
    auto* paper = makeBtn("종이", "종이 질감 켜기/끄기");
    connect(paper, &QToolButton::toggled, this, &MainPanel::paperToggled);
    row->addWidget(paper, 1, 0);
    auto* stencil = makeBtn("스텐", "스텐실: 선택 영역 안에서만 그리기");
    connect(stencil, &QToolButton::toggled, this, &MainPanel::stencilToggled);
    row->addWidget(stencil, 1, 1);
    lay->addLayout(row);
    lay->addStretch();
}

// ---------------- ColorPanel ----------------
ColorPanel::ColorPanel(QWidget* parent) : QWidget(parent) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    auto* row = new QHBoxLayout;
    btnA_ = new QToolButton;
    btnA_->setFixedSize(70, 36);
    btnA_->setToolTip(QStringLiteral("A 색상 (도구에 사용)"));
    btnB_ = new QToolButton;
    btnB_->setFixedSize(70, 36);
    btnB_->setToolTip(QStringLiteral("B 색상"));
    row->addWidget(btnA_);
    row->addWidget(btnB_);
    row->addStretch();
    lay->addLayout(row);

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
        b->setFixedSize(22, 22);
        b->setStyleSheet(QString("background:%1;border:1px solid #666;").arg(c.name()));
        connect(b, &QToolButton::clicked, this, [this, c] { setColorA(c); });
        sw->addWidget(b, i / 4, i % 4);
        ++i;
    }
    lay->addLayout(sw);
    auto* hint = new QLabel(QStringLiteral("N: A/B 색상 교환"));
    hint->setStyleSheet("color:#9aa0a6;");
    lay->addWidget(hint);
    lay->addStretch();

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
    btnA_->setStyleSheet(QString("background:%1;border:2px solid #4a90e2;").arg(a_.name()));
    btnB_->setStyleSheet(QString("background:%1;border:1px solid #888;").arg(b_.name()));
}

// ---------------- ToolPanel ----------------
ToolPanel::ToolPanel(QWidget* parent) : QWidget(parent) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    list_ = new QListWidget;
    for (int i = 0; i < brushPresetCount(); ++i) list_->addItem(QString::fromUtf8(brushPreset(i).name));
    list_->setCurrentRow(0);
    list_->setMaximumHeight(130);
    lay->addWidget(list_);

    auto makeSlider = [&](const QString& label, int lo, int hi, int val, QSlider*& s, QLabel*& l) {
        l = new QLabel;
        s = new QSlider(Qt::Horizontal);
        s->setRange(lo, hi);
        s->setValue(val);
        lay->addWidget(l);
        lay->addWidget(s);
        Q_UNUSED(label);
    };
    makeSlider("크기", 1, 200, 6, size_, sizeL_);
    makeSlider("불투명도", 1, 100, 100, opacity_, opacityL_);
    makeSlider("경도", 0, 100, 85, hardness_, hardnessL_);
    pressure_ = new QCheckBox(QStringLiteral("필압 사용"));
    lay->addWidget(pressure_);
    lay->addStretch();

    auto updateLabels = [this] {
        sizeL_->setText(QString("크기: %1").arg(size_->value()));
        opacityL_->setText(QString("불투명도: %1%").arg(opacity_->value()));
        hardnessL_->setText(QString("경도: %1%").arg(hardness_->value()));
    };
    updateLabels();

    connect(size_, &QSlider::valueChanged, this, [this, updateLabels](int) { updateLabels(); emitParams(); });
    connect(opacity_, &QSlider::valueChanged, this, [this, updateLabels](int) { updateLabels(); emitParams(); });
    connect(hardness_, &QSlider::valueChanged, this, [this, updateLabels](int) { updateLabels(); emitParams(); });
    connect(pressure_, &QCheckBox::toggled, this, [this](bool) { emitParams(); });
    connect(list_, &QListWidget::currentRowChanged, this, [this, updateLabels](int row) {
        if (row < 0) return;
        // 프리셋의 기본 크기/불투명도/경도/필압을 적용한다
        const BrushPreset& pr = brushPreset(row);
        {
            QSignalBlocker b1(opacity_), b2(hardness_), b3(size_), b4(pressure_);
            size_->setValue(pr.size);
            opacity_->setValue(pr.opacity);
            hardness_->setValue(pr.hardness);
            pressure_->setChecked(pr.pressure);
        }
        updateLabels();
        emit paramsChanged(params());
        emit brushSelected(row);
    });
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
    QSignalBlocker b1(size_), b2(opacity_), b3(hardness_), b4(pressure_);
    size_->setValue(p.size);
    opacity_->setValue(p.opacity);
    hardness_->setValue(p.hardness);
    pressure_->setChecked(p.pressure);
    sizeL_->setText(QString("크기: %1").arg(p.size));
    opacityL_->setText(QString("불투명도: %1%").arg(p.opacity));
    hardnessL_->setText(QString("경도: %1%").arg(p.hardness));
}

void ToolPanel::setRow(int row) {
    QSignalBlocker b(list_);
    list_->setCurrentRow(row);
}

void ToolPanel::selectPreset(int row) {
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
