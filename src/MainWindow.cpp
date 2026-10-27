#include <cmath>
#include <functional>
#include <optional>
#include <algorithm>
#include "MainWindow.h"
#include "CanvasWidget.h"
#include "Document.h"
#include "Extras.h"
#include "Effects.h"
#include "Style.h"
#include "VideoExport.h"
#include "Print.h"
#include "CurvesDialog.h"
#include "Blend.h"
#include "Collab.h"
#include "Plugins.h"
#include "Psd.h"
#include "Panels.h"
#include "TimelineWidget.h"
#include "Warp.h"

#include <QApplication>
#include <QAudioOutput>
#include <QActionGroup>
#include <QCheckBox>
#include <QDesktopServices>
#include <QLineEdit>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMediaPlayer>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPageSize>
#include <QFontComboBox>
#include <QAbstractButton>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QThread>
#include <QTemporaryDir>
#include <QProgressDialog>
#include <QStringConverter>
#include <QTextStream>
#include <QPainter>
#include <QPalette>
#include <QPdfWriter>
#ifdef ONY_HAS_PRINT
#include <QPrintDialog>
#include <QPrinter>
#endif
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QCursor>
#include <QDateTime>
#include <QFile>
#include <QImageReader>
#include <QMouseEvent>
#include <QShortcut>
#include <QStandardItemModel>
#include <QColorDialog>
#include <QStandardPaths>
#include <QSpinBox>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QUndoStack>
#include <QUndoView>
#include "LayerMenu.h"
#include "Shortcuts.h"
#include "Privacy.h"
#include <QUrl>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), doc_(new Document(this)) {
    QSettings st("Ony", "Ony");
    applyTheme(st.value("theme", 0).toInt());
    doc_->undoStack()->setUndoLimit(st.value("undoLevels", 60).toInt());

    buildCentral();
    buildDocks();
    buildMenus();

    connect(doc_, &Document::modifiedChanged, this, &MainWindow::updateTitle);
    connect(doc_, &Document::structureChanged, this, [this] {
        updateTitle();
        updateInfo();
        fpsSpin_->blockSignals(true);
        fpsSpin_->setValue(doc_->fps());
        fpsSpin_->blockSignals(false);
        playTimer_.setInterval(std::max(1, int(1000 / (doc_->fps() * (speedBox_ ? speedBox_->currentData().toDouble() : 1.0)))));
    });
    connect(doc_, &Document::currentChanged, this, [this] {
        updateInfo();
        const int x = TimelineWidget::kNameW + doc_->currentFrame() * timeline_->cell();
        tlScroll_->ensureVisible(x, 0, 60, 0);
    });
    connect(&playTimer_, &QTimer::timeout, this, &MainWindow::stepPlay);
    playTimer_.setInterval(1000 / doc_->fps());

    auto& SC = Shortcuts::instance();
    SC.add(this, "색상", "A/B 색 바꾸기", QKeySequence(Qt::Key_X), [this] { color_->swapColors(); });
    SC.add(this, "타임라인", "프레임 추가", QKeySequence(Qt::CTRL | Qt::Key_Insert), [this] {
        doc_->insertFrame(doc_->currentFrame() + 1);
        gotoFrame(doc_->currentFrame() + 1);
    });
    SC.add(this, "타임라인", "이전 프레임", QKeySequence(Qt::Key_Left), [this] { gotoFrame(doc_->currentFrame() - 1); });
    SC.add(this, "타임라인", "다음 프레임", QKeySequence(Qt::Key_Right), [this] { nextFrameAction(false); });
    SC.add(this, "타임라인", "다음 이미지 (새 이미지 만들기)", QKeySequence("Shift+Right"), [this] { nextFrameAction(true); });
    SC.add(this, "타임라인", "재생 / 워프 적용", QKeySequence(Qt::Key_Return), [this] {
        if (canvas_->hasPendingWarp()) canvas_->commitWarp();
        else togglePlay();
    });

    updateTitle();
    updateInfo();
    {
        QSettings st("Ony", "Ony");
        const int room = std::clamp(st.value("start/room", 0).toInt(), 0, std::max(0, roomCombo_->count() - 1));
        QSignalBlocker rb(roomCombo_);
        roomCombo_->setCurrentIndex(room);
        applyRoomDefault(room);
        if (soundBtn_ && !st.value("sound/on", true).toBool()) soundBtn_->setChecked(false);
    }
    setupAutosave();
    collabOn_ = QSettings("Ony", "Ony").value("collab/on", false).toBool();
    statusBar()->showMessage(QStringLiteral("준비"));
}

// ---------------------------------------------------------------- 테마
void MainWindow::applyTheme(int index) {
    Style::apply(index);
    if (canvas_) canvas_->setBackdrop(Style::scheme(index).canvasBg);
}

void MainWindow::preferencesDialog() {
    QSettings st("Ony", "Ony");
    QDialog dlg(this);
    dlg.setWindowTitle("환경설정");
    auto* top = new QVBoxLayout(&dlg);
    auto* tabs = new QTabWidget;
    top->addWidget(tabs);
    // 일반
    auto* gen = new QWidget;
    auto* gf = new QFormLayout(gen);
    auto* undo = new QSpinBox;
    undo->setRange(5, 500);
    undo->setValue(st.value("undoLevels", 60).toInt());
    gf->addRow("실행 취소 단계", undo);
    tabs->addTab(gen, "일반");
    // 저장
    auto* sav = new QWidget;
    auto* sf = new QFormLayout(sav);
    auto* autosave = new QSpinBox;
    autosave->setRange(0, 120);
    autosave->setSpecialValueText("끔");
    autosave->setSuffix(" 분마다");
    autosave->setValue(st.value("save/autosaveMin", 5).toInt());
    auto* backups = new QSpinBox;
    backups->setRange(1, 99);
    backups->setSuffix(" 개");
    backups->setValue(st.value("save/backupCount", 10).toInt());
    sf->addRow("자동 백업", autosave);
    sf->addRow("저장할 때 보관할 백업 수", backups);
    sf->addRow(new QLabel("자동 백업은 작업 중인 프로젝트를 백업 폴더에 사본으로 저장합니다\n(원본 파일은 건드리지 않습니다)."));
    tabs->addTab(sav, "저장");
    // 화면
    auto* disp = new QWidget;
    auto* df = new QFormLayout(disp);
    auto* theme = new QComboBox;
    for (int i = 0; i < Style::schemeCount(); ++i) theme->addItem(Style::schemeName(i));
    theme->setCurrentIndex(st.value("theme", 0).toInt());
    df->addRow("색상 테마", theme);
    tabs->addTab(disp, "화면");
    // 인터페이스
    auto* itf = new QWidget;
    auto* iff = new QFormLayout(itf);
    auto* ll = new QCheckBox("왼쪽 서랍 잠금");
    ll->setChecked(leftLocked_);
    auto* rl = new QCheckBox("오른쪽 서랍 잠금");
    rl->setChecked(rightLocked_);
    iff->addRow(ll);
    iff->addRow(rl);
    tabs->addTab(itf, "인터페이스");
    // 메모리 · 캐시
    auto* mem = new QWidget;
    auto* mf = new QVBoxLayout(mem);
    int imgs = 0;
    qint64 bytes = 0;
    for (int l = 0; l < doc_->layerCount(); ++l)
        for (int f = 0; f < doc_->frameCount(); ++f)
            if (!doc_->layer(l).frames[f].isNull()) {
                ++imgs;
                bytes += doc_->layer(l).frames[f].sizeInBytes();
            }
    mf->addWidget(new QLabel(QString("현재 프로젝트 이미지: %1 개\n사용 메모리: 약 %2 MB").arg(imgs).arg(bytes / (1024 * 1024))));
    mf->addStretch();
    tabs->addTab(mem, "메모리 · 캐시");
    // 사운드
    auto* snd = new QWidget;
    auto* sdf = new QFormLayout(snd);
    auto* soundOn = new QCheckBox("프로그램을 시작할 때 소리 켜기");
    soundOn->setChecked(st.value("sound/on", true).toBool());
    sdf->addRow(soundOn);
    sdf->addRow(new QLabel("트랙별 음량·위치는 타임라인 아래 \"소리\" 버튼에서 설정합니다."));
    tabs->addTab(snd, "사운드");
    // 시작
    auto* sta = new QWidget;
    auto* stf = new QFormLayout(sta);
    auto* fullOnStart = new QCheckBox("켤 때 창 크기 기억하기 (끄면 늘 화면에 꽉 차게)");
    fullOnStart->setChecked(st.value("window/remember", true).toBool());
    auto* openLast = new QCheckBox("시작할 때 마지막 프로젝트 열기");
    openLast->setChecked(st.value("start/openLast", false).toBool());
    auto* startRoom = new QComboBox;
    for (int i = 0; i < roomCombo_->count(); ++i) startRoom->addItem(roomCombo_->itemText(i));
    startRoom->setCurrentIndex(std::clamp(st.value("start/room", 0).toInt(), 0, startRoom->count() - 1));
    stf->addRow(fullOnStart);
    stf->addRow(openLast);
    stf->addRow("시작 룸", startRoom);
    tabs->addTab(sta, "시작");
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    top->addWidget(bb);
    if (dlg.exec() != QDialog::Accepted) return;
    st.setValue("theme", theme->currentIndex());
    st.setValue("undoLevels", undo->value());
    st.setValue("save/autosaveMin", autosave->value());
    st.setValue("save/backupCount", backups->value());
    st.setValue("sound/on", soundOn->isChecked());
    st.setValue("start/openLast", openLast->isChecked());
    st.setValue("window/remember", fullOnStart->isChecked());
    if (!fullOnStart->isChecked()) st.remove("window/geometry");
    st.setValue("start/room", startRoom->currentIndex());
    setupAutosave();
    applyTheme(theme->currentIndex());
    doc_->undoStack()->setUndoLimit(undo->value());
    leftLocked_ = ll->isChecked();
    rightLocked_ = rl->isChecked();
}

// ---------------------------------------------------------------- 화면 구성
void MainWindow::buildCentral() {
    auto* central = new QWidget;
    auto* lay = new QVBoxLayout(central);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    // ---- 캔버스 위 정보줄 (TVPaint: 프로젝트 / 클립 / 확대율 / 각도 / 현재 레이어 / 현재 이미지)
    canvasInfo_ = new QLabel;
    canvasInfo_->setContentsMargins(8, 3, 8, 3);
    canvasInfo_->setStyleSheet("color:#a8a8a8;");
    lay->addWidget(canvasInfo_);

    canvas_ = new CanvasWidget(doc_);
    canvas_->setBackdrop(Style::scheme(QSettings("Ony", "Ony").value("theme", 0).toInt()).canvasBg);
    lay->addWidget(canvas_, 1);

    // ---- 재생 바 (문서 순서: 소리, 반복, 카메라 보기, FPS, | 재생 컨트롤 | 재중앙) ----
    auto* pb = new QWidget;
    auto* ph = new QHBoxLayout(pb);
    ph->setContentsMargins(6, 2, 6, 2);
    ph->setSpacing(3);
    auto addBtn = [&](const QString& iconName, const QString& tip, auto fn, bool checkable = false) {
        auto* b = new QToolButton;
        b->setIcon(Style::icon(iconName, 18));
        b->setIconSize(QSize(18, 18));
        b->setFixedSize(28, 24);
        b->setToolTip(tip);
        b->setCheckable(checkable);
        if (checkable) connect(b, &QToolButton::toggled, this, fn);
        else connect(b, &QToolButton::clicked, this, fn);
        ph->addWidget(b);
        return b;
    };
    auto* markIn = new QToolButton;
    markIn->setText(QStringLiteral("마크 인"));
    markIn->setToolTip(QStringLiteral("현재 프레임을 반복 구간의 시작으로 지정"));
    connect(markIn, &QToolButton::clicked, this, [this] {
        const int cur = doc_->currentFrame();
        const int b = (loopB_ >= cur) ? loopB_ : doc_->frameCount() - 1;
        timeline_->setRange(cur, b);
    });
    ph->addWidget(markIn);
    soundBtn_ = addBtn("sound", "소리 켜기/끄기", [this](bool on) {
        if (audioOut_) audioOut_->setMuted(!on);
    }, true);
    soundBtn_->setChecked(true);
    loopBox_ = addBtn("loop", "반복 재생 / 한 번만 재생", [](bool) {}, true);
    loopBox_->setChecked(true);
    pingBtn_ = addBtn("loop", "왕복 재생 (끝에서 거꾸로 되돌아옴)", [](bool) {}, true);
    pingBtn_->setText(QStringLiteral("↔"));
    speedBox_ = new QComboBox;
    for (double s : {0.25, 0.5, 1.0, 1.5, 2.0, 4.0}) speedBox_->addItem(QString("×%1").arg(s), s);
    speedBox_->setCurrentIndex(2);
    speedBox_->setToolTip(QStringLiteral("재생 속도 (프로젝트 프레임 속도에 곱해집니다)"));
    connect(speedBox_, &QComboBox::currentIndexChanged, this, [this] {
        playTimer_.setInterval(std::max(1, int(1000 / (doc_->fps() * speedBox_->currentData().toDouble()))));
    });
    ph->addWidget(speedBox_);
    camViewBtn_ = addBtn("camera", "카메라 보기로 재생", [this](bool on) {
        canvas_->setCameraView(on);
        if (camViewAct_ && camViewAct_->isChecked() != on) camViewAct_->setChecked(on);
    }, true);
    fpsSpin_ = new QSpinBox;
    fpsSpin_->setRange(1, 120);
    fpsSpin_->setValue(doc_->fps());
    fpsSpin_->setSuffix(QStringLiteral(" fps"));
    fpsSpin_->setButtonSymbols(QAbstractSpinBox::NoButtons);
    fpsSpin_->setFixedWidth(64);
    connect(fpsSpin_, &QSpinBox::valueChanged, this, [this](int v) { doc_->setFps(v); });
    ph->addWidget(fpsSpin_);
    scrubBtn_ = new QToolButton;
    scrubBtn_->setIcon(Style::icon("hand", 18));
    scrubBtn_->setIconSize(QSize(18, 18));
    scrubBtn_->setFixedSize(28, 24);
    scrubBtn_->setToolTip(QStringLiteral("누른 채 좌우로 끌면 재생 헤드를 부드럽게 이동 (놓으면 원위치)"));
    scrubBtn_->installEventFilter(this);
    ph->addWidget(scrubBtn_);
    ph->addStretch();
    addBtn("play", "재생 (Enter)", [this] {
        if (!playTimer_.isActive()) togglePlay();
    });
    addBtn("stop", "정지", [this] {
        if (playTimer_.isActive()) togglePlay();
    });
    addBtn("first", "첫 프레임", [this] { gotoFrame(0); });
    addBtn("last", "마지막 프레임", [this] { gotoFrame(doc_->frameCount() - 1); });
    addBtn("prevkey", "이전 이미지(키)", [this] {
        const int l = doc_->currentLayer(), f = doc_->currentFrame();
        int k = doc_->keyIndexAt(l, f);
        if (k >= 0 && k == f) {
            int p = -1;
            for (int i = f - 1; i >= 0; --i)
                if (!doc_->layer(l).frames[i].isNull()) { p = i; break; }
            if (p >= 0) gotoFrame(p);
        } else if (k >= 0) gotoFrame(k);
    });
    addBtn("nextkey", "다음 이미지(키)", [this] {
        const int n = doc_->nextKeyIndex(doc_->currentLayer(), doc_->currentFrame());
        if (n >= 0) gotoFrame(n);
    });
    addBtn("prev", "이전 프레임 (←)", [this] { gotoFrame(doc_->currentFrame() - 1); });
    addBtn("next", "다음 프레임 (→). Shift+→ 는 항상 새 이미지를 만들며 이동", [this] { nextFrameAction(false); });
    autoNewBtn_ = addBtn("new", "자동 새 이미지: 켜면 다음 프레임으로 넘어갈 때(→) 현재 레이어에 빈 새 이미지를 만들고, 마지막 프레임이면 프레임을 늘립니다", [](bool) {}, true);
    {
        auto* addF = new QToolButton;
        addF->setText(QStringLiteral("+프레임"));
        addF->setToolTip(QStringLiteral("현재 프레임 뒤에 빈 프레임 추가 (Ctrl+Ins)"));
        connect(addF, &QToolButton::clicked, this, [this] {
            doc_->insertFrame(doc_->currentFrame() + 1);
            gotoFrame(doc_->currentFrame() + 1);
        });
        ph->addWidget(addF);
    }
    ph->addStretch();
    addBtn("center", "재생 헤드를 타임라인 중앙으로", [this] {
        const int x = TimelineWidget::kNameW + doc_->currentFrame() * timeline_->cell();
        tlScroll_->horizontalScrollBar()->setValue(x - tlScroll_->viewport()->width() / 2);
    });
    zoomLabel_ = new QLabel(QStringLiteral("100%"));
    zoomLabel_->setMinimumWidth(56);
    zoomLabel_->setAlignment(Qt::AlignCenter);
    zoomLabel_->setStyleSheet("background:#35393a;border:1px solid #2c2c2c;padding:2px 4px;");
    ph->addWidget(zoomLabel_);
    loopModeBtn_ = new QToolButton;
    loopModeBtn_->setText(QStringLiteral("구간"));
    loopModeBtn_->setCheckable(true);
    loopModeBtn_->setToolTip(QStringLiteral("루프 모드: Shift+타임라인 위쪽 눈금을 끌어 구간 지정"));
    connect(loopModeBtn_, &QToolButton::toggled, this, [this](bool on) {
        if (on && loopA_ < 0) statusBar()->showMessage(QStringLiteral("타임라인 위쪽 눈금을 Shift+드래그해서 구간을 지정하세요."), 4000);
    });
    ph->addWidget(loopModeBtn_);
    auto* markOut = new QToolButton;
    markOut->setText(QStringLiteral("마크 아웃"));
    markOut->setToolTip(QStringLiteral("현재 프레임을 반복 구간의 끝으로 지정"));
    connect(markOut, &QToolButton::clicked, this, [this] {
        const int cur = doc_->currentFrame();
        const int a = (loopA_ >= 0 && loopA_ <= cur) ? loopA_ : 0;
        timeline_->setRange(a, cur);
    });
    ph->addWidget(markOut);
    lay->addWidget(pb);

    scrub_ = new ScrubBar(doc_);
    lay->addWidget(scrub_);
    wave_ = new WaveformBar(doc_);
    wave_->setVisible(false);
    lay->addWidget(wave_);

    // ---- 타임라인 위 줄: 레이어 조작 + 프로젝트/클립/레이어 정보 ----
    auto* strip = new QWidget;
    auto* sh = new QHBoxLayout(strip);
    sh->setContentsMargins(6, 2, 6, 2);
    sh->setSpacing(3);
    auto layerBtn = [&](const QString& t, const QString& tip, auto fn) {
        auto* b = new QToolButton;
        b->setText(t);
        b->setToolTip(tip);
        b->setMinimumWidth(b->fontMetrics().horizontalAdvance(t) + 14);
        connect(b, &QToolButton::clicked, this, fn);
        sh->addWidget(b);
    };
    layerBtn(QStringLiteral("새 레이어"), QStringLiteral("새 레이어"), [this] { doc_->addLayer(QString("레이어 %1").arg(doc_->layerCount() + 1)); });
    layerBtn(QStringLiteral("복제"), QStringLiteral("레이어 복제"), [this] { doc_->duplicateLayer(doc_->currentLayer()); });
    layerBtn(QStringLiteral("삭제"), QStringLiteral("레이어 삭제"), [this] { doc_->removeLayer(doc_->currentLayer()); });
    layerBtn(QStringLiteral("▲"), QStringLiteral("레이어를 위로"), [this] { doc_->moveLayer(doc_->currentLayer(), +1); });
    layerBtn(QStringLiteral("▼"), QStringLiteral("레이어를 아래로"), [this] { doc_->moveLayer(doc_->currentLayer(), -1); });
    sh->addWidget(new QLabel(QStringLiteral("불투명도")));
    layerOpacity_ = new QSlider(Qt::Horizontal);
    layerOpacity_->setRange(0, 100);
    layerOpacity_->setValue(100);
    layerOpacity_->setFixedWidth(110);
    sh->addWidget(layerOpacity_);
    connect(layerOpacity_, &QSlider::valueChanged, this, [this](int v) {
        doc_->layer(doc_->currentLayer()).opacity = v / 100.0;
        doc_->notifyChanged();
    });
    infoProject_ = new QLabel;
    infoClip_ = new QLabel;
    infoLayer_ = new QLabel;
    infoImage_ = new QLabel;
    for (QLabel* l : {infoProject_, infoClip_, infoLayer_}) l->setStyleSheet("color:#a8a8a8;");
    sh->addSpacing(12);
    sh->addWidget(infoProject_);
    sh->addWidget(infoClip_);
    sh->addWidget(infoLayer_);
    sh->addStretch();
    lay->addWidget(strip);

    // ---- 하단: 타임라인 (레이어 머리글 + 프레임 칸) ----
    timeline_ = new TimelineWidget(doc_);
    tlScroll_ = new QScrollArea;
    tlScroll_->setWidget(timeline_);
    tlScroll_->setWidgetResizable(true);
    tlScroll_->setFrameShape(QFrame::NoFrame);
    auto* split = new QSplitter(Qt::Horizontal);
    bottomSplit_ = split;
    split->addWidget(tlScroll_);
    split->setMinimumHeight(150);
    split->setMaximumHeight(460);
    lay->addWidget(split);

    // ---- 타임라인 아래 줄: 노트 / 소리 ----
    auto* tabsRow = new QWidget;
    auto* th = new QHBoxLayout(tabsRow);
    th->setContentsMargins(6, 2, 6, 2);
    th->setSpacing(3);
    auto tabBtn = [&](const QString& t, const QString& tip, auto fn) {
        auto* b = new QToolButton;
        b->setText(t);
        b->setToolTip(tip);
        b->setMinimumWidth(b->fontMetrics().horizontalAdvance(t) + 22);
        connect(b, &QToolButton::clicked, this, fn);
        th->addWidget(b);
    };
    tabBtn(QStringLiteral("노트"), QStringLiteral("스토리보드/노트 패널 열기"), [this] {
        dockStory_->show();
        dockStory_->raise();
    });
    tabBtn(QStringLiteral("소리"), QStringLiteral("소리 파일 불러오기"), [this] { loadAudio(); });
    tabBtn(QStringLiteral("파형"), QStringLiteral("소리 파형 보이기/숨기기"), [this] {
        waveVisible_ = !waveVisible_;
        updateWaveform();
        if (wave_) wave_->setVisible(waveVisible_ && wave_->hasPeaks());
        if (waveVisible_ && (!wave_ || !wave_->hasPeaks()))
            statusBar()->showMessage(QStringLiteral("먼저 \"소리\" 로 소리 파일을 불러오세요."), 4000);
    });
    tabBtn(QStringLiteral("타임시트"), QStringLiteral("타임시트 패널 열기"), [this] {
        dockTimesheet_->show();
        dockTimesheet_->raise();
    });
    th->addStretch();
    lay->addWidget(tabsRow);

    // ---- 정보 바 (TVPaint: 좌표 · 색 · 프록시 · … · 룸) ----
    auto* info = new QWidget;
    auto* ih = new QHBoxLayout(info);
    ih->setContentsMargins(6, 2, 6, 2);
    ih->setSpacing(14);
    infoCoord_ = new QLabel;
    infoColor_ = new QLabel;
    auto* proxy = new QComboBox;
    proxy->addItems({"프록시: 100%", "프록시: 50%", "프록시: 25%"});
    proxy->setToolTip(QStringLiteral("프록시: 재생 중 낮은 해상도로 합성해 속도를 높입니다"));
    connect(proxy, &QComboBox::currentIndexChanged, this, [this](int i) { setProxyChoice(i); });
    ih->addWidget(infoCoord_);
    ih->addWidget(infoColor_);
    ih->addWidget(proxy);
    ih->addStretch();
    ih->addWidget(infoImage_);
    auto* house = new QToolButton;
    house->setText("⌂");
    house->setToolTip(QStringLiteral("룸: 이름 변경 / 불러오기 / 저장"));
    house->setPopupMode(QToolButton::InstantPopup);
    auto* hm = new QMenu(house);
    hm->addAction("이름 변경", this, [this] { renameRoom(); });
    hm->addAction("불러오기", this, [this] { loadRooms(); });
    hm->addAction("저장", this, [this] { saveRooms(); });
    house->setMenu(hm);
    roomCombo_ = new QComboBox;
    roomCombo_->addItems(roomNames_);
    roomCombo_->setToolTip(QStringLiteral("룸 전환 (Ctrl+Alt 를 누르고 선택하면 현재 룸 설정을 복사)"));
    connect(roomCombo_, &QComboBox::currentIndexChanged, this, [this](int i) { applyRoom(i); });
    ih->addWidget(house);
    ih->addWidget(roomCombo_);
    lay->addWidget(info);

    central->setMinimumWidth(420); // 오른쪽 패널을 넓게 끌 수 있도록
    setCentralWidget(central);

    connect(timeline_, &TimelineWidget::rangeChanged, this, [this](int a, int b) {
        loopA_ = a;
        loopB_ = b;
        scrub_->setRange(a, b);
        if (a >= 0 && loopModeBtn_) loopModeBtn_->setChecked(true);
    });
    connect(canvas_, &CanvasWidget::cursorMoved, this, [this](int x, int y) {
        cursorX_ = x;
        cursorY_ = y;
        updateInfo();
    });
    connect(canvas_, &CanvasWidget::zoomChanged, this, [this](qreal) { updateInfo(); });
    connect(canvas_, &CanvasWidget::message, this, [this](const QString& m) { statusBar()->showMessage(m, 3000); });
    connect(doc_, &Document::changed, this, [this] { hoverCacheValid_ = false; });
    connect(doc_, &Document::currentChanged, this, [this] { hoverCacheValid_ = false; });
    connect(doc_, &Document::currentChanged, this, [this] {
        QSignalBlocker b(layerOpacity_);
        layerOpacity_->setValue(int(doc_->layer(doc_->currentLayer()).opacity * 100 + 0.5));
    });
    connect(canvas_, &CanvasWidget::strokeFinished, this, [this] {
        HistEntry e{canvas_->tool(), brushRow_, canvas_->brush(), canvas_->colorA()};
        if (!history_.isEmpty()) {
            const HistEntry& t = history_.first();
            if (t.tool == e.tool && t.row == e.row && t.color == e.color && t.params.size == e.params.size &&
                t.params.opacity == e.params.opacity && t.params.hardness == e.params.hardness)
                return;
        }
        history_.push_front(e);
        while (history_.size() > 12) history_.removeLast();
    });
}

void MainWindow::buildDocks() {
    auto makeDock = [this](const QString& title, QWidget* w, Qt::DockWidgetArea area) {
        auto* d = new QDockWidget(title, this);
        d->setObjectName(title);
        d->setWidget(w);
        d->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetFloatable);
        d->setTitleBarWidget(new DockTitleBar(d));
        addDockWidget(area, d);
        docks_.push_back(d);
        return d;
    };
    auto* main = new MainPanel;
    color_ = new ColorPanel;
    tool_ = new ToolPanel;
    cameraPanel_ = new CameraPanel(doc_);

    dockMain_ = makeDock("메인 패널", main, Qt::LeftDockWidgetArea);
    dockColor_ = makeDock("색상 패널", color_, Qt::LeftDockWidgetArea);
    dockTool_ = makeDock("도구 패널", tool_, Qt::LeftDockWidgetArea);
    splitDockWidget(dockMain_, dockColor_, Qt::Vertical);
    splitDockWidget(dockColor_, dockTool_, Qt::Vertical);
    resizeDocks({dockMain_, dockColor_, dockTool_}, {250, 200, 330}, Qt::Vertical);

    brushPanel_ = new BrushPanel;
    dockBrush_ = makeDock("도구 프리셋", brushPanel_, Qt::RightDockWidgetArea);
    dockLibrary_ = makeDock("라이브러리", new LibraryPanel(doc_), Qt::RightDockWidgetArea);
    dockTimesheet_ = makeDock("타임시트", new TimesheetPanel(doc_), Qt::RightDockWidgetArea);
    dockFx_ = makeDock("FX 스택", new FxPanel(doc_), Qt::RightDockWidgetArea);
    dockCamera_ = makeDock("카메라", cameraPanel_, Qt::RightDockWidgetArea);
    dockStory_ = makeDock("스토리보드", new StoryboardPanel(doc_), Qt::RightDockWidgetArea);
    {
        auto* sc = new ScriptConsole(doc_);
        sc->setHost([this](const QStringList& t) { return scriptHostCommand(t); });
        dockScript_ = makeDock("스크립트 콘솔", sc, Qt::RightDockWidgetArea);
    }
    tabifyDockWidget(dockCamera_, dockStory_);
    tabifyDockWidget(dockStory_, dockScript_);
    dockScript_->hide();

    auto* lt = new LightTablePanel(canvas_);
    dockNavigator_ = makeDock("내비게이터", new NavigatorPanel(doc_, canvas_), Qt::RightDockWidgetArea);
    dockGuides_ = makeDock("가이드라인", new GuidelinesPanel(canvas_), Qt::RightDockWidgetArea);
    dockLight_ = makeDock("라이트 테이블", lt, Qt::RightDockWidgetArea);
    auto* remote = new RemotePanel;
    dockRemote_ = makeDock("리모컨", remote, Qt::RightDockWidgetArea);
    dockFlip_ = makeDock("플립 패널", new FlipPanel(doc_), Qt::RightDockWidgetArea);
    auto* share = new ShareManagerPanel;
    dockShare_ = makeDock("공유 관리자", share, Qt::RightDockWidgetArea);
    for (QDockWidget* d : {dockNavigator_, dockGuides_, dockLight_, dockRemote_, dockShare_, dockFlip_}) tabifyDockWidget(dockScript_, d);
    for (QDockWidget* d : {dockNavigator_, dockGuides_, dockLight_, dockRemote_, dockShare_, dockFlip_}) d->hide();
    connect(lt, &LightTablePanel::enabledChanged, this, [this](bool on) {
        if (lightAct_ && lightAct_->isChecked() != on) lightAct_->setChecked(on);
    });
    connect(remote, &RemotePanel::play, this, [this] { if (!playTimer_.isActive()) togglePlay(); });
    connect(remote, &RemotePanel::stop, this, [this] { if (playTimer_.isActive()) togglePlay(); });
    connect(remote, &RemotePanel::first, this, [this] { gotoFrame(0); });
    connect(remote, &RemotePanel::last, this, [this] { gotoFrame(doc_->frameCount() - 1); });
    connect(remote, &RemotePanel::prevFrame, this, [this] { gotoFrame(doc_->currentFrame() - 1); });
    connect(remote, &RemotePanel::nextFrame, this, [this] { gotoFrame(doc_->currentFrame() + 1); });
    connect(remote, &RemotePanel::prevImage, this, [this] {
        const int l = doc_->currentLayer(), f = doc_->currentFrame();
        const int k = doc_->keyIndexAt(l, f);
        int p = -1;
        for (int i = (k == f ? f - 1 : k - 1); i >= 0; --i)
            if (!doc_->layer(l).frames[i].isNull()) { p = i; break; }
        if (k >= 0 && k != f) p = k;
        if (p >= 0) gotoFrame(p);
    });
    connect(remote, &RemotePanel::nextImage, this, [this] {
        const int n = doc_->nextKeyIndex(doc_->currentLayer(), doc_->currentFrame());
        if (n >= 0) gotoFrame(n);
    });
    connect(share, &ShareManagerPanel::exportCurrentPng, this, [this] { exportCurrentPng(); });
    connect(share, &ShareManagerPanel::exportVideo, this, [this] { exportVideoDialog(); });
    connect(share, &ShareManagerPanel::exportPngSequence, this, [this] { exportPngSequence(); });
    connect(share, &ShareManagerPanel::exportTimesheetPdf, this, [this] { exportTimesheetPdf(); });
    connect(share, &ShareManagerPanel::exportXdts, this, [this] { exportXdtsDialog(); });
    tabifyDockWidget(dockBrush_, dockLibrary_);
    tabifyDockWidget(dockLibrary_, dockTimesheet_);
    tabifyDockWidget(dockTimesheet_, dockFx_);
    tabifyDockWidget(dockFx_, dockCamera_);
    dockBrush_->raise();

    connect(main, &MainPanel::toolSelected, this, [this](int id) {
        using T = CanvasWidget::Tool;
        switch (id) {
        case 0: canvas_->setTool(T::Pen); break;
        case 1: canvas_->setTool(T::Line); break;
        case 2: canvas_->setTool(T::Rect); break;
        case 3: canvas_->setTool(T::Circle); break;
        case 4: canvas_->setTool(T::Spline); break;
        case 5: canvas_->setTool(T::Bucket); break;
        case 6:
        case 8: canvas_->setTool(T::Select); break;
        case 7: canvas_->setTool(T::Warp); break;
        case 9:
            canvas_->setTool(T::Select);
            transformDialog();
            break;
        case 10: canvas_->setTool(T::Pick); break;
        case 11: canvas_->setTool(T::Lasso); break;
        case 12: canvas_->setTool(T::Polygon); break;
        case 13: canvas_->setTool(T::Wand); break;
        case 14: canvas_->setTool(T::Gradient); break;
        case 15: canvas_->setTool(T::Text); break;
        default: break;
        }
    });
    connect(main, &MainPanel::cameraToggled, this, [this](bool on) {
        doc_->setCameraEnabled(on);
        if (on) {
            dockCamera_->show();
            dockCamera_->raise();
        }
    });
    connect(canvas_, &CanvasWidget::colorPicked, this, [this](const QColor& c) {
        color_->setColorA(c);
    });
    connect(main, &MainPanel::clearRequested, canvas_, &CanvasWidget::clearCurrent);
    connect(main, &MainPanel::smoothingToggled, this, [this](bool on) {
        canvas_->setSmoothing(on);
        statusBar()->showMessage(on ? QString("선 보정 켬 (세기 %1). 도구 패널에서 세기를 바꿉니다.").arg(canvas_->smoothingStrength())
                                    : QStringLiteral("선 보정 끔"), 3500);
    });
    connect(tool_, &ToolPanel::smoothingStrengthChanged, this, [this](int s) {
        canvas_->setSmoothingStrength(s);
        if (canvas_->smoothing()) statusBar()->showMessage(QString("선 보정 세기: %1").arg(s), 2000);
    });
    connect(main, &MainPanel::paperToggled, canvas_, &CanvasWidget::setPaper);
    connect(main, &MainPanel::stencilToggled, canvas_, &CanvasWidget::setStencil);
    connect(main, &MainPanel::historyRequested, this, &MainWindow::showHistory);
    connect(color_, &ColorPanel::colorAChanged, this, [this](const QColor& c) {
        canvas_->setColorA(c);
        canvas_->setColorB(color_->colorB());   // 그라디언트·글자 테두리에 쓰는 B 색도 맞춘다
    });
    canvas_->setColorB(color_->colorB());
    connect(tool_, &ToolPanel::paramsChanged, canvas_, &CanvasWidget::setBrush);
    connect(tool_, &ToolPanel::brushSelected, this, [this](int row) {
        using T = CanvasWidget::Tool;
        brushRow_ = row;
        if (!brushPreset(row).eraser) lastPen_ = row;
        dockTool_->setWindowTitle(QString("도구 : %1").arg(QString::fromUtf8(brushPreset(row).name)));
        canvas_->setTool(brushPreset(row).eraser ? T::Eraser : T::Pen);
        if (brushPanel_) brushPanel_->setCurrent(row);
    });
    connect(brushPanel_, &BrushPanel::presetSelected, tool_, &ToolPanel::selectPreset);
    canvas_->setBrush(tool_->params());

    // 도구 단축키 (편집 > 단축키 설정 에서 바꿀 수 있다)
    {
        auto& SC = Shortcuts::instance();
        auto brushByName = [](const char* name) {
            for (int i = 0; i < brushPresetCount(); ++i)
                if (QString::fromUtf8(brushPreset(i).name) == QString::fromUtf8(name)) return i;
            return 0;
        };
        const int eraserRow = brushByName("지우개");
        const int pencilRow = brushByName("연필 브러시");
        SC.add(this, "도구", "브러시", QKeySequence(Qt::Key_B), [this, main] {
            main->selectTool(0);
            tool_->selectPreset(brushPreset(brushRow_).eraser ? lastPen_ : brushRow_);
        });
        SC.add(this, "도구", "지우개", QKeySequence(Qt::Key_E), [this, main, eraserRow] {
            main->selectTool(0);
            tool_->selectPreset(eraserRow);
        });
        SC.add(this, "도구", "연필", QKeySequence(Qt::Key_P), [this, main, pencilRow] {
            main->selectTool(0);
            tool_->selectPreset(pencilRow);
        });
        const struct { const char* label; int id; Qt::Key key; } tools[] = {
            {"선", 1, Qt::Key_L},      {"사각형", 2, Qt::Key_R}, {"원", 3, Qt::Key_O},
            {"스플라인", 4, Qt::Key_S}, {"페인트통", 5, Qt::Key_G}, {"선택 / 이동", 6, Qt::Key_M},
            {"리깅 / 핀 워프", 7, Qt::Key_W},
            {"색 추출 (스포이트)", 10, Qt::Key_I}, {"올가미 선택", 11, Qt::Key_Q},
            {"다각형 선택", 12, Qt::Key_Y},        {"마술봉 선택", 13, Qt::Key_A},
            {"그라디언트", 14, Qt::Key_D},         {"글자 넣기", 15, Qt::Key_T},
        };
        for (const auto& t : tools) {
            const int id = t.id;
            SC.add(this, "도구", t.label, QKeySequence(t.key), [main, id] { main->selectTool(id); });
        }
        SC.add(this, "도구", "브러시 크기 줄이기", QKeySequence(Qt::Key_BracketLeft), [this] { tool_->bumpSize(-2); });
        SC.add(this, "도구", "브러시 크기 키우기", QKeySequence(Qt::Key_BracketRight), [this] { tool_->bumpSize(2); });
    }
}

void MainWindow::showHistory(const QPoint& globalPos) {
    if (history_.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("브러시 기록이 비어 있습니다."), 3000);
        return;
    }
    QMenu menu(this);

    for (int i = 0; i < history_.size(); ++i) {
        const HistEntry& e = history_[i];
        const QString label = QString("%1  %2px  %3%  경도 %4  %5")
                                  .arg(QString::fromUtf8(brushPreset(e.row).name))
                                  .arg(e.params.size).arg(e.params.opacity).arg(e.params.hardness)
                                  .arg(e.color.name());
        QPixmap sw(14, 14);
        sw.fill(e.color);
        QAction* a = menu.addAction(QIcon(sw), label);
        connect(a, &QAction::triggered, this, [this, i] { applyHistory(history_[i]); });
    }
    menu.exec(globalPos);
}

void MainWindow::applyHistory(const HistEntry& e) {
    brushRow_ = e.row;
    tool_->setRow(e.row);
    if (brushPanel_) brushPanel_->setCurrent(e.row);
    tool_->setParams(e.params);
    canvas_->setBrush(e.params);
    canvas_->setTool(e.tool);
    color_->setColorA(e.color);
}

// 타임라인 프레임 편집. kind: 0 복사, 1 잘라내기, 2 끼워 넣기, 3 덮어쓰기, 4 내용 지우기, 5 삭제(당김), 6 옮기기
void MainWindow::frameEdit(int kind) {
    const bool all = frameAllLayers_;
    int a = timeline_->rangeStart(), b = timeline_->rangeEnd();
    if (a < 0) a = b = doc_->currentFrame();
    if (b < a) std::swap(a, b);
    const int l = doc_->currentLayer();
    const QString scope = all ? QStringLiteral("모든 레이어") : QStringLiteral("현재 레이어");
    switch (kind) {
    case 0:
        frameClip_ = doc_->copyFrames(l, a, b, all);
        statusBar()->showMessage(QString("%1 프레임 %2–%3 을 복사했습니다 (%4). 범위는 타임라인 눈금을 Shift+드래그해서 정합니다.")
                                     .arg(scope).arg(a + 1).arg(b + 1).arg(frameClip_.count), 6000);
        break;
    case 1:
        frameClip_ = doc_->copyFrames(l, a, b, all);
        if (doc_->deleteFrames(l, a, b, all, true)) {
            timeline_->clearRange();
            statusBar()->showMessage(QString("프레임 %1–%2 를 잘라냈습니다.").arg(a + 1).arg(b + 1), 5000);
        } else {
            statusBar()->showMessage(QStringLiteral("프레임을 전부 잘라낼 수는 없습니다."), 4000);
        }
        break;
    case 2:
    case 3: {
        if (frameClip_.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("복사한 프레임이 없습니다. 먼저 \"프레임 복사\"를 하세요."), 4000);
            return;
        }
        const int at = doc_->currentFrame();
        if (doc_->pasteFrames(l, at, frameClip_, kind == 2)) {
            statusBar()->showMessage(QString("프레임 %1 자리에 %2개를 %3했습니다.")
                                         .arg(at + 1).arg(frameClip_.count)
                                         .arg(kind == 2 ? QStringLiteral("끼워 넣기") : QStringLiteral("덮어쓰기")), 5000);
        } else {
            statusBar()->showMessage(QStringLiteral("붙여 넣지 못했습니다 (캔버스 크기가 다릅니다)."), 4000);
        }
        break;
    }
    case 4:
        doc_->deleteFrames(l, a, b, all, false);
        statusBar()->showMessage(QString("%1 의 프레임 %2–%3 내용을 지웠습니다.").arg(scope).arg(a + 1).arg(b + 1), 5000);
        break;
    case 5:
        if (doc_->deleteFrames(l, a, b, all, true)) {
            timeline_->clearRange();
            statusBar()->showMessage(QString("프레임 %1–%2 를 삭제했습니다.").arg(a + 1).arg(b + 1), 5000);
        } else {
            statusBar()->showMessage(QStringLiteral("프레임을 전부 삭제할 수는 없습니다."), 4000);
        }
        break;
    case 6: {
        bool ok = false;
        const int dest = QInputDialog::getInt(this, "프레임 옮기기",
                                              QString("프레임 %1–%2 를 어디 앞으로 옮길까요? (프레임 번호)").arg(a + 1).arg(b + 1),
                                              std::min(doc_->frameCount(), b + 2), 1, doc_->frameCount(), 1, &ok);
        if (!ok) return;
        if (doc_->moveFrames(l, a, b, dest - 1, all)) {
            timeline_->clearRange();
            statusBar()->showMessage(QStringLiteral("프레임을 옮겼습니다."), 4000);
        } else {
            statusBar()->showMessage(QStringLiteral("제자리라 옮기지 않았습니다."), 4000);
        }
        break;
    }
    default: break;
    }
}

// 팔레트 저장/불러오기 (.onypal = JSON, .gpl = GIMP 팔레트)
bool MainWindow::savePalette(const QString& path) const {
    const QVector<QColor> cols = color_->swatches();
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream ts(&f);
    ts.setEncoding(QStringConverter::Utf8);
    if (path.endsWith(".gpl", Qt::CaseInsensitive)) {
        ts << "GIMP Palette\nName: ONY\nColumns: 8\n#\n";
        for (const QColor& c : cols)
            ts << QString("%1 %2 %3\t%4\n").arg(c.red(), 3).arg(c.green(), 3).arg(c.blue(), 3).arg(c.name());
        return true;
    }
    QJsonArray arr;
    for (const QColor& c : cols) arr.append(c.name());
    QJsonObject root;
    root["type"] = "ony-palette";
    root["version"] = 1;
    root["colors"] = arr;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

int MainWindow::loadPalette(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return 0;
    const QByteArray data = f.readAll();
    QVector<QColor> cols;
    if (data.trimmed().startsWith('{')) {
        const QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isObject()) return 0;
        for (const QJsonValue& v : doc.object().value("colors").toArray()) {
            const QColor c(v.toString());
            if (c.isValid()) cols.push_back(c);
        }
    } else {
        // GIMP 팔레트: "R G B  이름" 줄들
        for (const QByteArray& lineRaw : data.split('\n')) {
            const QString line = QString::fromUtf8(lineRaw).trimmed();
            if (line.isEmpty() || line.startsWith('#') || line.startsWith("GIMP") || line.startsWith("Name:") || line.startsWith("Columns:"))
                continue;
            const QStringList parts = line.split(QRegularExpression("[\\s\\t]+"), Qt::SkipEmptyParts);
            if (parts.size() < 3) continue;
            bool o1 = false, o2 = false, o3 = false;
            const int r = parts[0].toInt(&o1), g = parts[1].toInt(&o2), b = parts[2].toInt(&o3);
            if (o1 && o2 && o3) cols.push_back(QColor(std::clamp(r, 0, 255), std::clamp(g, 0, 255), std::clamp(b, 0, 255)));
        }
    }
    if (cols.isEmpty()) return 0;
    color_->setSwatches(cols);
    return cols.size();
}

// ffmpeg 이 어디 있는지 보여 주고, 없으면 직접 고를 수 있게 한다
void MainWindow::ffmpegDialog() {
    QDialog dlg(this);
    dlg.setWindowTitle("ffmpeg 위치");
    dlg.setMinimumWidth(620);
    auto* lay = new QVBoxLayout(&dlg);
    auto* state = new QLabel;
    state->setWordWrap(true);
    auto* list = new QPlainTextEdit;
    list->setReadOnly(true);
    list->setMaximumHeight(190);
    auto refresh = [&] {
        const QString ff = VideoExport::findFfmpeg();
        if (ff.isEmpty()) {
            state->setText("<b style='color:#e2694a;'>ffmpeg 를 찾지 못했습니다.</b><br>"
                           "동영상 내보내기 · 동영상 가져오기 · 소리 파형에 ffmpeg 가 필요합니다.<br>"
                           "ffmpeg.exe 를 ONY 가 설치된 폴더(ony.exe 옆)에 두거나, 아래에서 직접 고르세요.");
        } else {
            QProcess p;
            p.start(ff, {"-version"});
            p.waitForFinished(5000);
            const QString first = QString::fromLocal8Bit(p.readAllStandardOutput()).section('\n', 0, 0);
            state->setText(QString("<b style='color:#5cc8a8;'>ffmpeg 를 찾았습니다.</b><br>%1<br>%2")
                               .arg(QDir::toNativeSeparators(ff).toHtmlEscaped(), first.toHtmlEscaped()));
        }
        list->setPlainText(VideoExport::searchedPaths().join('\n'));
    };
    refresh();
    lay->addWidget(state);
    lay->addWidget(new QLabel("찾아본 곳:"));
    lay->addWidget(list);
    auto* row = new QHBoxLayout;
    auto* pick = new QPushButton("ffmpeg 직접 고르기...");
    auto* clear = new QPushButton("지정 해제");
    auto* site = new QPushButton("내려받는 곳 열기");
    row->addWidget(pick);
    row->addWidget(clear);
    row->addWidget(site);
    row->addStretch();
    lay->addLayout(row);
    connect(pick, &QPushButton::clicked, &dlg, [&] {
#ifdef Q_OS_WIN
        const QString filter = "ffmpeg (ffmpeg.exe);;모든 파일 (*)";
#else
        const QString filter = "ffmpeg (ffmpeg);;모든 파일 (*)";
#endif
        const QString p = QFileDialog::getOpenFileName(&dlg, "ffmpeg 실행 파일 고르기", QString(), filter);
        if (p.isEmpty()) return;
        VideoExport::setConfiguredFfmpeg(p);
        refresh();
        updateWaveform();
    });
    connect(clear, &QPushButton::clicked, &dlg, [&] {
        VideoExport::setConfiguredFfmpeg(QString());
        refresh();
    });
    connect(site, &QPushButton::clicked, &dlg, [] {
        QDesktopServices::openUrl(QUrl("https://www.gyan.dev/ffmpeg/builds/"));
    });
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    lay->addWidget(bb);
    dlg.exec();
}

// 색 보정 대화상자 (커브 / 레벨)
void MainWindow::colorAdjustDialog(bool levelsMode) {
    const int l = doc_->currentLayer();
    const int k = doc_->keyIndexAt(l, doc_->currentFrame());
    if (k < 0 || doc_->layer(l).locked) {
        statusBar()->showMessage(QStringLiteral("현재 프레임에 편집할 이미지가 없거나 레이어가 잠겼습니다."), 4000);
        return;
    }
    const QImage before = doc_->keyImage(l, k);
    ColorAdjustDialog dlg(before, levelsMode, this);
    if (dlg.exec() != QDialog::Accepted) return;
    const QRect sel = canvas_->selection();
    QImage after = dlg.result();
    if (!sel.isNull()) {   // 선택 영역이 있으면 그 안에만 적용
        QImage merged = before;
        QPainter p(&merged);
        const QImage mask = canvas_->selectionMask();
        if (mask.isNull()) {
            p.setClipRect(sel);
            p.drawImage(0, 0, after);
        } else {
            QImage piece = after;
            QPainter mp(&piece);
            mp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            mp.drawImage(0, 0, mask);
            mp.end();
            QPainter cp(&merged);
            cp.setCompositionMode(QPainter::CompositionMode_DestinationOut);
            cp.drawImage(0, 0, mask);
            cp.setCompositionMode(QPainter::CompositionMode_SourceOver);
            cp.drawImage(0, 0, piece);
        }
        p.end();
        after = merged;
    }
    doc_->pushKeyEdit(l, k, before, after, levelsMode ? QStringLiteral("레벨") : QStringLiteral("커브"));
}

// 되돌리기 기록 창: 목록을 눌러 그 시점으로 간다
void MainWindow::undoHistoryDialog() {
    QDialog dlg(this);
    dlg.setWindowTitle("되돌리기 기록");
    dlg.setMinimumSize(380, 420);
    auto* lay = new QVBoxLayout(&dlg);
    lay->addWidget(new QLabel("목록에서 고르면 그 시점으로 갑니다. 맨 위는 아무것도 하지 않은 상태입니다."));
    auto* view = new QUndoView(doc_->undoStack());
    view->setEmptyLabel(QStringLiteral("(시작 상태)"));
    lay->addWidget(view, 1);
    auto* note = new QLabel;
    note->setWordWrap(true);
    note->setStyleSheet("color:#a0a0a0;");
    note->setText(QString("기록 %1개 · 최대 %2개까지 남습니다.\n레이어 추가·삭제처럼 구조를 바꾸는 작업을 하면 기록이 지워집니다.")
                      .arg(doc_->undoStack()->count())
                      .arg(doc_->undoStack()->undoLimit() > 0 ? QString::number(doc_->undoStack()->undoLimit()) : QStringLiteral("제한 없음")));
    lay->addWidget(note);
    auto* row = new QHBoxLayout;
    auto* clear = new QPushButton("기록 지우기");
    connect(clear, &QPushButton::clicked, &dlg, [this, note] {
        doc_->undoStack()->clear();
        note->setText(QStringLiteral("기록을 지웠습니다."));
    });
    row->addWidget(clear);
    row->addStretch();
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    row->addWidget(bb);
    lay->addLayout(row);
    dlg.exec();
}

// 스크립트에서 창(선택 영역·색·도구 등)을 다루는 명령을 처리한다
std::optional<QString> MainWindow::scriptHostCommand(const QStringList& t) {
    if (t.isEmpty()) return std::nullopt;
    const QString cmd = t[0].toLower();
    auto num = [&](int i, double def = 0) { return i < t.size() ? t[i].toDouble() : def; };
    if (cmd == "select.all") { canvas_->selectAll(); return QString(); }
    if (cmd == "select.none") { canvas_->clearSelection(); return QString(); }
    if (cmd == "select.invert") { canvas_->invertSelection(); return QString(); }
    if (cmd == "select.rect") {
        canvas_->setSelectionRect(QRect(int(num(1)), int(num(2)), int(num(3)), int(num(4))));
        return QString();
    }
    if (cmd == "select.grow") { canvas_->growSelection(int(num(1, 4))); return QString(); }
    if (cmd == "select.feather") { canvas_->featherSelection(int(num(1, 4))); return QString(); }
    if (cmd == "color") {
        if (t.size() < 2) return QStringLiteral("사용법: color #rrggbb  또는  color A #rrggbb");
        const QString c = t.size() > 2 ? t[2] : t[1];
        const QColor col(c);
        if (!col.isValid()) return QStringLiteral("색을 알 수 없습니다");
        color_->setColorA(col);
        return QString();
    }
    if (cmd == "tool") {
        if (t.size() < 2) return QStringLiteral("사용법: tool 이름 (펜 지우개 선 사각형 원 페인트통 선택 스포이트 올가미 마술봉 그라디언트 글자)");
        using T = CanvasWidget::Tool;
        static const QMap<QString, T> kTools{
            {"펜", T::Pen}, {"pen", T::Pen}, {"연필", T::Pencil}, {"지우개", T::Eraser}, {"eraser", T::Eraser},
            {"선", T::Line}, {"line", T::Line}, {"사각형", T::Rect}, {"rect", T::Rect}, {"원", T::Circle},
            {"페인트통", T::Bucket}, {"선택", T::Select}, {"스포이트", T::Pick}, {"올가미", T::Lasso},
            {"다각형", T::Polygon}, {"마술봉", T::Wand}, {"그라디언트", T::Gradient}, {"글자", T::Text}};
        const auto it = kTools.constFind(t[1]);
        if (it == kTools.constEnd()) return QStringLiteral("모르는 도구입니다");
        canvas_->setTool(it.value());
        return QString();
    }
    if (cmd == "zoom") {
        const double z = num(1, 100) / 100.0;
        canvas_->zoomBy(z / std::max(0.01, canvas_->zoom()));
        return QString();
    }
    if (cmd == "msg") {
        statusBar()->showMessage(t.mid(1).join(' '), 5000);
        return QString();
    }
    if (cmd == "zoom.fit") { canvas_->zoomFit(); return QString(); }
    return std::nullopt;
}

// 벡터 레이어 획 편집. kind: 0 굵기, 1 색, 2 단순화, 3 마지막 지우기, 4 모두 지우기
void MainWindow::vectorEditDialog(int kind) {
    const int l = doc_->currentLayer(), f = doc_->currentFrame();
    if (!doc_->isVectorLayer(l)) {
        statusBar()->showMessage(QStringLiteral("벡터 레이어가 아닙니다. 레이어 > 벡터 레이어 에서 켜세요."), 4000);
        return;
    }
    const QVector<VecStroke>* cur = doc_->vectorStrokes(l, f);
    if (!cur || cur->isEmpty()) {
        statusBar()->showMessage(QStringLiteral("이 프레임에 벡터 획이 없습니다."), 3000);
        return;
    }
    QVector<VecStroke> s = *cur;
    switch (kind) {
    case 0: {
        bool ok = false;
        const int v = QInputDialog::getInt(this, "획 굵기", "굵기 (px):", int(s.first().width), 1, 400, 1, &ok);
        if (!ok) return;
        for (VecStroke& x : s) x.width = v;
        break;
    }
    case 1: {
        const QColor c = QColorDialog::getColor(s.first().color, this, "획 색");
        if (!c.isValid()) return;
        for (VecStroke& x : s)
            if (!x.eraser) x.color = c;
        break;
    }
    case 2: {
        bool ok = false;
        const int v = QInputDialog::getInt(this, "획 단순화", "점을 얼마나 줄일까요? (1 = 그대로, 5 = 많이):", 2, 1, 20, 1, &ok);
        if (!ok) return;
        int before = 0, after = 0;
        for (VecStroke& x : s) {
            before += x.pts.size();
            if (x.pts.size() <= 3) { after += x.pts.size(); continue; }
            QVector<QPointF> p{x.pts.first()};
            QVector<float> pr{x.pressure.value(0, 1.0f)};
            for (int i = v; i < x.pts.size() - 1; i += v) {
                p.push_back(x.pts[i]);
                pr.push_back(x.pressure.value(i, 1.0f));
            }
            p.push_back(x.pts.last());
            pr.push_back(x.pressure.value(x.pts.size() - 1, 1.0f));
            x.pts = p;
            x.pressure = pr;
            after += p.size();
        }
        statusBar()->showMessage(QString("점 %1개 → %2개").arg(before).arg(after), 5000);
        break;
    }
    case 3:
        s.removeLast();
        break;
    case 4:
        if (QMessageBox::question(this, "획 지우기", "이 프레임의 벡터 획을 모두 지울까요?") != QMessageBox::Yes) return;
        s.clear();
        break;
    default:
        return;
    }
    doc_->setVectorStrokes(l, f, s);
}

// 두 키 사이를 자동으로 채운다
void MainWindow::inbetweenDialog() {
    const int l = doc_->currentLayer();
    const int f = doc_->currentFrame();
    // 현재 프레임을 기준으로 앞뒤 키를 찾는다
    int a = -1, b = -1;
    for (int i = f; i >= 0; --i)
        if (doc_->keyIndexAt(l, i) == i) { a = i; break; }
    if (a < 0)
        for (int i = 0; i < doc_->frameCount(); ++i)
            if (doc_->keyIndexAt(l, i) == i) { a = i; break; }
    if (a >= 0)
        for (int i = a + 1; i < doc_->frameCount(); ++i)
            if (doc_->keyIndexAt(l, i) == i) { b = i; break; }
    if (a < 0 || b < 0) {
        QMessageBox::information(this, "중간 그림",
                                 "이 레이어에 키 이미지가 두 개 이상 있어야 합니다.\n시작 그림과 끝 그림을 서로 다른 프레임에 그린 뒤 다시 해 주세요.");
        return;
    }
    QDialog dlg(this);
    dlg.setWindowTitle("중간 그림 자동으로 만들기");
    dlg.setMinimumWidth(460);
    auto* form = new QFormLayout(&dlg);
    auto* fa = new QSpinBox; fa->setRange(1, doc_->frameCount()); fa->setValue(a + 1);
    auto* fb = new QSpinBox; fb->setRange(1, doc_->frameCount()); fb->setValue(b + 1);
    auto* mode = new QComboBox;
    const bool vec = doc_->isVectorLayer(l);
    mode->addItem(vec ? "자동 (벡터 획을 이어서 그림)" : "자동 (모양 섞기)", 0);
    mode->addItem("모양 섞기 (선화에 맞음)", 1);
    mode->addItem("겹쳐 지나가기 (크로스페이드)", 2);
    auto* ease = new QComboBox;
    ease->addItems({"직선", "느리게 시작", "느리게 끝", "느리게 시작·끝"});
    auto* note = new QLabel;
    note->setWordWrap(true);
    note->setStyleSheet("color:#a0a0a0;");
    note->setText(vec ? "이 레이어는 벡터라 획의 점을 하나하나 이어 그립니다. 두 그림의 획 수가 비슷할수록 깔끔합니다."
                      : "그림(래스터) 레이어입니다. 모양 섞기는 테두리를 보고 중간 모양을 만들어 냅니다.\n"
                        "사람이 그린 중간 그림만큼 정확하지는 않습니다. 움직임이 크면 어색할 수 있습니다.");
    form->addRow("시작 프레임", fa);
    form->addRow("끝 프레임", fb);
    form->addRow("방식", mode);
    form->addRow("속도 곡선", ease);
    form->addRow(note);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;
    const int n = doc_->makeInbetweens(l, fa->value() - 1, fb->value() - 1, mode->currentData().toInt(), ease->currentIndex());
    if (n > 0) {
        statusBar()->showMessage(QString("중간 그림 %1장을 만들었습니다. 마음에 안 들면 그 프레임을 지우고 직접 그리면 됩니다.").arg(n), 10000);
    } else {
        QMessageBox::information(this, "중간 그림",
                                 "만들지 못했습니다.\n시작과 끝 프레임이 모두 키 이미지여야 하고, 사이에 최소 한 칸이 있어야 합니다.");
    }
}

// ---------------------------------------------------------------- 플러그인
void MainWindow::runPluginCommand(const Plugins::Command& c) {
    QString script = c.script;
    ScriptEngine eng(doc_);
    eng.setHost([this](const QStringList& t) { return scriptHostCommand(t); });
    if (!c.ask.isEmpty()) {
        // "변수이름:물어볼 말:기본값"
        const QStringList parts = c.ask.split(':');
        const QString var = parts.value(0).trimmed();
        const QString label = parts.value(1, QStringLiteral("값")).trimmed();
        const double def = parts.value(2, QStringLiteral("1")).toDouble();
        bool ok = false;
        const double v = QInputDialog::getDouble(this, c.label, label, def, -1000000, 1000000, 2, &ok);
        if (!ok) return;
        if (!var.isEmpty()) eng.setVar(var, v);
    }
    const QString log = eng.run(script);
    const bool bad = log.contains("알 수 없는") || log.contains("사용법") || log.contains("못했습니다");
    statusBar()->showMessage(bad ? QString("%1: %2").arg(c.label, log.left(160).trimmed())
                                 : QString("%1 을(를) 실행했습니다.").arg(c.label),
                             bad ? 12000 : 4000);
}

void MainWindow::rebuildPluginMenu() {
    if (!pluginMenu_) return;
    pluginMenu_->clear();
    plugins_ = Plugins::scan();
    int shown = 0;
    for (const Plugins::Plugin& p : plugins_) {
        if (!p.enabled) continue;
        if (!p.error.isEmpty() && p.commands.isEmpty()) continue;
        QMenu* sub = pluginMenu_->addMenu(p.name);
        for (const Plugins::Command& c : p.commands) {
            QAction* a = sub->addAction(c.label);
            if (!c.shortcut.isEmpty()) a->setShortcut(QKeySequence(c.shortcut));
            connect(a, &QAction::triggered, this, [this, c] { runPluginCommand(c); });
            Shortcuts::instance().adopt(a, QString("플러그인 · %1").arg(p.name));
        }
        ++shown;
    }
    if (shown == 0) {
        QAction* none = pluginMenu_->addAction("(켜진 플러그인이 없습니다)");
        none->setEnabled(false);
    }
    pluginMenu_->addSeparator();
    auto pact = [this](const QString& text, auto fn) {
        QAction* a = pluginMenu_->addAction(text);
        connect(a, &QAction::triggered, this, fn);
        Shortcuts::instance().adopt(a, QStringLiteral("플러그인"));
    };
    pact("플러그인 관리...", [this] { pluginManagerDialog(); });
    pact("플러그인 폴더 열기", [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(Plugins::pluginsDir())); });
    pact("플러그인 만드는 법 보기", [this] {
        QMessageBox::information(this, "플러그인 만드는 법",
                                 QString("플러그인 폴더 안에 폴더를 하나 만들고 ony-plugin.json 을 넣으면 됩니다.\n\n"
                                         "가장 쉬운 방법은 예제 폴더(ony-sample)를 복사해서 고치는 것입니다.\n"
                                         "같은 폴더의 \"읽어보세요.txt\" 에 적는 법이 들어 있습니다.\n\n"
                                         "쓸 수 있는 명령은 스크립트 콘솔의 [도움말] 버튼에서 볼 수 있습니다.\n\n"
                                         "폴더: %1").arg(QDir::toNativeSeparators(Plugins::pluginsDir())));
    });
    pact("다시 읽기", [this] {
        rebuildPluginMenu();
        statusBar()->showMessage(QStringLiteral("플러그인을 다시 읽었습니다."), 3000);
    });
}

void MainWindow::pluginManagerDialog() {
    QDialog dlg(this);
    dlg.setWindowTitle("플러그인");
    dlg.setMinimumSize(560, 420);
    auto* lay = new QVBoxLayout(&dlg);
    auto* info = new QLabel(QString("플러그인 폴더: %1\n폴더마다 ony-plugin.json 이 있으면 플러그인으로 읽습니다.")
                                .arg(QDir::toNativeSeparators(Plugins::pluginsDir())));
    info->setWordWrap(true);
    lay->addWidget(info);
    auto* list = new QListWidget;
    lay->addWidget(list, 1);
    auto* detail = new QLabel;
    detail->setWordWrap(true);
    detail->setMinimumHeight(70);
    detail->setStyleSheet("color:#b0b0b0;");
    lay->addWidget(detail);
    auto fill = [&] {
        list->clear();
        plugins_ = Plugins::scan();
        for (const Plugins::Plugin& p : plugins_) {
            auto* it = new QListWidgetItem(QString("%1  %2").arg(p.name, p.version));
            it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
            it->setCheckState(p.enabled ? Qt::Checked : Qt::Unchecked);
            it->setData(Qt::UserRole, p.id);
            if (!p.error.isEmpty()) it->setForeground(QColor(0xe2, 0x6a, 0x4a));
            list->addItem(it);
        }
        if (plugins_.isEmpty()) list->addItem(QStringLiteral("(플러그인이 없습니다)"));
    };
    fill();
    connect(list, &QListWidget::currentRowChanged, &dlg, [&](int r) {
        if (r < 0 || r >= plugins_.size()) {
            detail->clear();
            return;
        }
        const Plugins::Plugin& p = plugins_[r];
        QString s = QString("%1\n만든이: %2\n명령 %3개\n%4")
                        .arg(p.description.isEmpty() ? QStringLiteral("(설명 없음)") : p.description,
                             p.author.isEmpty() ? QStringLiteral("(없음)") : p.author)
                        .arg(p.commands.size())
                        .arg(QDir::toNativeSeparators(p.dir));
        if (!p.error.isEmpty()) s += "\n문제: " + p.error;
        detail->setText(s);
    });
    connect(list, &QListWidget::itemChanged, &dlg, [this](QListWidgetItem* it) {
        const QString id = it->data(Qt::UserRole).toString();
        if (id.isEmpty()) return;
        Plugins::setEnabled(id, it->checkState() == Qt::Checked);
    });
    auto* row = new QHBoxLayout;
    auto* add = new QPushButton("폴더에서 설치...");
    auto* del = new QPushButton("선택한 플러그인 지우기");
    auto* sample = new QPushButton("예제 다시 만들기");
    row->addWidget(add);
    row->addWidget(del);
    row->addWidget(sample);
    row->addStretch();
    lay->addLayout(row);
    connect(add, &QPushButton::clicked, &dlg, [&] {
        const QString d = QFileDialog::getExistingDirectory(&dlg, "플러그인 폴더 고르기");
        if (d.isEmpty()) return;
        const QString e = Plugins::installFromFolder(d);
        if (!e.isEmpty()) QMessageBox::warning(&dlg, "설치 실패", e);
        fill();
    });
    connect(del, &QPushButton::clicked, &dlg, [&] {
        QListWidgetItem* it = list->currentItem();
        if (!it) return;
        const QString id = it->data(Qt::UserRole).toString();
        if (id.isEmpty()) return;
        if (QMessageBox::question(&dlg, "플러그인 지우기", QString("\"%1\" 폴더를 지웁니다. 계속할까요?").arg(id)) != QMessageBox::Yes) return;
        const QString e = Plugins::removePlugin(id);
        if (!e.isEmpty()) QMessageBox::warning(&dlg, "지우지 못했습니다", e);
        fill();
    });
    connect(sample, &QPushButton::clicked, &dlg, [&] {
        Plugins::installSample();
        fill();
    });
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    lay->addWidget(bb);
    dlg.exec();
    rebuildPluginMenu();
}

// ---------------------------------------------------------------- 공유 폴더 협업
void MainWindow::checkCollabOnOpen(const QString& path) {
    if (!collabOn_ || path.isEmpty()) return;
    Collab::LockInfo info;
    if (Collab::acquireLock(path, &info)) {
        statusBar()->showMessage(QString("이 프로젝트를 쓰는 중으로 표시했습니다 (%1).").arg(Collab::userName()), 6000);
        if (!collabTimer_) {
            collabTimer_ = new QTimer(this);
            connect(collabTimer_, &QTimer::timeout, this, [this] {
                if (collabOn_ && !doc_->path().isEmpty()) Collab::touchLock(doc_->path());
            });
            collabTimer_->start(5 * 60 * 1000);
        }
        updateTitle();
        return;
    }
    const QString since = info.since.isValid() ? info.since.toString("M월 d일 H시 m분") : QStringLiteral("(모름)");
    QMessageBox box(QMessageBox::Warning, "다른 사람이 쓰는 중",
                    QString("이 프로젝트는 지금 다른 사람이 열고 있습니다.\n\n"
                            "사용자: %1\n컴퓨터: %2\n시작: %3\n\n"
                            "그냥 열면 둘이 따로 저장해 한쪽 작업이 사라질 수 있습니다.")
                        .arg(info.user, info.machine, since),
                    QMessageBox::NoButton, this);
    QPushButton* readOnly = box.addButton("보기만 하기 (저장 안 함)", QMessageBox::AcceptRole);
    QPushButton* take = box.addButton("내가 가져오기 (잠금 뺏기)", QMessageBox::DestructiveRole);
    QPushButton* copy = box.addButton("사본으로 열기", QMessageBox::ActionRole);
    box.exec();
    if (box.clickedButton() == static_cast<QAbstractButton*>(take)) {
        Collab::releaseLock(path, true);
        Collab::acquireLock(path);
        Collab::appendHistory(path, QString("잠금을 가져옴 (이전: %1)").arg(info.user), QFileInfo(path).size());
        statusBar()->showMessage(QStringLiteral("잠금을 가져왔습니다. 상대에게 꼭 알려 주세요."), 10000);
    } else if (box.clickedButton() == static_cast<QAbstractButton*>(copy)) {
        doc_->clearPath();
        doc_->setModified(true);
        statusBar()->showMessage(QStringLiteral("사본으로 열었습니다. 저장할 때 새 이름을 정하세요."), 8000);
    } else {
        collabReadOnly_ = true;
        statusBar()->showMessage(QStringLiteral("보기 전용입니다. 저장하려면 [공유] 메뉴에서 잠금을 받으세요."), 10000);
    }
    updateTitle();
}

void MainWindow::collabDialog() {
    QDialog dlg(this);
    dlg.setWindowTitle("공유 폴더로 함께 작업");
    dlg.setMinimumSize(620, 460);
    auto* lay = new QVBoxLayout(&dlg);
    auto* intro = new QLabel(
        "회사 공유 폴더나 드롭박스 같은 곳에 프로젝트를 두고 여럿이 돌아가며 쓸 때 씁니다.\n"
        "프로젝트 파일 옆에 작은 표시 파일(.lock, .history)을 두어, 지금 누가 쓰는지와 저장 이력을 남깁니다.\n"
        "같은 순간에 둘이 함께 그리는 기능은 아닙니다.");
    intro->setWordWrap(true);
    lay->addWidget(intro);

    auto* form = new QFormLayout;
    auto* on = new QCheckBox("이 기능 켜기");
    on->setChecked(collabOn_);
    auto* user = new QLineEdit(Collab::userName());
    auto* stale = new QSpinBox;
    stale->setRange(5, 1440);
    stale->setValue(Collab::staleMinutes());
    stale->setSuffix(" 분");
    stale->setToolTip(QStringLiteral("이 시간 동안 아무 기척이 없으면 버려진 잠금으로 보고 가져올 수 있게 합니다."));
    form->addRow(on);
    form->addRow("내 이름", user);
    form->addRow("잠금이 버려진 것으로 보는 시간", stale);
    lay->addLayout(form);

    auto* state = new QLabel;
    state->setWordWrap(true);
    lay->addWidget(state);
    auto* hist = new QListWidget;
    lay->addWidget(new QLabel("저장 이력"));
    lay->addWidget(hist, 1);

    auto refresh = [&] {
        const QString p = doc_->path();
        if (p.isEmpty()) {
            state->setText("<b>아직 저장하지 않은 프로젝트입니다.</b> 공유 폴더에 한 번 저장한 뒤 다시 열어 주세요.");
            hist->clear();
            return;
        }
        const Collab::LockInfo i = Collab::checkLock(p);
        QString s = QString("파일: %1<br>").arg(QDir::toNativeSeparators(p).toHtmlEscaped());
        if (!i.locked) s += "<b style='color:#5cc8a8;'>아무도 쓰고 있지 않습니다.</b>";
        else if (i.mine) s += "<b style='color:#5cc8a8;'>내가 쓰는 중입니다.</b>";
        else if (i.stale) s += QString("<b style='color:#e2a84a;'>%1 님이 열어 둔 채로 오래됐습니다.</b> 가져와도 됩니다.").arg(i.user.toHtmlEscaped());
        else s += QString("<b style='color:#e2694a;'>%1 님이 쓰는 중입니다 (%2).</b>").arg(i.user.toHtmlEscaped(), i.machine.toHtmlEscaped());
        if (collabReadOnly_) s += "<br>지금 이 창은 보기 전용입니다.";
        state->setText(s);
        hist->clear();
        const auto list = Collab::readHistory(p);
        for (int k = list.size() - 1; k >= 0; --k) {
            const auto& h = list[k];
            hist->addItem(QString("%1   %2   %3   (%4 KB)")
                              .arg(h.when.toString("yyyy-MM-dd HH:mm"), h.user, h.note)
                              .arg(h.bytes / 1024));
        }
        if (list.isEmpty()) hist->addItem(QStringLiteral("(아직 기록이 없습니다)"));
    };
    refresh();

    auto* row = new QHBoxLayout;
    auto* grab = new QPushButton("잠금 받기");
    auto* rel = new QPushButton("잠금 풀기");
    auto* force = new QPushButton("남의 잠금 뺏기");
    auto* note = new QPushButton("다음 저장에 메모 달기...");
    for (QPushButton* b : {grab, rel, force, note}) row->addWidget(b);
    row->addStretch();
    lay->addLayout(row);
    connect(grab, &QPushButton::clicked, &dlg, [&] {
        Collab::LockInfo i;
        if (Collab::acquireLock(doc_->path(), &i)) {
            collabReadOnly_ = false;
            statusBar()->showMessage(QStringLiteral("잠금을 받았습니다."), 4000);
        } else {
            QMessageBox::information(&dlg, "잠금", QString("%1 님이 쓰는 중이라 받을 수 없습니다.").arg(i.user));
        }
        refresh();
        updateTitle();
    });
    connect(rel, &QPushButton::clicked, &dlg, [&] {
        Collab::releaseLock(doc_->path());
        refresh();
        updateTitle();
    });
    connect(force, &QPushButton::clicked, &dlg, [&] {
        const Collab::LockInfo i = Collab::checkLock(doc_->path());
        if (!i.locked || i.mine) {
            refresh();
            return;
        }
        if (QMessageBox::question(&dlg, "잠금 뺏기",
                                  QString("%1 님의 잠금을 뺏습니다.\n그쪽에서 저장하면 한쪽 작업이 사라질 수 있습니다. 계속할까요?").arg(i.user)) != QMessageBox::Yes)
            return;
        Collab::releaseLock(doc_->path(), true);
        Collab::acquireLock(doc_->path());
        Collab::appendHistory(doc_->path(), QString("잠금을 가져옴 (이전: %1)").arg(i.user), 0);
        collabReadOnly_ = false;
        refresh();
        updateTitle();
    });
    connect(note, &QPushButton::clicked, &dlg, [&] {
        bool ok = false;
        const QString s = QInputDialog::getText(&dlg, "저장 메모", "이번에 무엇을 했는지 한 줄로:", QLineEdit::Normal, collabNote_, &ok);
        if (ok) collabNote_ = s;
    });

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(bb);
    if (dlg.exec() != QDialog::Accepted) return;
    Collab::setUserName(user->text());
    Collab::setStaleMinutes(stale->value());
    const bool wasOn = collabOn_;
    collabOn_ = on->isChecked();
    QSettings("Ony", "Ony").setValue("collab/on", collabOn_);
    if (collabOn_ && !wasOn && !doc_->path().isEmpty()) checkCollabOnOpen(doc_->path());
    if (!collabOn_ && wasOn && !doc_->path().isEmpty()) Collab::releaseLock(doc_->path());
    updateTitle();
}

void MainWindow::symmetryDialog() {
    QDialog dlg(this);
    dlg.setWindowTitle("대칭 그리기");
    auto* f = new QFormLayout(&dlg);
    auto s = canvas_->symmetry();
    auto* on = new QCheckBox("대칭 그리기 사용");
    on->setChecked(s.on);
    auto* vert = new QCheckBox("세로축 기준 좌우 대칭");
    vert->setChecked(s.vertical);
    auto* horz = new QCheckBox("가로축 기준 위아래 대칭");
    horz->setChecked(s.horizontal);
    auto* rad = new QSpinBox;
    rad->setRange(1, 24);
    rad->setValue(std::max(1, s.radial));
    rad->setSpecialValueText("끔");
    rad->setSuffix(" 방향");
    auto* ax = new QDoubleSpinBox; ax->setRange(-1, 16384); ax->setDecimals(1); ax->setSpecialValueText("가운데");
    auto* ay = new QDoubleSpinBox; ay->setRange(-1, 16384); ay->setDecimals(1); ay->setSpecialValueText("가운데");
    ax->setValue(s.axisX);
    ay->setValue(s.axisY);
    f->addRow(on);
    f->addRow(vert);
    f->addRow(horz);
    f->addRow("방사 대칭", rad);
    f->addRow("축 X", ax);
    f->addRow("축 Y", ay);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    f->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;
    s.on = on->isChecked();
    s.vertical = vert->isChecked();
    s.horizontal = horz->isChecked();
    s.radial = rad->value();
    s.axisX = ax->value();
    s.axisY = ay->value();
    canvas_->setSymmetry(s);
    if (symAct_) {
        QSignalBlocker b(symAct_);
        symAct_->setChecked(s.on);
    }
}

void MainWindow::gradientTextDialog() {
    QDialog dlg(this);
    dlg.setWindowTitle("그라디언트 / 글자");
    auto* tabs = new QTabWidget;
    auto* lay = new QVBoxLayout(&dlg);
    lay->addWidget(tabs);

    auto g = canvas_->gradientParams();
    auto* gw = new QWidget;
    auto* gf = new QFormLayout(gw);
    auto* gtype = new QComboBox;
    gtype->addItems({"선형", "원형", "원뿔"});
    gtype->setCurrentIndex(g.type);
    auto* gtrans = new QCheckBox("B 색 대신 투명으로");
    gtrans->setChecked(g.toTransparent);
    gf->addRow("모양", gtype);
    gf->addRow(gtrans);
    gf->addRow(new QLabel("캔버스에서 시작점부터 끝점까지 끌면 적용됩니다.\n선택 영역이 있으면 그 안에만 칠합니다."));
    tabs->addTab(gw, "그라디언트");

    auto t = canvas_->textParams();
    auto* tw = new QWidget;
    auto* tf = new QFormLayout(tw);
    auto* ttext = new QPlainTextEdit(t.text);
    ttext->setMaximumHeight(90);
    auto* tfam = new QFontComboBox;
    if (!t.family.isEmpty()) tfam->setCurrentFont(QFont(t.family));
    auto* tsize = new QSpinBox; tsize->setRange(4, 2000); tsize->setValue(t.size); tsize->setSuffix(" px");
    auto* tbold = new QCheckBox("굵게"); tbold->setChecked(t.bold);
    auto* titalic = new QCheckBox("기울임"); titalic->setChecked(t.italic);
    auto* tout = new QCheckBox("테두리 넣기 (B 색)"); tout->setChecked(t.outline);
    auto* tow = new QSpinBox; tow->setRange(1, 40); tow->setValue(t.outlineWidth); tow->setSuffix(" px");
    tf->addRow("내용", ttext);
    tf->addRow("글꼴", tfam);
    tf->addRow("크기", tsize);
    tf->addRow(tbold);
    tf->addRow(titalic);
    tf->addRow(tout);
    tf->addRow("테두리 두께", tow);
    tf->addRow(new QLabel("글자 도구로 캔버스를 클릭한 자리에 들어갑니다."));
    tabs->addTab(tw, "글자");

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    lay->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;
    g.type = gtype->currentIndex();
    g.toTransparent = gtrans->isChecked();
    canvas_->setGradientParams(g);
    t.text = ttext->toPlainText();
    t.family = tfam->currentFont().family();
    t.size = tsize->value();
    t.bold = tbold->isChecked();
    t.italic = titalic->isChecked();
    t.outline = tout->isChecked();
    t.outlineWidth = tow->value();
    canvas_->setTextParams(t);
}

void MainWindow::renderCameraDialog() {
    QDialog dlg(this);
    dlg.setWindowTitle("렌더 카메라");
    auto* f = new QFormLayout(&dlg);
    f->addRow(new QLabel("그림을 그리는 캔버스 크기와 별개로, 내보내기·퍼블리시·인쇄의 최종 출력 크기를 정합니다."));
    const RenderCamera rc = doc_->renderCamera();
    auto* on = new QCheckBox("렌더 카메라 사용");
    on->setChecked(rc.on);
    auto* w = new QSpinBox; w->setRange(16, 16384); w->setValue(rc.size.width());
    auto* h = new QSpinBox; h->setRange(16, 16384); h->setValue(rc.size.height());
    auto* preset = new QComboBox;
    preset->addItem("직접 입력", QSize());
    preset->addItem("HD 1280 x 720", QSize(1280, 720));
    preset->addItem("풀 HD 1920 x 1080", QSize(1920, 1080));
    preset->addItem("2K DCI 2048 x 1080", QSize(2048, 1080));
    preset->addItem("UHD 4K 3840 x 2160", QSize(3840, 2160));
    preset->addItem("정사각형 1080 x 1080", QSize(1080, 1080));
    preset->addItem("세로 영상 1080 x 1920", QSize(1080, 1920));
    preset->addItem("SD 720 x 480", QSize(720, 480));
    connect(preset, &QComboBox::currentIndexChanged, &dlg, [=](int i) {
        const QSize s = preset->itemData(i).toSize();
        if (s.isValid()) { w->setValue(s.width()); h->setValue(s.height()); }
    });
    auto* fit = new QComboBox;
    fit->addItems({"비율 유지 (남는 곳은 배경색)", "화면 채우기 (넘치는 곳은 잘림)", "늘이기 (비율 무시)"});
    fit->setCurrentIndex(rc.fit);
    f->addRow(on);
    f->addRow("크기 프리셋", preset);
    f->addRow("가로 (px)", w);
    f->addRow("세로 (px)", h);
    f->addRow("맞춤 방식", fit);
    f->addRow(new QLabel(QString("현재 캔버스: %1 x %2").arg(doc_->size().width()).arg(doc_->size().height())));
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    f->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;
    RenderCamera n;
    n.on = on->isChecked();
    n.size = QSize(w->value(), h->value());
    n.fit = fit->currentIndex();
    doc_->setRenderCamera(n);
    statusBar()->showMessage(n.on ? QString("렌더 카메라: %1 x %2 로 출력합니다.").arg(n.size.width()).arg(n.size.height()) : QStringLiteral("렌더 카메라를 껐습니다 (캔버스 크기로 출력)."), 4000);
}

void MainWindow::printDialog() {
    QDialog dlg(this);
    dlg.setWindowTitle("인쇄");
    auto* f = new QFormLayout(&dlg);
    auto* layout = new QComboBox;
    layout->addItem("프레임당 한 장", QSize(1, 1));
    layout->addItem("콘택트 시트 2 x 2", QSize(2, 2));
    layout->addItem("콘택트 시트 3 x 4 (스토리보드)", QSize(3, 4));
    layout->addItem("콘택트 시트 4 x 6", QSize(4, 6));
    layout->setCurrentIndex(2);
    auto* range = new QComboBox;
    range->addItems({"전체 프레임", "현재 프레임만", "현재 프레임부터 끝까지"});
    auto* numbers = new QCheckBox("프레임 번호 표시");
    numbers->setChecked(true);
    auto* cam = new QCheckBox("카메라 시점으로 인쇄");
    cam->setEnabled(doc_->cameraEnabled());
    auto* title = new QLineEdit(doc_->path().isEmpty() ? QStringLiteral("ONY") : QFileInfo(doc_->path()).completeBaseName());
    auto* dest = new QComboBox;
#ifdef ONY_HAS_PRINT
    dest->addItem("프린터로 인쇄", 0);
#endif
    dest->addItem("PDF 파일로 저장", 1);
    f->addRow("배치", layout);
    f->addRow("범위", range);
    f->addRow("제목", title);
    f->addRow(numbers);
    f->addRow(cam);
    f->addRow("출력", dest);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    f->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;
    PrintOptions o;
    const QSize g = layout->currentData().toSize();
    o.cols = g.width();
    o.rows = g.height();
    o.numbers = numbers->isChecked();
    o.useCamera = cam->isChecked() && doc_->cameraEnabled();
    o.title = title->text();
    if (range->currentIndex() == 1) o.from = o.to = doc_->currentFrame();
    else if (range->currentIndex() == 2) o.from = doc_->currentFrame();
    if (dest->currentData().toInt() == 1) {
        const QString path = QFileDialog::getSaveFileName(this, "PDF로 저장", o.title + ".pdf", "PDF (*.pdf)");
        if (path.isEmpty()) return;
        statusBar()->showMessage(Printing::exportPdf(doc_, path, o) ? "PDF로 저장했습니다: " + QDir::toNativeSeparators(path) : QStringLiteral("PDF를 저장하지 못했습니다."), 6000);
        return;
    }
#ifdef ONY_HAS_PRINT
    QPrinter printer(QPrinter::HighResolution);
    QPrintDialog pd(&printer, this);
    pd.setWindowTitle("인쇄");
    if (pd.exec() != QDialog::Accepted) return;
    statusBar()->showMessage(Printing::paintAll(&printer, doc_, o) ? QStringLiteral("인쇄했습니다.") : QStringLiteral("인쇄하지 못했습니다."), 5000);
#endif
}

QStringList MainWindow::publishTo(const QString& folder, const QString& name, const PublishOptions& po) {
    QStringList made, errs;
    QDir().mkpath(folder);
    VideoOptions vo;
    vo.quality = po.quality;
    vo.useCamera = doc_->cameraEnabled();
    vo.audio = true;
    if (po.mp4) {
        vo.format = "mp4";
        const QString out = QDir(folder).filePath(name + ".mp4");
        const QString e = VideoExport::run(doc_, out, vo, po.progress);
        if (e.isEmpty()) made << out; else errs << "MP4: " + e;
    }
    if (po.gif) {
        vo.format = "gif";
        const QString out = QDir(folder).filePath(name + ".gif");
        const QString e = VideoExport::run(doc_, out, vo, po.progress);
        if (e.isEmpty()) made << out; else errs << "GIF: " + e;
    }
    if (po.png) {
        vo.format = "png";
        vo.baseName = name;
        const QString out = QDir(folder).filePath(name + "_png");
        const QString e = VideoExport::run(doc_, out, vo, po.progress);
        if (e.isEmpty()) made << out; else errs << "PNG: " + e;
    }
    if (po.pdf) {
        PrintOptions o;
        o.title = name;
        o.cols = 3; o.rows = 4;
        const QString out = QDir(folder).filePath(name + "_콘택트시트.pdf");
        if (Printing::exportPdf(doc_, out, o)) made << out; else errs << "PDF: 저장 실패";
    }
    if (po.project) {
        const QString out = QDir(folder).filePath(name + ".ony");
        if (doc_->saveCopy(out)) made << out; else errs << "프로젝트: 저장 실패";
    }
    if (po.thumb) {
        const QString out = QDir(folder).filePath(name + "_썸네일.png");
        if (doc_->renderFrame(0, Qt::white, doc_->cameraEnabled()).save(out, "PNG")) made << out; else errs << "썸네일: 저장 실패";
    }
    QFile info(QDir(folder).filePath("퍼블리시_정보.txt"));
    if (info.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream ts(&info);
        ts.setEncoding(QStringConverter::Utf8);
        ts << "ONY 퍼블리시\n" << "이름: " << name << "\n" << "날짜: " << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm") << "\n"
           << "프레임: " << doc_->frameCount() << " (" << doc_->fps() << " fps)\n"
           << "출력 크기: " << doc_->renderSize().width() << " x " << doc_->renderSize().height() << "\n"
           << "만든 파일:\n";
        for (const QString& s : made) ts << "  " << QFileInfo(s).fileName() << "\n";
        for (const QString& s : errs) ts << "  (실패) " << s << "\n";
        made << info.fileName();
    }
    lastPublishErrors_ = errs;
    return made;
}

void MainWindow::publishDialog() {
    if (playTimer_.isActive()) togglePlay();
    QDialog dlg(this);
    dlg.setWindowTitle("퍼블리시");
    auto* f = new QFormLayout(&dlg);
    f->addRow(new QLabel("선택한 형식을 한 번에 만들어 폴더 하나에 모읍니다."));
    auto* name = new QLineEdit(doc_->path().isEmpty() ? QStringLiteral("ONY") : QFileInfo(doc_->path()).completeBaseName());
    auto* mp4 = new QCheckBox("MP4 동영상"); mp4->setChecked(true);
    auto* gif = new QCheckBox("GIF 애니메이션");
    auto* png = new QCheckBox("PNG 이미지 시퀀스");
    auto* pdf = new QCheckBox("콘택트 시트 PDF (스토리보드)"); pdf->setChecked(true);
    auto* prj = new QCheckBox("프로젝트 사본 (.ony)"); prj->setChecked(true);
    auto* thumb = new QCheckBox("썸네일 (첫 프레임 PNG)"); thumb->setChecked(true);
    auto* quality = new QComboBox;
    quality->addItems({"높음", "보통", "낮음 (작은 파일)"});
    quality->setCurrentIndex(1);
    f->addRow("이름", name);
    for (auto* c : {mp4, gif, png, pdf, prj, thumb}) f->addRow(c);
    f->addRow("영상 화질", quality);
    f->addRow(new QLabel(QString("출력 크기: %1 x %2 (프로젝트 → 렌더 카메라 설정에서 바꿀 수 있습니다)").arg(doc_->renderSize().width()).arg(doc_->renderSize().height())));
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    f->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;
    const QString parent = QFileDialog::getExistingDirectory(this, "퍼블리시할 위치 선택");
    if (parent.isEmpty()) return;
    const QString nm = name->text().trimmed().isEmpty() ? QStringLiteral("ONY") : name->text().trimmed();
    PublishOptions po;
    po.mp4 = mp4->isChecked(); po.gif = gif->isChecked(); po.png = png->isChecked();
    po.pdf = pdf->isChecked(); po.project = prj->isChecked(); po.thumb = thumb->isChecked();
    po.quality = quality->currentIndex();
    QProgressDialog prog("퍼블리시하는 중...", "취소", 0, 100, this);
    prog.setWindowModality(Qt::WindowModal);
    prog.setMinimumDuration(0);
    po.progress = [&](int done, int total) {
        prog.setValue(total > 0 ? done * 100 / total : 0);
        QApplication::processEvents();
        return !prog.wasCanceled();
    };
    const QString folder = QDir(parent).filePath(nm + "_publish");
    const QStringList made = publishTo(folder, nm, po);
    prog.close();
    if (!lastPublishErrors_.isEmpty())
        QMessageBox::warning(this, "퍼블리시", "일부 형식을 만들지 못했습니다:\n" + lastPublishErrors_.join("\n"));
    statusBar()->showMessage(QString("퍼블리시 완료: %1 (%2개 파일)").arg(QDir::toNativeSeparators(folder)).arg(made.size()), 10000);
}

void MainWindow::strokeAnimationDialog() {
    const int src = doc_->currentFrame();
    if (doc_->strokeCount(src) == 0) {
        QMessageBox::information(this, "획 애니메이션", "현재 프레임에 기록된 획이 없습니다.\n이미지 → 드로잉 레코더 → \"녹화 켜기\"(F9)를 켜고 그린 뒤 다시 실행하세요.");
        return;
    }
    QDialog dlg(this);
    dlg.setWindowTitle("획 애니메이션");
    auto* f = new QFormLayout(&dlg);
    f->addRow(new QLabel(QString("현재 프레임에 기록된 획 %1개를 그려지는 과정 그대로 새 레이어의 프레임들로 풀어 씁니다.").arg(doc_->strokeCount(src))));
    auto* start = new QSpinBox; start->setRange(1, 100000); start->setValue(src + 1);
    auto* len = new QSpinBox; len->setRange(2, 2000); len->setValue(std::min(48, std::max(2, doc_->fps() * 2)));
    auto* mode = new QComboBox;
    mode->addItems({"그린 속도 그대로", "획 길이에 비례 (일정한 속도)"});
    f->addRow("시작 프레임", start);
    f->addRow("길이 (프레임)", len);
    f->addRow("진행 방식", mode);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    f->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;
    const int l = doc_->strokesToFrames(src, start->value() - 1, len->value(), mode->currentIndex() == 0);
    statusBar()->showMessage(l >= 0 ? QStringLiteral("획 애니메이션 레이어를 만들었습니다.") : QStringLiteral("만들지 못했습니다."), 3000);
}

void MainWindow::transitionDialog(int kind, const QString& name) {
    const int l = doc_->currentLayer(), f0 = doc_->currentFrame();
    bool ok = false;
    const int n = QInputDialog::getInt(this, name, "전환 길이 (프레임 수)", 8, 2, std::max(2, doc_->frameCount() - 1 - f0), 1, &ok);
    if (!ok) return;
    runTransition(l, f0, n, kind);
}

bool MainWindow::runTransition(int l, int f0, int n, int kind) {
    const int f1 = f0 + n;
    if (f1 >= doc_->frameCount() || doc_->layer(l).locked) {
        statusBar()->showMessage(QStringLiteral("전환할 프레임 범위가 부족하거나 레이어가 잠겨 있습니다."), 3000);
        return false;
    }
    const int ka = doc_->resolveKey(l, f0), kb = doc_->resolveKey(l, f1);
    if (ka < 0 || kb < 0) {
        statusBar()->showMessage(QStringLiteral("시작·끝 프레임에 이미지가 있어야 합니다."), 3000);
        return false;
    }
    const QImage a = doc_->keyImage(l, ka), b = doc_->keyImage(l, kb);
    doc_->undoStack()->beginMacro(QStringLiteral("전환"));
    for (int i = 1; i < n; ++i) {
        const double t = double(i) / n;
        QImage r;
        if (kind == 0) r = Effects::crossfade(a, b, t);
        else if (kind >= 1 && kind <= 4) r = Effects::wipe(a, b, t, kind - 1);
        else r = Effects::slide(a, b, t);
        doc_->pushKeyEdit(l, f0 + i, doc_->layer(l).frames[f0 + i], r, QStringLiteral("전환"));
    }
    doc_->undoStack()->endMacro();
    return true;
}

void MainWindow::applyEffect(const QString& text, const std::function<QImage(const QImage&)>& fn) {
    if (!doc_->transformCurrent(fn, text))
        statusBar()->showMessage(QStringLiteral("현재 프레임에 편집 가능한 이미지가 없습니다."), 3000);
}

// ---------------------------------------------------------------- 대화상자
void MainWindow::transformDialog() {
    QDialog dlg(this);
    dlg.setWindowTitle("변형");
    auto* form = new QFormLayout(&dlg);
    auto* dx = new QDoubleSpinBox; dx->setRange(-8192, 8192); dx->setDecimals(1);
    auto* dy = new QDoubleSpinBox; dy->setRange(-8192, 8192); dy->setDecimals(1);
    auto* sc = new QDoubleSpinBox; sc->setRange(1, 1000); sc->setValue(100); sc->setDecimals(1);
    auto* rot = new QDoubleSpinBox; rot->setRange(-360, 360); rot->setDecimals(1);
    auto* flip = new QCheckBox("좌우 반전");
    const QRect sel = canvas_->selection();
    form->addRow(new QLabel(sel.isEmpty() ? "대상: 현재 이미지 전체" : "대상: 선택 영역"));
    form->addRow("X 이동", dx);
    form->addRow("Y 이동", dy);
    form->addRow("배율(%)", sc);
    form->addRow("회전(°)", rot);
    form->addRow(flip);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(bb);
    if (dlg.exec() != QDialog::Accepted) return;
    const QTransform t = Warp::makeTransform(sel, doc_->size(), dx->value(), dy->value(), sc->value(), rot->value(), flip->isChecked());
    applyEffect("변형", [&](const QImage& im) { return Warp::transformRegion(im, sel, t); });
    canvas_->clearSelection();
}

void MainWindow::perspectiveDialog() {
    QDialog dlg(this);
    dlg.setWindowTitle("원근");
    auto* form = new QFormLayout(&dlg);
    const QRect sel = canvas_->selection();
    form->addRow(new QLabel(sel.isEmpty() ? "대상: 현재 이미지 전체 (네 모서리를 옮길 거리)" : "대상: 선택 영역 (네 모서리를 옮길 거리)"));
    QDoubleSpinBox* sp[8];
    const char* names[4] = {"좌상", "우상", "우하", "좌하"};
    for (int i = 0; i < 4; ++i) {
        auto* row = new QHBoxLayout;
        for (int j = 0; j < 2; ++j) {
            sp[i * 2 + j] = new QDoubleSpinBox;
            sp[i * 2 + j]->setRange(-4096, 4096);
            sp[i * 2 + j]->setDecimals(1);
            row->addWidget(new QLabel(j == 0 ? "X" : "Y"));
            row->addWidget(sp[i * 2 + j]);
        }
        form->addRow(QString::fromUtf8(names[i]), row);
    }
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(bb);
    if (dlg.exec() != QDialog::Accepted) return;
    QPointF off[4];
    for (int i = 0; i < 4; ++i) off[i] = QPointF(sp[i * 2]->value(), sp[i * 2 + 1]->value());
    QTransform t;
    if (!Warp::makePerspective(sel, doc_->size(), off, &t)) {
        statusBar()->showMessage(QStringLiteral("이 값으로는 원근 변환을 만들 수 없습니다."), 3000);
        return;
    }
    applyEffect("원근", [&](const QImage& im) { return Warp::transformRegion(im, sel, t); });
    canvas_->clearSelection();
}

void MainWindow::folderAction(int kind) {
    auto pick = [this](const QString& title, bool allowNone, int* out) {
        QStringList items;
        if (allowNone) items << "(폴더 없음)";
        for (int i = 0; i < doc_->folderCount(); ++i) items << doc_->folderPath(i);
        if (items.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("폴더가 없습니다. 먼저 새 폴더를 만드세요."), 3000);
            return false;
        }
        bool ok = false;
        const QString s = QInputDialog::getItem(this, title, "폴더:", items, 0, false, &ok);
        if (!ok) return false;
        *out = items.indexOf(s) - (allowNone ? 1 : 0);
        return true;
    };
    int idx = -1;
    switch (kind) {
    case 0: { // 새 폴더 (현재 레이어 포함)
        bool ok = false;
        const QString name = QInputDialog::getText(this, "새 폴더", "폴더 이름:", QLineEdit::Normal,
                                                   QString("폴더 %1").arg(doc_->folderCount() + 1), &ok);
        if (!ok || name.isEmpty()) return;
        const int f = doc_->addFolder(name);
        doc_->setLayerFolder(doc_->currentLayer(), f);
        break;
    }
    case 1:
        if (pick("폴더에 넣기", true, &idx)) doc_->setLayerFolder(doc_->currentLayer(), idx);
        break;
    case 2:
        if (pick("폴더 보이기/숨기기", false, &idx)) {
            doc_->folder(idx).visible = !doc_->folder(idx).visible;
            doc_->notifyStructure();
        }
        break;
    case 3:
        if (pick("폴더 불투명도", false, &idx)) {
            bool ok = false;
            const int v = QInputDialog::getInt(this, "폴더 불투명도", "불투명도(%):", int(doc_->folder(idx).opacity * 100), 0, 100, 5, &ok);
            if (ok) {
                doc_->folder(idx).opacity = v / 100.0;
                doc_->notifyStructure();
            }
        }
        break;
    case 4:
        if (pick("폴더 삭제 (레이어는 유지)", false, &idx)) doc_->removeFolder(idx);
        break;
    case 5: { // 폴더를 다른 폴더 안에 넣기 (중첩)
        if (doc_->folderCount() < 1) {
            statusBar()->showMessage(QStringLiteral("폴더가 없습니다. 먼저 새 폴더를 만드세요."), 3000);
            return;
        }
        int child = -1;
        if (!pick("안으로 넣을 폴더 고르기", false, &child)) return;
        QStringList items{QStringLiteral("(최상위로 꺼내기)")};
        QVector<int> ids{-1};
        for (int i = 0; i < doc_->folderCount(); ++i) {
            if (i == child) continue;
            items << doc_->folderPath(i);
            ids << i;
        }
        bool ok = false;
        const QString s = QInputDialog::getItem(this, "상위 폴더 고르기", "상위 폴더:", items, 0, false, &ok);
        if (!ok) return;
        const int pick2 = items.indexOf(s);
        doc_->setFolderParent(child, ids.value(pick2, -1));
        statusBar()->showMessage(QString("폴더 위치: %1").arg(doc_->folderPath(child)), 4000);
        break;
    }
    default: break;
    }
}

// ---------------------------------------------------------------- 사운드
// 소리 파형을 다시 계산한다 (ffmpeg 로 소리를 읽어 최대값을 뽑는다)
void MainWindow::updateWaveform() {
    if (!wave_) return;
    const QString path = doc_->audioPath();
    if (path.isEmpty() || !QFile::exists(path)) {
        wave_->clearPeaks();
        wave_->setVisible(false);
        waveSource_.clear();
        return;
    }
    if (waveSource_ == path && wave_->hasPeaks()) {
        wave_->setVisible(waveVisible_);
        return;
    }
    statusBar()->showMessage(QStringLiteral("소리 파형을 그리는 중..."), 2000);
    QApplication::processEvents();
    const QVector<float> peaks = VideoExport::audioPeaks(path, 2000);
    waveSource_ = path;
    if (peaks.isEmpty()) {
        wave_->clearPeaks();
        wave_->setVisible(false);
        statusBar()->showMessage(QStringLiteral("소리 파형을 그리지 못했습니다 (ffmpeg 가 필요합니다)."), 5000);
        return;
    }
    wave_->setPeaks(peaks, QFileInfo(path).fileName());
    wave_->setVisible(waveVisible_);
}

void MainWindow::loadAudio() {
    const QString path = QFileDialog::getOpenFileName(this, "사운드 트랙 불러오기", QString(), "오디오 (*.wav *.mp3 *.ogg *.flac *.m4a)");
    if (path.isEmpty()) return;
    doc_->setAudioPath(path);
    syncAudio();
}

void MainWindow::syncAudio() {
    updateWaveform();
    if (doc_->audioPath().isEmpty()) {
        if (player_) player_->setSource(QUrl());
        return;
    }
    if (!player_) {
        player_ = new QMediaPlayer(this);
        audioOut_ = new QAudioOutput(this);
        player_->setAudioOutput(audioOut_);
    }
    player_->setSource(QUrl::fromLocalFile(doc_->audioPath()));
}

// ---------------------------------------------------------------- 메뉴
void MainWindow::buildMenus() {
    auto act = [this](QMenu* m, const QString& text, auto fn, const QKeySequence& ks = QKeySequence()) {
        QAction* a = m->addAction(text);
        if (!ks.isEmpty()) a->setShortcut(ks);
        connect(a, &QAction::triggered, this, fn);
        Shortcuts::instance().adopt(a, m->title());
        return a;
    };

    // 파일
    QMenu* file = menuBar()->addMenu("파일");
    act(file, "새 프로젝트...", [this] { newProjectDialog(); }, QKeySequence::New);
    act(file, "프로젝트 열기...", [this] { openProject(); }, QKeySequence::Open);
    QMenu* recentProj = file->addMenu("최근 프로젝트");
    QMenu* recentDirs = file->addMenu("최근 폴더");
    connect(recentProj, &QMenu::aboutToShow, this, [this, recentProj] {
        recentProj->clear();
        const QStringList l = QSettings("Ony", "Ony").value("recent/files").toStringList();
        for (const QString& p : l) {
            if (!QFileInfo::exists(p)) continue;
            recentProj->addAction(QFileInfo(p).fileName() + "    " + QDir::toNativeSeparators(QFileInfo(p).absolutePath()), this, [this, p] {
                if (maybeSave()) openPath(p);
            });
        }
        if (recentProj->isEmpty()) recentProj->addAction("(없음)")->setEnabled(false);
    });
    connect(recentDirs, &QMenu::aboutToShow, this, [this, recentDirs] {
        recentDirs->clear();
        QStringList dirs;
        for (const QString& p : QSettings("Ony", "Ony").value("recent/files").toStringList())
            if (!dirs.contains(QFileInfo(p).absolutePath())) dirs << QFileInfo(p).absolutePath();
        for (const QString& d : dirs)
            recentDirs->addAction(QDir::toNativeSeparators(d), this, [d] { QDesktopServices::openUrl(QUrl::fromLocalFile(d)); });
        if (recentDirs->isEmpty()) recentDirs->addAction("(없음)")->setEnabled(false);
    });
    file->addSeparator();
    act(file, "저장", [this] { saveProject(); }, QKeySequence::Save);
    act(file, "다른 이름으로 저장...", [this] { saveProjectAs(); }, QKeySequence::SaveAs);
    act(file, "템플릿으로 저장...", [this] {
        bool ok = false;
        const QString n = QInputDialog::getText(this, "템플릿으로 저장", "템플릿 이름:", QLineEdit::Normal, "내 템플릿", &ok);
        if (!ok || n.trimmed().isEmpty()) return;
        if (doc_->save(templateDir() + "/" + n.trimmed() + ".ony")) statusBar()->showMessage("템플릿을 저장했습니다: " + n, 3000);
    });
    QMenu* tplMenu = file->addMenu("템플릿으로 새 프로젝트");
    connect(tplMenu, &QMenu::aboutToShow, this, [this, tplMenu] {
        tplMenu->clear();
        const auto l = QDir(templateDir()).entryInfoList({"*.ony"}, QDir::Files, QDir::Name);
        for (const QFileInfo& fi : l)
            tplMenu->addAction(fi.completeBaseName(), this, [this, fi] {
                if (!maybeSave()) return;
                if (!doc_->load(fi.absoluteFilePath())) return;
                doc_->clearPath();
                doc_->setModified(true);
                syncAudio();
                canvas_->zoomFit();
                updateTitle();
            });
        if (tplMenu->isEmpty()) tplMenu->addAction("(저장된 템플릿 없음)")->setEnabled(false);
    });
    act(file, "공유 폴더로 함께 작업...", [this] { collabDialog(); });
    act(file, "ffmpeg 위치 확인 / 지정...", [this] { ffmpegDialog(); });
    act(file, "백업 폴더 열기", [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(backupDir())); });
    file->addSeparator();
    act(file, "현재 프레임 PNG로 내보내기...", [this] { exportCurrentPng(); });
    act(file, "내보내기 (동영상 / 이미지 시퀀스)...", [this] { exportVideoDialog(); }, QKeySequence("Ctrl+Shift+E"));
    act(file, "PNG 시퀀스로 내보내기...", [this] { exportPngSequence(); });
    act(file, "타임시트 PDF로 내보내기...", [this] { exportTimesheetPdf(); });
    act(file, "인쇄...", [this] { printDialog(); }, QKeySequence("Ctrl+P"));
    act(file, "퍼블리시 (한 번에 여러 형식으로 내보내기)...", [this] { publishDialog(); });
    act(file, "타임시트 XDTS로 내보내기...", [this] { exportXdtsDialog(); });
    act(file, "스캔 이미지 가져오기...", [this] { scanImportDialog(); });
    act(file, "이미지 시퀀스 가져오기...", [this] { importSequenceDialog(); });
    act(file, "동영상 가져오기...", [this] { importVideoDialog(); });
    act(file, "PSD 가져오기 (레이어 유지)...", [this] { importPsdDialog(); });
    act(file, "PSD로 내보내기 (레이어 유지)...", [this] { exportPsdDialog(); });
    file->addSeparator();
    act(file, "프로젝트 닫기", [this] {
        if (!maybeSave()) return;
        if (playTimer_.isActive()) togglePlay();
        doc_->newProject(QSize(1280, 720), 48, 24);
        resetClips();
        canvas_->zoomFit();
    });
    act(file, "종료", [this] { close(); }, QKeySequence("Shift+Q"));

    // 편집
    QMenu* edit = menuBar()->addMenu("편집");
    QAction* undo = doc_->undoStack()->createUndoAction(this, "실행 취소");
    undo->setShortcut(QKeySequence::Undo);
    QAction* redo = doc_->undoStack()->createRedoAction(this, "다시 실행");
    redo->setShortcut(QKeySequence::Redo);
    Shortcuts::instance().adopt(undo, "편집");
    Shortcuts::instance().adopt(redo, "편집");
    edit->addAction(undo);
    edit->addAction(redo);
    edit->addSeparator();
    QAction* editCut = act(edit, "잘라내기", [this] {
        const int k = doc_->keyIndexAt(doc_->currentLayer(), doc_->currentFrame());
        if (k < 0) return;
        clip_ = doc_->keyImage(doc_->currentLayer(), k);
        canvas_->clearCurrent();
    }, QKeySequence::Cut);
    QAction* editCopy = act(edit, "복사", [this] {
        const int k = doc_->keyIndexAt(doc_->currentLayer(), doc_->currentFrame());
        if (k >= 0) clip_ = doc_->keyImage(doc_->currentLayer(), k);
    }, QKeySequence::Copy);
    QAction* editPaste = act(edit, "붙여넣기", [this] {
        if (clip_.isNull() || clip_.size() != doc_->size()) return;
        const int l = doc_->currentLayer(), f = doc_->currentFrame();
        if (doc_->layer(l).locked) return;
        doc_->pushKeyEdit(l, f, doc_->layer(l).frames[f], clip_, QStringLiteral("붙여넣기"));
    }, QKeySequence::Paste);
    edit->addSeparator();
    auto bookmarkStep = [this](int dir) {
        const auto& bm = doc_->bookmarks();
        int best = -1;
        for (int f : bm) {
            if (dir > 0 && f > doc_->currentFrame() && (best < 0 || f < best)) best = f;
            if (dir < 0 && f < doc_->currentFrame() && f > best) best = f;
        }
        if (best >= 0) gotoFrame(best);
    };
    act(edit, "북마크 켜기/끄기", [this] { doc_->toggleBookmark(doc_->currentFrame()); }, QKeySequence("Ctrl+Shift+Up"));
    act(edit, "이전 북마크로 이동", [bookmarkStep] { bookmarkStep(-1); }, QKeySequence("Ctrl+Shift+Left"));
    act(edit, "다음 북마크로 이동", [bookmarkStep] { bookmarkStep(1); }, QKeySequence("Ctrl+Shift+Right"));
    act(edit, "북마크 모두 지우기", [this] { doc_->clearBookmarks(); });
    edit->addSeparator();
    act(edit, "되돌리기 기록...", [this] { undoHistoryDialog(); }, QKeySequence("Ctrl+Alt+H"));
    act(edit, "단축키 설정...", [this] { showShortcutDialog(this); }, QKeySequence("Ctrl+Alt+K"));
    act(edit, "단축키 프로필 저장...", [this] {
        QString p = QFileDialog::getSaveFileName(this, "단축키 프로필 저장", "내 단축키.onykeys", "ONY 단축키 (*.onykeys)");
        if (p.isEmpty()) return;
        if (!p.endsWith(".onykeys", Qt::CaseInsensitive)) p += ".onykeys";
        const QString e = Shortcuts::instance().saveProfile(p);
        if (e.isEmpty()) statusBar()->showMessage("단축키를 저장했습니다: " + QDir::toNativeSeparators(p), 6000);
        else QMessageBox::warning(this, "저장 실패", e);
    });
    act(edit, "단축키 프로필 불러오기...", [this] {
        const QString p = QFileDialog::getOpenFileName(this, "단축키 프로필 불러오기", QString(), "ONY 단축키 (*.onykeys)");
        if (p.isEmpty()) return;
        const QString e = Shortcuts::instance().loadProfile(p);
        if (e.isEmpty()) statusBar()->showMessage(QStringLiteral("단축키 프로필을 적용했습니다."), 6000);
        else QMessageBox::warning(this, "불러오기 실패", e);
    });
    act(edit, "단축키 모두 기본값으로", [this] {
        if (QMessageBox::question(this, "단축키 초기화", "바꾼 단축키를 모두 기본값으로 되돌릴까요?") != QMessageBox::Yes) return;
        Shortcuts::instance().resetAll();
        statusBar()->showMessage(QStringLiteral("단축키를 기본값으로 되돌렸습니다."), 4000);
    });
    edit->addSeparator();
    act(edit, "색 팔레트 저장...", [this] {
        QString p = QFileDialog::getSaveFileName(this, "색 팔레트 저장", "내 팔레트.onypal", "ONY 팔레트 (*.onypal);;GIMP 팔레트 (*.gpl)");
        if (p.isEmpty()) return;
        if (!p.endsWith(".onypal", Qt::CaseInsensitive) && !p.endsWith(".gpl", Qt::CaseInsensitive)) p += ".onypal";
        if (savePalette(p)) statusBar()->showMessage("팔레트를 저장했습니다: " + QDir::toNativeSeparators(p), 6000);
        else QMessageBox::warning(this, "저장 실패", "파일을 만들 수 없습니다.");
    });
    act(edit, "색 팔레트 불러오기...", [this] {
        const QString p = QFileDialog::getOpenFileName(this, "색 팔레트 불러오기", QString(), "팔레트 (*.onypal *.gpl);;모든 파일 (*)");
        if (p.isEmpty()) return;
        const int n = loadPalette(p);
        if (n > 0) statusBar()->showMessage(QString("색 %1개를 불러왔습니다.").arg(n), 6000);
        else QMessageBox::warning(this, "불러오기 실패", "팔레트를 읽지 못했습니다.");
    });
    act(edit, "색 팔레트 기본값으로", [this] {
        color_->setSwatches(ColorPanel::defaultPalette());
        statusBar()->showMessage(QStringLiteral("팔레트를 기본값으로 되돌렸습니다."), 4000);
    });
    edit->addSeparator();
    act(edit, "모두 선택", [this] { canvas_->selectAll(); }, QKeySequence::SelectAll);
    act(edit, "선택 해제", [this] { canvas_->clearSelection(); }, QKeySequence("Ctrl+D"));
    act(edit, "선택 반전", [this] { canvas_->invertSelection(); }, QKeySequence("Ctrl+Shift+I"));
    act(edit, "선택 영역 키우기...", [this] {
        if (canvas_->selection().isNull()) { statusBar()->showMessage(QStringLiteral("먼저 영역을 고르세요."), 3000); return; }
        bool ok = false;
        const int v = QInputDialog::getInt(this, "선택 영역 키우기", "픽셀:", 4, 1, 200, 1, &ok);
        if (ok) canvas_->growSelection(v);
    });
    act(edit, "선택 영역 줄이기...", [this] {
        if (canvas_->selection().isNull()) { statusBar()->showMessage(QStringLiteral("먼저 영역을 고르세요."), 3000); return; }
        bool ok = false;
        const int v = QInputDialog::getInt(this, "선택 영역 줄이기", "픽셀:", 4, 1, 200, 1, &ok);
        if (ok) canvas_->growSelection(-v);
    });
    act(edit, "선택 가장자리 부드럽게...", [this] {
        if (canvas_->selection().isNull()) { statusBar()->showMessage(QStringLiteral("먼저 영역을 고르세요."), 3000); return; }
        bool ok = false;
        const int v = QInputDialog::getInt(this, "가장자리 부드럽게", "번지는 폭 (픽셀):", 6, 1, 100, 1, &ok);
        if (ok) {
            canvas_->featherSelection(v);
            statusBar()->showMessage(QStringLiteral("가장자리를 부드럽게 했습니다. 스텐실을 켜면 그릴 때도 부드럽게 들어갑니다."), 6000);
        }
    });
    {
        QMenu* selm = edit->addMenu("선택 도구 설정");
        act(selm, "마술봉 허용 오차...", [this] {
            bool ok = false;
            const int v = QInputDialog::getInt(this, "마술봉", "허용 오차 (0~255): 클수록 넓게 선택됩니다", canvas_->wandTolerance(), 0, 255, 1, &ok);
            if (ok) canvas_->setWandTolerance(v);
        });
        QAction* wa = selm->addAction("마술봉: 보이는 모든 레이어에서 색 보기");
        wa->setCheckable(true);
        wa->setChecked(canvas_->wandSampleAllLayers());
        connect(wa, &QAction::toggled, this, [this](bool on) { canvas_->setWandSampleAllLayers(on); });
        Shortcuts::instance().adopt(wa, QStringLiteral("편집"));
    }
    edit->addSeparator();
    act(edit, "환경설정...", [this] { preferencesDialog(); });

    // 프로젝트
    QMenu* proj = menuBar()->addMenu("프로젝트");
    act(proj, "렌더 카메라 설정...", [this] { renderCameraDialog(); });
    proj->addSeparator();
    act(proj, "프로젝트 복제...", [this] {
        const QString path = QFileDialog::getSaveFileName(this, "프로젝트 복제", QString(), "ONY 프로젝트 (*.ony)");
        if (path.isEmpty()) return;
        if (doc_->saveCopy(path)) statusBar()->showMessage("복제본을 저장했습니다 (현재 프로젝트는 그대로)", 3000);
        else QMessageBox::warning(this, "복제 실패", "파일을 저장할 수 없습니다.");
        updateTitle();
    });
    QMenu* go = proj->addMenu("이동");
    go->addAction("첫 프레임", this, [this] { gotoFrame(0); });
    go->addAction("마지막 프레임", this, [this] { gotoFrame(doc_->frameCount() - 1); });
    go->addAction("프레임 번호로 이동", this, [this] {
        bool ok = false;
        const int f = QInputDialog::getInt(this, "프레임 이동", "프레임 번호:", doc_->currentFrame() + 1, 1, doc_->frameCount(), 1, &ok);
        if (ok) gotoFrame(f - 1);
    });
    go->addAction("다음 키 이미지", this, [this] {
        const int n = doc_->nextKeyIndex(doc_->currentLayer(), doc_->currentFrame());
        if (n >= 0) gotoFrame(n);
    });
    go->addAction("이전 키 이미지", this, [this] {
        const int l = doc_->currentLayer();
        for (int f = doc_->currentFrame() - 1; f >= 0; --f)
            if (!doc_->layer(l).frames[f].isNull() && f < doc_->keyIndexAt(l, doc_->currentFrame())) { gotoFrame(f); return; }
    });
    act(proj, "프로젝트 설정...", [this] { projectSettingsDialog(); }, QKeySequence("Ctrl+Alt+P"));
    proj->addSeparator();
    act(proj, "프로젝트 정보", [this] {
        QMessageBox::information(this, "프로젝트 정보",
            QString("크기: %1 x %2\n프레임 수: %3\n프레임 속도: %4\n레이어: %5\n폴더: %6\n라이브러리: %7개")
                .arg(doc_->size().width()).arg(doc_->size().height()).arg(doc_->frameCount())
                .arg(doc_->fps()).arg(doc_->layerCount()).arg(doc_->folderCount()).arg(doc_->libraryCount()));
    });
    act(proj, "프레임 수 변경", [this] {
        bool ok = false;
        const int n = QInputDialog::getInt(this, "프레임 수", "프레임 수:", doc_->frameCount(), 1, 5000, 1, &ok);
        if (ok) doc_->setFrameCount(n);
    });
    act(proj, "프레임 속도 변경", [this] {
        bool ok = false;
        const int n = QInputDialog::getInt(this, "프레임 속도", "초당 프레임:", doc_->fps(), 1, 120, 1, &ok);
        if (ok) doc_->setFps(n);
    });
    proj->addSeparator();
    act(proj, "사운드 트랙 불러오기", [this] { loadAudio(); });
    act(proj, "사운드 트랙 제거", [this] {
        doc_->setAudioPath(QString());
        syncAudio();
    });

    // 클립
    QMenu* clip = menuBar()->addMenu("클립");
    clipMenu_ = clip;
    act(clip, "새 클립", [this] { newClip(); });
    act(clip, "클립 이름 변경", [this] { renameClip(); });
    act(clip, "클립 삭제", [this] { deleteClip(); });
    clip->addSeparator();
    connect(clip, &QMenu::aboutToShow, this, [this] { rebuildClipMenu(); });
    rebuildClipMenu();

    // 레이어
    QMenu* layer = menuBar()->addMenu("레이어");
    act(layer, "새 레이어", [this] { doc_->addLayer(QString("레이어 %1").arg(doc_->layerCount() + 1)); }, QKeySequence("Ctrl+Shift+N"));
    LayerMenu::populate(layer->addMenu("레이어 속성"), doc_, [this] { return doc_->currentLayer(); }, [this] { return doc_->currentFrame(); });
    layer->addSeparator();
    act(layer, "새 CTG 레이어 (색 + 선화)", [this] { doc_->addCtgPair(); });
    act(layer, "레이어 복제", [this] { doc_->duplicateLayer(doc_->currentLayer()); });
    act(layer, "레이어 삭제", [this] { doc_->removeLayer(doc_->currentLayer()); });
    layer->addSeparator();
    act(layer, "위로 이동", [this] { doc_->moveLayer(doc_->currentLayer(), +1); });
    act(layer, "아래로 이동", [this] { doc_->moveLayer(doc_->currentLayer(), -1); });
    layer->addSeparator();
    QMenu* folder = layer->addMenu("폴더");
    act(folder, "새 폴더 (현재 레이어 포함)", [this] { folderAction(0); });
    act(folder, "현재 레이어를 폴더에 넣기", [this] { folderAction(1); });
    act(folder, "폴더 보이기/숨기기", [this] { folderAction(2); });
    act(folder, "폴더 불투명도", [this] { folderAction(3); });
    act(folder, "폴더 삭제", [this] { folderAction(4); });
    act(folder, "폴더를 다른 폴더 안에 넣기 (중첩)", [this] { folderAction(5); });
    layer->addSeparator();
    {
        QMenu* vm = layer->addMenu("벡터 레이어");
        QAction* vOn = vm->addAction("이 레이어를 벡터로 쓰기");
        vOn->setCheckable(true);
        connect(vm, &QMenu::aboutToShow, this, [this, vOn] {
            QSignalBlocker b(vOn);
            vOn->setChecked(doc_->isVectorLayer(doc_->currentLayer()));
        });
        connect(vOn, &QAction::triggered, this, [this](bool on) {
            const int l = doc_->currentLayer();
            if (!on && doc_->isVectorLayer(l)) {
                if (QMessageBox::question(this, "벡터 레이어 끄기",
                                          "벡터 획을 그림으로 굳히고 벡터 데이터를 지웁니다. 계속할까요?") != QMessageBox::Yes) {
                    return;
                }
                doc_->flattenVectorLayer(l);
                statusBar()->showMessage(QStringLiteral("벡터 획을 그림으로 굳혔습니다."), 5000);
                return;
            }
            doc_->setVectorLayer(l, on);
            statusBar()->showMessage(on ? QStringLiteral("벡터 레이어입니다. 그린 획은 점을 다시 옮길 수 있습니다 (선택 도구로 점 끌기).")
                                        : QString(), 7000);
        });
        Shortcuts::instance().adopt(vOn, QStringLiteral("레이어"));
        act(vm, "획 굵기 한꺼번에 바꾸기...", [this] { vectorEditDialog(0); });
        act(vm, "획 색 한꺼번에 바꾸기...", [this] { vectorEditDialog(1); });
        act(vm, "획 단순화 (점 줄이기)...", [this] { vectorEditDialog(2); });
        act(vm, "마지막 획 지우기", [this] { vectorEditDialog(3); });
        act(vm, "이 프레임의 획 모두 지우기", [this] { vectorEditDialog(4); });
        act(vm, "그림으로 굳히기 (벡터 끄기)", [this] {
            if (!doc_->isVectorLayer(doc_->currentLayer())) {
                statusBar()->showMessage(QStringLiteral("벡터 레이어가 아닙니다."), 3000);
                return;
            }
            doc_->flattenVectorLayer(doc_->currentLayer());
            statusBar()->showMessage(QStringLiteral("벡터 획을 그림으로 굳혔습니다."), 5000);
        });
    }
    {
        QMenu* cb = layer->addMenu("사용자 브러시 · 질감");
        act(cb, "선택 영역을 브러시로 만들기", [this] {
            const QRect sel = canvas_->selection();
            if (sel.isEmpty()) {
                statusBar()->showMessage(QStringLiteral("먼저 선택 도구로 영역을 고르세요."), 4000);
                return;
            }
            const int l = doc_->currentLayer(), k = doc_->resolveKey(l, doc_->currentFrame());
            if (k < 0) {
                statusBar()->showMessage(QStringLiteral("현재 프레임에 이미지가 없습니다."), 4000);
                return;
            }
            QImage cut = doc_->keyImage(l, k).copy(sel);
            const QImage mask = canvas_->selectionMask();
            if (!mask.isNull()) {   // 사각형이 아닌 선택이면 모양대로 오린다
                QPainter mp(&cut);
                mp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
                mp.drawImage(-sel.x(), -sel.y(), mask);
            }
            bool ok = false;
            const QString name = QInputDialog::getText(this, "사용자 브러시", "이름:", QLineEdit::Normal,
                                                       QString("내 브러시 %1").arg(customBrushCount() + 1), &ok);
            if (!ok) return;
            const int id = addCustomBrush(name, cut);
            tool_->refreshCustomBrushes();
            statusBar()->showMessage(id >= 0 ? QString("\"%1\" 을 만들었습니다. 도구 패널 아래에서 고르세요.").arg(name)
                                             : QStringLiteral("만들지 못했습니다."), 7000);
        });
        act(cb, "그림 파일에서 브러시 만들기...", [this] {
            const QString p = QFileDialog::getOpenFileName(this, "브러시로 쓸 그림", QString(), "그림 (*.png *.jpg *.jpeg *.bmp *.tif *.tiff)");
            if (p.isEmpty()) return;
            QImage im(p);
            if (im.isNull()) {
                statusBar()->showMessage(QStringLiteral("그림을 읽지 못했습니다."), 4000);
                return;
            }
            const int id = addCustomBrush(QFileInfo(p).completeBaseName(), im);
            tool_->refreshCustomBrushes();
            statusBar()->showMessage(id >= 0 ? QStringLiteral("브러시를 만들었습니다. 도구 패널 아래에서 고르세요.") : QStringLiteral("만들지 못했습니다."), 7000);
        });
        act(cb, "사용자 브러시 모두 지우기", [this] {
            clearCustomBrushes();
            tool_->refreshCustomBrushes();
            canvas_->setBrush(tool_->params());
            statusBar()->showMessage(QStringLiteral("사용자 브러시를 모두 지웠습니다."), 4000);
        });
        cb->addSeparator();
        act(cb, "질감 그림 지정...", [this] {
            const QString p = QFileDialog::getOpenFileName(this, "질감 그림", QString(), "그림 (*.png *.jpg *.jpeg *.bmp *.tif *.tiff)");
            if (p.isEmpty()) return;
            QImage im(p);
            if (im.isNull()) {
                statusBar()->showMessage(QStringLiteral("그림을 읽지 못했습니다."), 4000);
                return;
            }
            setCustomGrainTexture(im);
            statusBar()->showMessage(QStringLiteral("질감을 지정했습니다 (밝은 곳이 남고 어두운 곳이 비칩니다)."), 6000);
        });
        act(cb, "질감 기본으로 되돌리기", [this] {
            setCustomGrainTexture(QImage());
            statusBar()->showMessage(QStringLiteral("질감을 기본으로 되돌렸습니다."), 4000);
        });
    }
    layer->addSeparator();
    act(layer, "아래 레이어와 병합", [this] {
        if (!doc_->mergeDown(doc_->currentLayer())) statusBar()->showMessage(QStringLiteral("합칠 아래 레이어가 없습니다."), 3000);
    });
    act(layer, "보이는 레이어 모두 병합", [this] {
        if (!doc_->mergeVisible()) statusBar()->showMessage(QStringLiteral("보이는 레이어가 2개 이상 필요합니다."), 3000);
    });
    act(layer, "레이어 이름 변경", [this] {
        bool ok = false;
        const QString n = QInputDialog::getText(this, "레이어 이름", "이름:", QLineEdit::Normal, doc_->layer(doc_->currentLayer()).name, &ok);
        if (ok && !n.isEmpty()) {
            doc_->layer(doc_->currentLayer()).name = n;
            doc_->notifyStructure();
        }
    });

    // 이미지
    QMenu* image = menuBar()->addMenu("이미지");
    act(image, "새 빈 이미지(키 프레임)", [this] {
        const int l = doc_->currentLayer(), f = doc_->currentFrame();
        if (doc_->layer(l).locked) return;
        QImage blank(doc_->size(), QImage::Format_ARGB32_Premultiplied);
        blank.fill(Qt::transparent);
        doc_->pushKeyEdit(l, f, doc_->layer(l).frames[f], blank, QStringLiteral("새 빈 이미지"));
    }, QKeySequence(Qt::Key_Insert));
    act(image, "키 이미지 지우기(노출 해제)", [this] {
        const int l = doc_->currentLayer(), f = doc_->currentFrame();
        if (doc_->layer(l).locked || doc_->layer(l).frames[f].isNull()) return;
        doc_->pushKeyEdit(l, f, doc_->layer(l).frames[f], QImage(), QStringLiteral("키 이미지 지우기"));
    });
    act(image, "이미지 지우기", [this] { canvas_->clearCurrent(); });
    image->addSeparator();
    act(image, "모든 이미지 선택", [this] { timeline_->setRange(0, doc_->frameCount() - 1); });
    act(image, "이미지 반복...", [this] {
        // 선택 구간(없으면 현재 이미지부터 다음 키 전까지)을 N번 반복한다
        const int l = doc_->currentLayer();
        int a = doc_->currentFrame(), b = a;
        if (timeline_->rangeStart() >= 0) { a = timeline_->rangeStart(); b = timeline_->rangeEnd(); }
        else {
            const int k = doc_->keyIndexAt(l, a);
            if (k < 0) return;
            a = k;
            const int n = doc_->nextKeyIndex(l, k);
            b = n < 0 ? doc_->frameCount() - 1 : n - 1;
        }
        QDialog dlg(this);
        dlg.setWindowTitle("이미지 반복");
        auto* f = new QFormLayout(&dlg);
        f->addRow(new QLabel(QString("대상 구간: %1 ~ %2 프레임 (레이어 \"%3\")").arg(a + 1).arg(b + 1).arg(doc_->layer(l).name)));
        auto* times = new QSpinBox;
        times->setRange(1, 200);
        times->setValue(2);
        auto* pp = new QCheckBox("왕복 (홀수 번째 반복은 거꾸로)");
        f->addRow("반복 횟수", times);
        f->addRow(pp);
        auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        f->addRow(bb);
        if (dlg.exec() != QDialog::Accepted) return;
        const int added = doc_->repeatKeys(l, a, b, times->value(), pp->isChecked());
        statusBar()->showMessage(QString("%1 프레임을 반복해 붙였습니다.").arg(added), 4000);
    });
    act(image, "이미지 순서 뒤집기", [this] {
        // 선택한 구간이 있으면 그 구간만, 없으면 레이어 전체의 이미지 순서를 뒤집는다
        const int a = timeline_->rangeStart() >= 0 ? timeline_->rangeStart() : 0;
        const int b = timeline_->rangeStart() >= 0 ? timeline_->rangeEnd() : doc_->frameCount() - 1;
        doc_->reverseKeys(doc_->currentLayer(), a, b);
    });
    image->addSeparator();
    image->addAction(editCut);
    image->addAction(editCopy);
    image->addAction(editPaste);
    image->addSeparator();
    QMenu* inst = image->addMenu("인스턴스");
    inst->addAction("인스턴스 이름 변경", this, [this] {
        const int l = doc_->currentLayer(), k = doc_->keyIndexAt(l, doc_->currentFrame());
        if (k < 0) return;
        bool ok = false;
        const QString n = QInputDialog::getText(this, "인스턴스 이름", "이름:", QLineEdit::Normal, doc_->layer(l).instanceNames.value(k), &ok);
        if (!ok) return;
        if (n.isEmpty()) doc_->layer(l).instanceNames.remove(k);
        else doc_->layer(l).instanceNames[k] = n;
        doc_->notifyChanged();
    });
    inst->addAction("인스턴스 이름 지우기", this, [this] {
        const int l = doc_->currentLayer(), k = doc_->keyIndexAt(l, doc_->currentFrame());
        if (k < 0) return;
        doc_->layer(l).instanceNames.remove(k);
        doc_->notifyChanged();
    });
    inst->addAction("같은 이름의 이미지로 이동", this, [this] {
        const int l = doc_->currentLayer(), k = doc_->keyIndexAt(l, doc_->currentFrame());
        const QString n = doc_->layer(l).instanceNames.value(k);
        if (n.isEmpty()) return;
        for (int f = k + 1; f < doc_->frameCount(); ++f)
            if (doc_->layer(l).instanceNames.value(f) == n) { gotoFrame(f); return; }
        for (int f = 0; f < k; ++f)
            if (doc_->layer(l).instanceNames.value(f) == n) { gotoFrame(f); return; }
    });
    QMenu* expo = image->addMenu("노출");
    expo->addAction("노출 늘리기", this, [this] {
        if (doc_->extendExposure(doc_->currentLayer(), doc_->currentFrame())) gotoFrame(doc_->currentFrame() + 1);
    });
    expo->addAction("노출 줄이기", this, [this] { doc_->shrinkExposure(doc_->currentLayer(), doc_->currentFrame()); });
    for (int n : {1, 2, 3, 4}) {
        expo->addAction(QString("모든 이미지를 %1프레임씩").arg(n), this, [this, n] { doc_->setUniformExposure(doc_->currentLayer(), n); });
    }
    image->addSeparator();
    auto scanFix = [this](int th, int ds) {
        const int l = doc_->currentLayer(), k = doc_->keyIndexAt(l, doc_->currentFrame());
        if (k < 0 || doc_->layer(l).locked) return;
        const QImage src = doc_->keyImage(l, k);
        doc_->pushKeyEdit(l, k, src, Extras::scanClean(src, th, ds, canvas_->colorA()), QStringLiteral("스캔 클리너"));
    };
    act(image, "스캔 클리너 (약하게)", [scanFix] { scanFix(200, 0); });
    act(image, "스캔 클리너 (강하게)", [scanFix] { scanFix(150, 2); });

    // 드로잉 레코더 / 획 애니메이션
    image->addSeparator();
    QMenu* rec = image->addMenu("드로잉 레코더");
    QAction* recAct = act(rec, "녹화 켜기 (그리는 획을 기록)", [] {}, QKeySequence("F9"));
    recAct->setCheckable(true);
    connect(recAct, &QAction::toggled, this, [this](bool on) {
        doc_->setRecording(on);
        statusBar()->showMessage(on ? QStringLiteral("● 녹화 중: 이제 그리는 획이 기록됩니다") : QStringLiteral("녹화를 껐습니다"), 3000);
    });
    recAct_ = recAct;
    QAction* recInfo = rec->addAction("기록된 획: 0개");
    recInfo->setEnabled(false);
    connect(doc_, &Document::recorderChanged, this, [this, recInfo] {
        recInfo->setText(QString("기록된 획: 전체 %1개 / 현재 프레임 %2개").arg(doc_->strokeCount()).arg(doc_->strokeCount(doc_->currentFrame())));
    });
    connect(doc_, &Document::currentChanged, this, [this, recInfo] {
        recInfo->setText(QString("기록된 획: 전체 %1개 / 현재 프레임 %2개").arg(doc_->strokeCount()).arg(doc_->strokeCount(doc_->currentFrame())));
    });
    act(rec, "획 애니메이션 만들기 (현재 프레임의 기록 → 새 레이어)...", [this] { strokeAnimationDialog(); });
    act(rec, "현재 프레임의 기록 지우기", [this] { doc_->clearStrokes(doc_->currentFrame()); });
    act(rec, "기록 모두 지우기", [this] { doc_->clearStrokes(); });
    image->addSeparator();
    act(image, "변형", [this] { transformDialog(); }, QKeySequence("Ctrl+T"));
    act(image, "원근", [this] { perspectiveDialog(); });
    act(image, "메시 워프 (격자 변형)...", [this] { meshWarpDialog(); }, QKeySequence("Ctrl+Alt+M"));
    act(image, "중간 그림 자동으로 만들기...", [this] { inbetweenDialog(); }, QKeySequence("Ctrl+Alt+I"));
    act(image, "좌우 뒤집기", [this] { applyEffect("좌우 뒤집기", Effects::flipH); });
    act(image, "상하 뒤집기", [this] { applyEffect("상하 뒤집기", Effects::flipV); });
    image->addSeparator();
    act(image, "라이브러리에 추가", [this] {
        const int k = doc_->keyIndexAt(doc_->currentLayer(), doc_->currentFrame());
        if (k < 0) return;
        doc_->addToLibrary(QString("이미지 %1").arg(doc_->libraryCount() + 1), doc_->keyImage(doc_->currentLayer(), k));
    });

    // 효과 (파괴적 적용)
    QMenu* fx = menuBar()->addMenu("효과");
    act(fx, "색 반전", [this] { applyEffect("색 반전", Effects::invert); });
    act(fx, "흑백", [this] { applyEffect("흑백", Effects::grayscale); });
    act(fx, "밝게", [this] { applyEffect("밝게", [](const QImage& i) { return Effects::brightness(i, 24); }); });
    act(fx, "어둡게", [this] { applyEffect("어둡게", [](const QImage& i) { return Effects::brightness(i, -24); }); });
    act(fx, "블러(박스, 반경 4)", [this] { applyEffect("블러", [](const QImage& i) { return Effects::boxBlur(i, 4); }); });
    fx->addSeparator();
    for (const QString& cat : Effects::categories()) {
        QMenu* sub = fx->addMenu(cat);
        for (Effects::Type t : Effects::typesIn(cat)) {
            const auto pi = Effects::paramInfo(t);
            act(sub, Effects::typeName(t) + (pi.has ? "..." : ""), [this, t, pi] {
                Effects::Item item;
                item.type = t;
                item.param = pi.def;
                if (pi.has) {
                    bool ok = false;
                    item.param = QInputDialog::getInt(this, Effects::typeName(t), pi.label, pi.def, pi.lo, pi.hi, 1, &ok);
                    if (!ok) return;
                }
                applyEffect(Effects::typeName(t), [item](const QImage& i) { return Effects::apply(i, item); });
            });
        }
    }
    fx->addSeparator();
    act(fx, "커브...", [this] { colorAdjustDialog(false); }, QKeySequence("Ctrl+M"));
    act(fx, "레벨...", [this] { colorAdjustDialog(true); }, QKeySequence("Ctrl+Shift+L"));
    QMenu* trans = fx->addMenu("전환");
    const QStringList tnames = {"디졸브", "와이프 (좌→우)", "와이프 (우→좌)", "와이프 (위→아래)", "와이프 (아래→위)", "밀기"};
    for (int ti = 0; ti < tnames.size(); ++ti)
        act(trans, tnames[ti] + "...", [this, ti, tnames] { transitionDialog(ti, tnames[ti]); });
    fx->addSeparator();
    act(fx, "FX 스택 열기 (비파괴)", [this] {
        dockFx_->show();
        dockFx_->raise();
    });

    // 보기
    QMenu* view = menuBar()->addMenu("보기");
    act(view, "확대", [this] { canvas_->zoomBy(1.25); }, QKeySequence("Ctrl+="));
    act(view, "축소", [this] { canvas_->zoomBy(0.8); }, QKeySequence("Ctrl+-"));
    act(view, "화면에 맞춤", [this] { canvas_->zoomFit(); }, QKeySequence("Ctrl+0"));
    act(view, "100%", [this] { canvas_->zoomActual(); }, QKeySequence("Ctrl+1"));
    view->addSeparator();
    act(view, "대칭 그리기 설정...", [this] { symmetryDialog(); });
    {
        QAction* sq = act(view, "대칭 그리기 켜기/끄기", [] {}, QKeySequence("Ctrl+Alt+S"));
        sq->setCheckable(true);
        connect(sq, &QAction::toggled, this, [this](bool on) {
            auto s = canvas_->symmetry();
            s.on = on;
            if (on && !s.vertical && !s.horizontal && s.radial < 2) s.vertical = true;
            canvas_->setSymmetry(s);
            statusBar()->showMessage(on ? QStringLiteral("대칭 그리기를 켰습니다.") : QStringLiteral("대칭 그리기를 껐습니다."), 2500);
        });
        symAct_ = sq;
    }
    act(view, "그라디언트 / 글자 설정...", [this] { gradientTextDialog(); });
    view->addSeparator();
    act(view, "타임라인 확대", [this] { timeline_->setCellWidth(timeline_->cell() + 6); }, QKeySequence("Ctrl+Alt+="));
    act(view, "타임라인 축소", [this] { timeline_->setCellWidth(timeline_->cell() - 6); }, QKeySequence("Ctrl+Alt+-"));
    act(view, "타임라인 기본 크기", [this] { timeline_->setCellWidth(TimelineWidget::kCell); }, QKeySequence("Ctrl+Alt+0"));
    QAction* compactAct = act(view, "레이어 접기 (한 줄로 보기)", [] {}, QKeySequence("Ctrl+Shift+K"));
    compactAct->setCheckable(true);
    connect(compactAct, &QAction::toggled, this, [this](bool on) { timeline_->setCompact(on); });
    view->addSeparator();
    QAction* lt = act(view, "라이트 테이블", [] {}, QKeySequence("Ctrl+L"));
    lightAct_ = lt;
    lt->setCheckable(true);
    connect(lt, &QAction::toggled, canvas_, &CanvasWidget::setLightTable);
    camViewAct_ = act(view, "카메라 보기", [] {});
    camViewAct_->setCheckable(true);
    connect(camViewAct_, &QAction::toggled, this, [this](bool on) {
        canvas_->setCameraView(on);
        if (camViewBtn_->isChecked() != on) camViewBtn_->setChecked(on);
    });

    view->addSeparator();
    act(view, "전체 화면", [this] { setWindowState(windowState() ^ Qt::WindowFullScreen); }, QKeySequence(Qt::Key_F11));
    QMenu* roomMenu = view->addMenu("룸");
    auto* roomGroup = new QActionGroup(this);
    for (int i = 0; i < roomNames_.size(); ++i) {
        QAction* a = roomMenu->addAction(roomNames_[i]);
        a->setCheckable(true);
        a->setChecked(i == 0);
        roomGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, i] { roomCombo_->setCurrentIndex(i); });
    }
    connect(roomCombo_, &QComboBox::currentIndexChanged, roomGroup, [roomGroup](int i) {
        if (i >= 0 && i < roomGroup->actions().size()) roomGroup->actions()[i]->setChecked(true);
    });
    view->addSeparator();
    QAction* ll = act(view, "왼쪽 서랍 잠금 (해제하면 자동 숨김)", [] {});
    ll->setCheckable(true);
    ll->setChecked(true);
    connect(ll, &QAction::toggled, this, [this](bool on) { leftLocked_ = on; });
    QAction* rl = act(view, "오른쪽 서랍 잠금 (해제하면 자동 숨김)", [] {});
    rl->setCheckable(true);
    rl->setChecked(true);
    connect(rl, &QAction::toggled, this, [this](bool on) { rightLocked_ = on; });
    drawerTimer_.setInterval(200);
    connect(&drawerTimer_, &QTimer::timeout, this, &MainWindow::drawerTick);
    drawerTimer_.start();

    // 창
    QMenu* win = menuBar()->addMenu("창");
    for (QDockWidget* d : docks_) win->addAction(d->toggleViewAction());

    pluginMenu_ = menuBar()->addMenu("플러그인");
    Plugins::installSample();
    rebuildPluginMenu();
    customMenu_ = menuBar()->addMenu("사용자 패널");
    customDefs_ = loadCustomPanels();
    rebuildCustomMenu();
    QMenu* help = menuBar()->addMenu("도움말");
    {
        QMenu* inl = help->addMenu("인라인 도움말");
        auto* grp = new QActionGroup(this);
        const char* names[] = {"끔", "간략", "상세"};
        for (int i = 0; i < 3; ++i) {
            QAction* a = inl->addAction(QString::fromUtf8(names[i]));
            a->setCheckable(true);
            a->setChecked(i == 1);
            grp->addAction(a);
            connect(a, &QAction::triggered, this, [this, i] {
                setProperty("inlineHelp", i);
                statusBar()->showMessage(i == 0 ? QStringLiteral("인라인 도움말: 끔") : (i == 1 ? QStringLiteral("인라인 도움말: 간략") : QStringLiteral("인라인 도움말: 상세")), 2500);
            });
        }
    }
    act(help, "개인정보 설정", [this] { Privacy::showDialog(this); });
    act(help, "정보", [this] {
        QMessageBox box(this);
        box.setWindowTitle("정보");
        box.setText(QString("ONY — 키 이미지(원화) 중심의 2D 프레임 애니메이션 도구 (개발 중)\n빌드: %1 %2").arg(__DATE__, __TIME__));
#ifdef ONY_HAS_LOGO
        box.setIconPixmap(QPixmap(":/ony_logo.png").scaledToWidth(320, Qt::SmoothTransformation));
#endif
        box.exec();
    });

    // 메뉴 아래 단축 아이콘 줄 (문서 순서: 파일 | 편집 | 패널 토글 | 도움말) + 클립 탭
    auto* tb = addToolBar("단축");
    tb->setObjectName("단축");
    tb->setMovable(false);
    tb->setIconSize(QSize(22, 22));
    tb->setToolButtonStyle(Qt::ToolButtonIconOnly);
    auto tbAct = [&](const QString& icon, const QString& tip, auto fn) {
        QAction* a = tb->addAction(Style::icon(icon, 22), tip);
        a->setToolTip(tip);
        connect(a, &QAction::triggered, this, fn);
    };
    tbAct("new", "새 프로젝트", [this] { newProjectDialog(); });
    tbAct("open", "프로젝트 열기", [this] { openProject(); });
    tbAct("save", "프로젝트 저장", [this] { saveProject(); });
    tb->addSeparator();
    undo->setIcon(Style::icon("undo", 22));
    redo->setIcon(Style::icon("redo", 22));
    undo->setToolTip(QStringLiteral("실행 취소 (Ctrl+Z)"));
    redo->setToolTip(QStringLiteral("다시 실행 (Ctrl+Y)"));
    tb->addAction(undo);
    tb->addAction(redo);
    tb->addSeparator();
    addDockToggle(tb, dockMain_, "transform", "메인 패널");
    addDockToggle(tb, dockColor_, "palette", "색상 패널 (P)");
    addDockToggle(tb, dockTool_, "pen", "도구 패널 (Shift+A)");
    addDockToggle(tb, dockBrush_, "brush", "도구 프리셋 패널");
    {
        QAction* a = tb->addAction(Style::icon("timeline", 22), "타임라인");
        a->setCheckable(true);
        a->setChecked(true);
        a->setToolTip(QStringLiteral("타임라인과 레이어 스택 (0)"));
        connect(a, &QAction::toggled, this, [this](bool on) { bottomSplit_->setVisible(on); });
        timelineToggle_ = a;
    }
    addDockToggle(tb, dockLight_, "light", "라이트 테이블 패널");
    addDockToggle(tb, dockRemote_, "remote", "리모컨 패널");
    addDockToggle(tb, dockFlip_, "play", "플립 패널");
    addDockToggle(tb, dockNavigator_, "navigator", "내비게이터 패널");
    addDockToggle(tb, dockFx_, "fx", "FX 스택");
    addDockToggle(tb, dockLibrary_, "library", "이미지 라이브러리");
    addDockToggle(tb, dockGuides_, "guides", "가이드라인 패널");
    addDockToggle(tb, dockShare_, "share", "공유 관리자");
    tb->addSeparator();
    // 클립 탭 (TVPaint 의 문서 탭 자리)
    clipTabs_ = new QTabBar;
    clipTabs_->setDrawBase(false);
    clipTabs_->setExpanding(false);
    clipTabs_->setToolTip(QStringLiteral("클립 전환 (더블클릭: 이름 변경, 우클릭: 메뉴)"));
    clipTabs_->installEventFilter(this);
    connect(clipTabs_, &QTabBar::currentChanged, this, [this](int i) {
        if (i >= 0 && i != clipIdx_) switchClip(i);
    });
    clipTabs_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(clipTabs_, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        const int i = clipTabs_->tabAt(pos);
        if (i < 0) return;
        if (i != clipIdx_) switchClip(i);
        QMenu m(this);
        m.addAction("클립 이름 변경", this, [this] { renameClip(); syncClipTabs(); });
        m.addAction("새 클립", this, [this] { newClip(); });
        QAction* del = m.addAction("클립 삭제", this, [this] { deleteClip(); syncClipTabs(); });
        del->setEnabled(clipNames_.size() > 1);
        m.exec(clipTabs_->mapToGlobal(pos));
    });
    // 클립이 많아져도 오른쪽 검색창이 밀리지 않도록, 탭은 얇은 가로 스크롤 영역 안에 둔다
    clipScroll_ = new QScrollArea;
    clipScroll_->setFrameShape(QFrame::NoFrame);
    clipScroll_->setWidget(clipTabs_);
    clipScroll_->setWidgetResizable(false);
    clipScroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    clipScroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    clipScroll_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    clipScroll_->setMinimumWidth(120);
    clipScroll_->setFixedHeight(40);
    clipScroll_->setStyleSheet(
        "QScrollArea{background:transparent;}"
        "QScrollBar:horizontal{height:6px;background:transparent;margin:0;}"
        "QScrollBar::handle:horizontal{background:#808080;border-radius:3px;min-width:30px;}"
        "QScrollBar::handle:horizontal:hover{background:#a0a0a0;}"
        "QScrollBar::add-line:horizontal,QScrollBar::sub-line:horizontal{width:0;height:0;}"
        "QScrollBar::add-page:horizontal,QScrollBar::sub-page:horizontal{background:transparent;}");
    tb->addWidget(clipScroll_);
    auto* addTab = new QToolButton;
    addTab->setIcon(Style::icon("plus", 18));
    addTab->setToolTip(QStringLiteral("새 클립"));
    connect(addTab, &QToolButton::clicked, this, [this] { newClip(); });
    tb->addWidget(addTab);
    auto* helpSearch = new QLineEdit;
    helpSearch->setPlaceholderText(QStringLiteral("? 매뉴얼 검색"));
    helpSearch->setMaximumWidth(160);
    connect(helpSearch, &QLineEdit::returnPressed, this, [helpSearch] {
        const QString q = helpSearch->text().trimmed();
        if (!q.isEmpty()) QDesktopServices::openUrl(QUrl("https://doc.tvpaint.com/search?q=" + QUrl::toPercentEncoding(q)));
    });
    tb->addWidget(helpSearch);
    syncClipTabs();

    auto& SC = Shortcuts::instance();
    SC.add(this, "패널", "타임라인 표시/숨김", QKeySequence(Qt::Key_0), [this] { timelineToggle_->toggle(); });
    SC.add(this, "패널", "색상 패널 표시/숨김", QKeySequence("Shift+P"), [this] { dockColor_->setVisible(!dockColor_->isVisible()); });
    SC.add(this, "패널", "도구 패널 표시/숨김", QKeySequence("Shift+A"), [this] { dockTool_->setVisible(!dockTool_->isVisible()); });
    SC.add(this, "패널", "브러시 패널 표시/숨김", QKeySequence("Shift+B"), [this] { dockBrush_->setVisible(!dockBrush_->isVisible()); });
}

void MainWindow::addDockToggle(QToolBar* tb, QDockWidget* dock, const QString& iconName, const QString& tip) {
    QAction* a = tb->addAction(Style::icon(iconName, 22), tip);
    a->setCheckable(true);
    a->setChecked(dock->isVisible());
    a->setToolTip(tip);
    connect(a, &QAction::toggled, dock, [dock](bool on) { dock->setVisible(on); });
    connect(dock, &QDockWidget::visibilityChanged, a, [a](bool on) {
        if (a->isChecked() != on) a->setChecked(on);
    });
}

void MainWindow::renameRoom() {
    bool ok = false;
    const QString n = QInputDialog::getText(this, "룸 이름", "이름:", QLineEdit::Normal, roomNames_[curRoom_], &ok);
    if (!ok || n.isEmpty()) return;
    roomNames_[curRoom_] = n;
    roomCombo_->setItemText(curRoom_, n);
}

void MainWindow::saveRooms() {
    const QString path = QFileDialog::getSaveFileName(this, "룸 저장", QString(), "룸 설정 (*.ini)");
    if (path.isEmpty()) return;
    roomStates_[curRoom_] = saveState();
    QSettings st(path, QSettings::IniFormat);
    st.setValue("names", roomNames_);
    for (int i = 0; i < roomStates_.size(); ++i) st.setValue(QString("state%1").arg(i), roomStates_[i]);
    st.sync();
    statusBar()->showMessage("룸을 저장했습니다.", 3000);
}

void MainWindow::loadRooms() {
    const QString path = QFileDialog::getOpenFileName(this, "룸 불러오기", QString(), "룸 설정 (*.ini)");
    if (path.isEmpty()) return;
    QSettings st(path, QSettings::IniFormat);
    const QStringList names = st.value("names").toStringList();
    if (names.size() != roomNames_.size()) {
        QMessageBox::warning(this, "불러오기 실패", "룸 설정 파일이 올바르지 않습니다.");
        return;
    }
    roomNames_ = names;
    for (int i = 0; i < roomStates_.size(); ++i) {
        roomStates_[i] = st.value(QString("state%1").arg(i)).toByteArray();
        roomCombo_->setItemText(i, names[i]);
    }
    if (!roomStates_[curRoom_].isEmpty()) restoreState(roomStates_[curRoom_]);
    statusBar()->showMessage("룸을 불러왔습니다.", 3000);
}

void MainWindow::applyRoom(int index) {
    if (index < 0 || index >= roomStates_.size() || index == curRoom_) return;
    const bool copy = QApplication::keyboardModifiers().testFlags(Qt::ControlModifier | Qt::AltModifier);
    roomStates_[curRoom_] = saveState();
    curRoom_ = index;
    if (copy) {
        // Ctrl+Alt: 떠나는 룸의 설정을 새 룸이 그대로 물려받는다 (배치는 바꾸지 않는다).
    } else if (!roomStates_[index].isEmpty()) {
        restoreState(roomStates_[index]);
    } else {
        applyRoomDefault(index);
    }
}

void MainWindow::applyRoomDefault(int index) {
    // 룸마다 오른쪽 서랍에 보이는 패널이 달라진다.
    const QList<QDockWidget*> right = {dockBrush_, dockLibrary_, dockTimesheet_, dockFx_, dockCamera_, dockStory_,
                                       dockScript_, dockNavigator_, dockGuides_, dockLight_, dockRemote_, dockShare_, dockFlip_};
    QList<QDockWidget*> show;
    switch (index) {
    case 0: show = {dockBrush_, dockNavigator_}; break;
    case 1: show = {dockTimesheet_, dockLight_, dockCamera_, dockRemote_, dockFlip_}; break;
    case 2: show = {dockFx_, dockLibrary_, dockGuides_}; break;
    default: show = {dockStory_, dockShare_}; break;
    }
    for (QDockWidget* d : right) d->setVisible(show.contains(d));
    for (QDockWidget* d : show) d->raise();
    if (!show.isEmpty()) show.first()->raise();
}

// ---------------------------------------------------------------- 파일
void MainWindow::releaseCollabLock() {
    if (collabOn_ && !collabReadOnly_ && !doc_->path().isEmpty()) Collab::releaseLock(doc_->path());
}

bool MainWindow::maybeSave() {
    if (!doc_->modified()) return true;
    const auto r = QMessageBox::question(this, "저장", "변경 사항을 저장할까요?",
                                         QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (r == QMessageBox::Cancel) return false;
    if (r == QMessageBox::Save) return saveProject();
    return true;
}

void MainWindow::closeEvent(QCloseEvent* e) {
    playTimer_.stop();
    if (player_) player_->stop();
    if (maybeSave()) {
        releaseCollabLock();
        QSettings("Ony", "Ony").setValue("window/geometry", saveGeometry());
        e->accept();
    } else {
        e->ignore();
    }
}

void MainWindow::projectSettingsDialog() {
    if (playTimer_.isActive()) togglePlay();
    QDialog dlg(this);
    dlg.setWindowTitle("프로젝트 설정");
    auto* form = new QFormLayout(&dlg);
    auto* preset = new QComboBox;
    preset->addItems({"현재 크기", "HD 1280x720", "Full HD 1920x1080", "4K UHD 3840x2160", "정사각 1080x1080", "SD 720x480", "사용자 지정"});
    auto* w = new QSpinBox; w->setRange(16, 8192); w->setValue(doc_->size().width());
    auto* h = new QSpinBox; h->setRange(16, 8192); h->setValue(doc_->size().height());
    auto* lockRatio = new QCheckBox("가로세로 비율 유지");
    const double ratio = double(doc_->size().width()) / doc_->size().height();
    auto* mode = new QComboBox;
    mode->addItems({"그림을 새 크기에 맞춰 늘이기/줄이기", "캔버스만 변경 (가운데 정렬, 넘치면 잘림)", "캔버스만 변경 (왼쪽 위 기준)"});
    auto* fps = new QSpinBox; fps->setRange(1, 120); fps->setValue(doc_->fps());
    auto* frames = new QSpinBox; frames->setRange(1, 5000); frames->setValue(doc_->frameCount());
    bool busy = false;
    auto setSize = [=, &busy](int nw, int nh) {
        busy = true;
        w->setValue(nw);
        h->setValue(nh);
        busy = false;
    };
    connect(preset, &QComboBox::currentIndexChanged, &dlg, [=](int i) {
        static const int sz[][2] = {{0, 0}, {1280, 720}, {1920, 1080}, {3840, 2160}, {1080, 1080}, {720, 480}};
        if (i == 0) setSize(doc_->size().width(), doc_->size().height());
        else if (i < 6) setSize(sz[i][0], sz[i][1]);
    });
    connect(w, &QSpinBox::valueChanged, &dlg, [=, &busy](int v) {
        if (busy) return;
        preset->blockSignals(true);
        preset->setCurrentIndex(6);
        preset->blockSignals(false);
        if (lockRatio->isChecked()) setSize(v, std::max(16, int(v / ratio + 0.5)));
    });
    connect(h, &QSpinBox::valueChanged, &dlg, [=, &busy](int v) {
        if (busy) return;
        preset->blockSignals(true);
        preset->setCurrentIndex(6);
        preset->blockSignals(false);
        if (lockRatio->isChecked()) setSize(std::max(16, int(v * ratio + 0.5)), v);
    });
    form->addRow("해상도 프리셋", preset);
    form->addRow("너비 (px)", w);
    form->addRow("높이 (px)", h);
    form->addRow(lockRatio);
    form->addRow("크기 변경 방식", mode);
    form->addRow("프레임 속도", fps);
    auto* stretch = new QCheckBox("프레임 속도를 바꿀 때 길이(초) 유지: 이미지 위치를 비율대로 옮김");
    form->addRow(stretch);
    form->addRow("프레임 수", frames);
    auto* note = new QLabel("해상도를 바꾸면 실행 취소 기록이 지워집니다.");
    note->setStyleSheet("color:#a0a0a0;");
    form->addRow(note);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(bb);
    if (dlg.exec() != QDialog::Accepted) return;
    const QSize ns(w->value(), h->value());
    if (ns != doc_->size()) doc_->resizeCanvas(ns, mode->currentIndex());
    const bool fpsChanged = fps->value() != doc_->fps();
    if (fpsChanged) {
        if (stretch->isChecked()) doc_->stretchToFps(fps->value());
        else doc_->setFps(fps->value());
    }
    if (frames->value() != doc_->frameCount() && !(stretch->isChecked() && fpsChanged)) doc_->setFrameCount(frames->value());
    canvas_->zoomFit();
    updateInfo();
    statusBar()->showMessage(QString("프로젝트 설정 적용: %1 x %2, %3 fps, %4 프레임").arg(ns.width()).arg(ns.height()).arg(doc_->fps()).arg(doc_->frameCount()), 4000);
}

// 현재 레이어의 현재 프레임이 비어 있으면(홀드 중) 빈 새 이미지를 만든다
void MainWindow::ensureBlankKey() {
    const int l = doc_->currentLayer(), f = doc_->currentFrame();
    if (doc_->layer(l).locked || !doc_->layer(l).frames[f].isNull()) return;
    QImage blank(doc_->size(), QImage::Format_ARGB32_Premultiplied);
    blank.fill(Qt::transparent);
    doc_->pushKeyEdit(l, f, QImage(), blank, QStringLiteral("새 이미지"));
}

// 다음 프레임으로 이동. newImage 가 켜져 있으면 끝을 넘어가면 프레임을 늘리고 빈 새 이미지를 만든다.
void MainWindow::nextFrameAction(bool forceNewImage) {
    const bool makeNew = forceNewImage || (autoNewBtn_ && autoNewBtn_->isChecked());
    int f = doc_->currentFrame() + 1;
    if (f >= doc_->frameCount()) {
        if (!makeNew) return;
        doc_->setFrameCount(f + 1);
    }
    gotoFrame(f);
    if (makeNew) ensureBlankKey();
}

void MainWindow::newProjectDialog() {
    if (!maybeSave()) return;
    QDialog dlg(this);
    dlg.setWindowTitle("새 프로젝트");
    auto* form = new QFormLayout(&dlg);
    auto* preset = new QComboBox;
    preset->addItems({"HD 1280x720", "Full HD 1920x1080", "정사각 1080x1080", "사용자 지정"});
    auto* w = new QSpinBox; w->setRange(16, 8192); w->setValue(1280);
    auto* h = new QSpinBox; h->setRange(16, 8192); h->setValue(720);
    auto* frames = new QSpinBox; frames->setRange(1, 5000); frames->setValue(48);
    auto* fps = new QSpinBox; fps->setRange(1, 120); fps->setValue(24);
    connect(preset, &QComboBox::currentIndexChanged, &dlg, [=](int i) {
        if (i == 0) { w->setValue(1280); h->setValue(720); }
        else if (i == 1) { w->setValue(1920); h->setValue(1080); }
        else if (i == 2) { w->setValue(1080); h->setValue(1080); }
    });
    form->addRow("프리셋", preset);
    form->addRow("너비", w);
    form->addRow("높이", h);
    form->addRow("프레임 수", frames);
    form->addRow("프레임 속도", fps);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(bb);
    if (dlg.exec() != QDialog::Accepted) return;
    if (playTimer_.isActive()) togglePlay();
    doc_->newProject(QSize(w->value(), h->value()), frames->value(), fps->value());
    resetClips();
    syncAudio();
    canvas_->zoomFit();
}

void MainWindow::openProject() {
    if (!maybeSave()) return;
    const QString path = QFileDialog::getOpenFileName(this, "프로젝트 열기", QString(), "ONY 프로젝트 (*.ony *.tvpx)");
    if (path.isEmpty()) return;
    openPath(path);
}

void MainWindow::openPath(const QString& path) {
    if (playTimer_.isActive()) togglePlay();
    if (!doc_->load(path)) {
        QMessageBox::warning(this, "열기 실패", "프로젝트 파일을 읽을 수 없습니다.");
        return;
    }
    loadClipSiblings(path);
    syncAudio();
    canvas_->zoomFit();
    addRecent(path);
    checkCollabOnOpen(path);
}

void MainWindow::setupAutosave() {
    if (!autosaveTimer_) {
        autosaveTimer_ = new QTimer(this);
        connect(autosaveTimer_, &QTimer::timeout, this, [this] {
            if (!doc_->modified()) return;
            const QString name = doc_->path().isEmpty() ? QStringLiteral("이름없음") : QFileInfo(doc_->path()).completeBaseName();
            if (doc_->saveCopy(backupDir() + "/autosave_" + name + ".ony")) statusBar()->showMessage("자동 백업했습니다", 2500);
        });
    }
    const int min = QSettings("Ony", "Ony").value("save/autosaveMin", 5).toInt();
    autosaveTimer_->stop();
    if (min > 0) autosaveTimer_->start(min * 60 * 1000);
}

void MainWindow::openLastProject() {
    const QStringList l = QSettings("Ony", "Ony").value("recent/files").toStringList();
    for (const QString& p : l)
        if (QFileInfo::exists(p)) { openPath(p); return; }
}

QString MainWindow::backupDir() const {
    const QString d = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/backup";
    QDir().mkpath(d);
    return d;
}

QString MainWindow::templateDir() const {
    const QString d = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/templates";
    QDir().mkpath(d);
    return d;
}

void MainWindow::addRecent(const QString& path) {
    QSettings st("Ony", "Ony");
    QStringList l = st.value("recent/files").toStringList();
    const QString abs = QFileInfo(path).absoluteFilePath();
    l.removeAll(abs);
    l.prepend(abs);
    while (l.size() > 10) l.removeLast();
    st.setValue("recent/files", l);
}

bool MainWindow::writeProject(const QString& path) {
    const int keep = clipIdx_;
    if (clipNames_.size() > 1 && clipIdx_ != 0) switchClip(0);
    // 덮어쓰기 전에 이전 파일을 백업 폴더에 보관 (최근 10개)
    if (QFileInfo::exists(path)) {
        const QFileInfo fi(path);
        QFile::copy(path, QString("%1/%2_%3.ony").arg(backupDir(), fi.completeBaseName(), QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss")));
        QDir bd(backupDir());
        const auto old = bd.entryInfoList({fi.completeBaseName() + "_*.ony"}, QDir::Files, QDir::Time);
        for (int i = QSettings("Ony", "Ony").value("save/backupCount", 10).toInt(); i < old.size(); ++i) QFile::remove(old[i].absoluteFilePath());
    }
    const bool ok = doc_->save(path);
    if (ok) {
        if (collabOn_) {
            Collab::appendHistory(path, collabNote_.isEmpty() ? QStringLiteral("저장") : collabNote_, QFileInfo(path).size());
            collabNote_.clear();
            Collab::touchLock(path);
        }
        addRecent(path);
        QString base = path;
        if (base.endsWith(".ony", Qt::CaseInsensitive)) base.chop(4);
        else if (base.endsWith(".tvpx", Qt::CaseInsensitive)) base.chop(5);
        // 다른 클립은 <이름>.clipN.ony, 클립 이름은 <이름>.clips 에 기록
        for (int i = 1; i < clipNames_.size(); ++i) {
            const QString dst = QString("%1.clip%2.ony").arg(base).arg(i + 1);
            QFile::remove(dst);
            if (!clipFiles_[i].isEmpty()) QFile::copy(clipFiles_[i], dst);
        }
        for (int i = clipNames_.size() + 1; QFile::exists(QString("%1.clip%2.ony").arg(base).arg(i)); ++i)
            QFile::remove(QString("%1.clip%2.ony").arg(base).arg(i));
        QFile nf(base + ".clips");
        if (clipNames_.size() > 1) {
            if (nf.open(QIODevice::WriteOnly | QIODevice::Text)) nf.write(clipNames_.join('\n').toUtf8());
        } else {
            nf.remove();
        }
    }
    if (keep != 0 && keep < clipNames_.size()) switchClip(keep);
    return ok;
}

void MainWindow::resetClips() {
    clipNames_ = QStringList{QStringLiteral("클립 1")};
    clipFiles_ = QStringList{QString()};
    clipIdx_ = 0;
}

void MainWindow::loadClipSiblings(const QString& path) {
    resetClips();
    QString base = path;
    if (base.endsWith(".ony", Qt::CaseInsensitive)) base.chop(4);
        else if (base.endsWith(".tvpx", Qt::CaseInsensitive)) base.chop(5);
    QStringList names;
    QFile nf(base + ".clips");
    if (nf.open(QIODevice::ReadOnly | QIODevice::Text)) names = QString::fromUtf8(nf.readAll()).split('\n', Qt::SkipEmptyParts);
    if (!names.isEmpty()) clipNames_[0] = names[0];
    for (int i = 2; QFile::exists(QString("%1.clip%2.ony").arg(base).arg(i)); ++i) {
        const QString dst = clipDir_.filePath(QString("open_clip%1.ony").arg(i));
        QFile::remove(dst);
        if (!QFile::copy(QString("%1.clip%2.ony").arg(base).arg(i), dst)) break;
        clipNames_ << (i - 1 < names.size() ? names[i - 1] : QString("클립 %1").arg(i));
        clipFiles_ << dst;
    }
}

bool MainWindow::saveProject() {
    if (doc_->path().isEmpty()) return saveProjectAs();
    if (collabOn_ && collabReadOnly_) {
        const auto r = QMessageBox::question(this, "보기 전용",
                                             "지금은 보기 전용입니다 (다른 사람이 쓰는 중).\n"
                                             "그래도 저장하면 그쪽 작업이 사라질 수 있습니다.\n\n"
                                             "다른 이름으로 저장할까요?",
                                             QMessageBox::Save | QMessageBox::Yes | QMessageBox::Cancel);
        if (r == QMessageBox::Cancel) return false;
        if (r == QMessageBox::Save) return saveProjectAs();
    }
    if (!writeProject(doc_->path())) {
        QMessageBox::warning(this, "저장 실패", "파일을 저장할 수 없습니다.");
        return false;
    }
    statusBar()->showMessage("저장했습니다.", 3000);
    return true;
}

bool MainWindow::saveProjectAs() {
    QString path = QFileDialog::getSaveFileName(this, "다른 이름으로 저장", QString(), "ONY 프로젝트 (*.ony)");
    if (path.isEmpty()) return false;
    if (!path.endsWith(".ony", Qt::CaseInsensitive)) path += ".ony";
    if (!writeProject(path)) {
        QMessageBox::warning(this, "저장 실패", "파일을 저장할 수 없습니다.");
        return false;
    }
    statusBar()->showMessage("저장했습니다.", 3000);
    return true;
}

void MainWindow::exportCurrentPng() {
    QString path = QFileDialog::getSaveFileName(this, "PNG로 내보내기", QString(), "PNG 이미지 (*.png)");
    if (path.isEmpty()) return;
    if (!path.endsWith(".png", Qt::CaseInsensitive)) path += ".png";
    if (!doc_->exportFrame(doc_->currentFrame(), path)) QMessageBox::warning(this, "내보내기 실패", "파일을 저장할 수 없습니다.");
}

void MainWindow::exportVideoDialog() {
    if (playTimer_.isActive()) togglePlay();
    QDialog dlg(this);
    dlg.setWindowTitle("내보내기");
    dlg.setMinimumWidth(420);
    auto* form = new QFormLayout(&dlg);
    auto* fmt = new QComboBox;
    fmt->addItem("MP4 동영상 (H.264)", "mp4");
    fmt->addItem("MOV 동영상 (ProRes, 투명 배경 가능)", "mov");
    fmt->addItem("WebM 동영상 (VP9, 투명 배경 가능)", "webm");
    fmt->addItem("GIF 애니메이션", "gif");
    fmt->addItem("PNG 이미지 시퀀스", "png");
    fmt->addItem("JPG 이미지 시퀀스", "jpg");
    fmt->addItem("BMP 이미지 시퀀스", "bmp");
    fmt->addItem("TIFF 이미지 시퀀스", "tiff");
    QSettings st("Ony", "Ony");
    fmt->setCurrentIndex(std::max(0, fmt->findData(st.value("export/format", "mp4"))));

    auto* range = new QComboBox;
    range->addItem("전체 프레임");
    range->addItem("현재 프레임부터 끝까지");
    const bool hasLoop = loopA_ >= 0;
    range->addItem(hasLoop ? QString("반복 구간 (%1–%2)").arg(loopA_ + 1).arg(loopB_ + 1) : QString("반복 구간 (지정 안 됨)"));
    range->addItem("직접 지정");
    auto* fromSp = new QSpinBox;
    auto* toSp = new QSpinBox;
    fromSp->setRange(1, doc_->frameCount());
    toSp->setRange(1, doc_->frameCount());
    fromSp->setValue(1);
    toSp->setValue(doc_->frameCount());
    auto* custom = new QWidget;
    auto* ch = new QHBoxLayout(custom);
    ch->setContentsMargins(0, 0, 0, 0);
    ch->addWidget(fromSp);
    ch->addWidget(new QLabel("~"));
    ch->addWidget(toSp);
    custom->setVisible(false);
    connect(range, &QComboBox::currentIndexChanged, &dlg, [custom](int i) { custom->setVisible(i == 3); });

    auto* quality = new QComboBox;
    quality->addItems({"높음", "보통", "낮음 (작은 파일)"});
    quality->setCurrentIndex(1);
    auto* scale = new QComboBox;
    for (int s : {200, 150, 100, 75, 50, 25}) scale->addItem(QString("%1%  (%2 x %3)").arg(s).arg(doc_->renderSize().width() * s / 100).arg(doc_->renderSize().height() * s / 100), s);
    scale->setCurrentIndex(2);
    auto* fps = new QComboBox;
    fps->addItem(QString("프로젝트와 같게 (%1 fps)").arg(doc_->fps()), 0);
    for (int f : {8, 12, 15, 24, 25, 30, 48, 60}) fps->addItem(QString("%1 fps").arg(f), f);
    auto* bg = new QComboBox;
    bg->addItem("흰색", "white");
    bg->addItem("검정", "black");
    bg->addItem("투명 (MOV / WebM / PNG / TIFF)", "none");
    bg->addItem("색 지정...", "custom");
    QColor customBg(0x20, 0x80, 0xff);
    connect(bg, &QComboBox::activated, &dlg, [&](int i) {
        if (bg->itemData(i).toString() != "custom") return;
        const QColor c = QColorDialog::getColor(customBg, &dlg, "배경색");
        if (c.isValid()) customBg = c;
    });
    auto* camera = new QCheckBox("카메라 시점으로 내보내기");
    camera->setChecked(doc_->cameraEnabled());
    camera->setEnabled(doc_->cameraEnabled());
    auto* audio = new QCheckBox("프로젝트의 소리 넣기");
    audio->setChecked(!doc_->audioPath().isEmpty());
    audio->setEnabled(!doc_->audioPath().isEmpty());
    auto* bgExport = new QCheckBox("백그라운드로 내보내기 (프레임을 뽑은 뒤 작업을 이어서 할 수 있습니다)");
    bgExport->setToolTip(QStringLiteral("프레임을 뽑는 동안에는 기다려야 하고, 영상으로 묶는 동안에는 작업할 수 있습니다."));

    form->addRow("형식", fmt);
    form->addRow("범위", range);
    form->addRow("", custom);
    form->addRow("화질", quality);
    form->addRow("출력 크기", scale);
    form->addRow("프레임 속도", fps);
    form->addRow("배경", bg);
    form->addRow(camera);
    form->addRow(audio);
    form->addRow(bgExport);
    const QString ffmpeg = VideoExport::findFfmpeg();
    auto* note = new QLabel;
    note->setWordWrap(true);
    note->setStyleSheet("color:#a0a0a0;");
    form->addRow(note);
    // 형식에 따라 쓸 수 없는 옵션을 끈다
    auto refresh = [&] {
        const QString f = fmt->currentData().toString();
        const bool video = f == "mp4" || f == "mov" || f == "webm" || f == "gif";
        quality->setEnabled(f == "mp4" || f == "mov" || f == "webm");
        fps->setEnabled(video);
        audio->setEnabled(f != "gif" && video && !doc_->audioPath().isEmpty());
        const bool alpha = f == "mov" || f == "webm" || f == "png" || f == "tiff";
        const int noneIdx = bg->findData("none");
        qobject_cast<QStandardItemModel*>(bg->model())->item(noneIdx)->setEnabled(alpha);
        if (!alpha && bg->currentIndex() == noneIdx) bg->setCurrentIndex(0);
        if (video) note->setText(ffmpeg.isEmpty() ? "ffmpeg 를 찾지 못했습니다. [파일] > [ffmpeg 위치 확인 / 지정] 에서 직접 고를 수 있습니다." : "인코더: " + QDir::toNativeSeparators(ffmpeg));
        else note->setText("선택한 구간의 프레임을 번호가 붙은 이미지 파일로 저장합니다.");
    };
    connect(fmt, &QComboBox::currentIndexChanged, &dlg, refresh);
    refresh();
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(bb);
    if (dlg.exec() != QDialog::Accepted) return;
    st.setValue("export/format", fmt->currentData());

    VideoOptions opt;
    opt.format = fmt->currentData().toString();
    opt.quality = quality->currentIndex();
    opt.scalePercent = scale->currentData().toInt();
    opt.audio = audio->isChecked();
    opt.fps = fps->currentData().toInt();
    opt.useCamera = camera->isChecked();
    const QString bgKey = bg->currentData().toString();
    opt.background = bgKey == "black" ? QColor(Qt::black) : bgKey == "none" ? QColor(0, 0, 0, 0) : bgKey == "custom" ? customBg : QColor(Qt::white);
    opt.from = 0;
    opt.to = doc_->frameCount() - 1;
    switch (range->currentIndex()) {
    case 1: opt.from = doc_->currentFrame(); break;
    case 2:
        if (hasLoop) { opt.from = loopA_; opt.to = loopB_; }
        break;
    case 3:
        opt.from = std::min(fromSp->value(), toSp->value()) - 1;
        opt.to = std::max(fromSp->value(), toSp->value()) - 1;
        break;
    default: break;
    }
    const QString base = doc_->path().isEmpty() ? QStringLiteral("ony") : QFileInfo(doc_->path()).completeBaseName();
    const bool seq = opt.format == "png" || opt.format == "jpg" || opt.format == "bmp" || opt.format == "tiff";
    QString out;
    if (seq) {
        out = QFileDialog::getExistingDirectory(this, "이미지를 저장할 폴더");
        if (out.isEmpty()) return;
        opt.baseName = base;
    } else {
        const QString ext = opt.format;
        out = QFileDialog::getSaveFileName(this, "동영상 저장", base + "." + ext, QString("%1 (*.%1)").arg(ext));
        if (out.isEmpty()) return;
        if (!out.endsWith("." + ext, Qt::CaseInsensitive)) out += "." + ext;
    }

    // 백그라운드 내보내기: 프레임 렌더는 여기서(문서를 읽어야 하므로), 영상 묶기는 다른 스레드에서
    if (bgExport->isChecked() && !seq) {
        if (exportBusy_) {
            QMessageBox::information(this, "내보내기", "이미 백그라운드로 내보내는 중입니다. 끝난 뒤 다시 해 주세요.");
            return;
        }
        auto* tmp = new QTemporaryDir;
        if (!tmp->isValid()) {
            delete tmp;
            QMessageBox::warning(this, "내보내기 실패", "임시 폴더를 만들 수 없습니다.");
            return;
        }
        tmp->setAutoRemove(true);
        QProgressDialog rp("프레임을 뽑는 중... (끝나면 작업을 이어서 할 수 있습니다)", "취소", 0, 100, this);
        rp.setWindowModality(Qt::WindowModal);
        rp.setMinimumDuration(0);
        const QString rerr = VideoExport::renderFrames(doc_, tmp->path(), opt, [&](int done, int total) {
            rp.setValue(total > 0 ? done * 100 / total : 0);
            QApplication::processEvents();
            return !rp.wasCanceled();
        });
        rp.close();
        if (!rerr.isEmpty()) {
            delete tmp;
            if (rerr != "취소됨") QMessageBox::warning(this, "내보내기 실패", rerr);
            return;
        }
        exportBusy_ = true;
        const QString dirPath = tmp->path();
        const int fpsFallback = doc_->fps();
        const QString audioPath = doc_->audioPath();
        const double audioStart = double(opt.from) / std::max(1, doc_->fps());
        auto* th = QThread::create([this, dirPath, out, opt, fpsFallback, audioPath, audioStart] {
            const QString e = VideoExport::encodeFrames(dirPath, out, opt, fpsFallback, audioPath, audioStart);
            QMetaObject::invokeMethod(this, [this, e, out] {
                exportBusy_ = false;
                if (e.isEmpty()) statusBar()->showMessage("내보내기를 마쳤습니다: " + QDir::toNativeSeparators(out), 15000);
                else QMessageBox::warning(this, "내보내기 실패", e);
            }, Qt::QueuedConnection);
        });
        connect(th, &QThread::finished, this, [th, tmp] {
            delete tmp;
            th->deleteLater();
        });
        th->start();
        statusBar()->showMessage(QStringLiteral("백그라운드에서 영상으로 묶는 중입니다. 그동안 작업을 이어서 하세요."), 15000);
        return;
    }

    QProgressDialog prog("프레임을 렌더링하고 저장하는 중...", "취소", 0, 100, this);
    prog.setWindowModality(Qt::WindowModal);
    prog.setMinimumDuration(0);
    const QString err = VideoExport::run(doc_, out, opt, [&](int done, int total) {
        prog.setValue(total > 0 ? done * 100 / total : 0);
        QApplication::processEvents();
        return !prog.wasCanceled();
    });
    prog.close();
    if (err.isEmpty()) {
        statusBar()->showMessage("내보냈습니다: " + QDir::toNativeSeparators(out), 8000);
    } else if (err != "취소됨") {
        QMessageBox::warning(this, "내보내기 실패", err);
    }
}

void MainWindow::exportPngSequence() {
    const QString dir = QFileDialog::getExistingDirectory(this, "내보낼 폴더 선택");
    if (dir.isEmpty()) return;
    int ok = 0;
    for (int f = 0; f < doc_->frameCount(); ++f) {
        const QString p = QDir(dir).filePath(QString("frame_%1.png").arg(f + 1, 4, 10, QChar('0')));
        if (doc_->exportFrame(f, p)) ++ok;
    }
    statusBar()->showMessage(QString("%1 / %2 프레임을 내보냈습니다.").arg(ok).arg(doc_->frameCount()), 5000);
}

void MainWindow::exportTimesheetPdf() {
    QString path = QFileDialog::getSaveFileName(this, "타임시트 PDF로 내보내기", QString(), "PDF (*.pdf)");
    if (path.isEmpty()) return;
    if (!path.endsWith(".pdf", Qt::CaseInsensitive)) path += ".pdf";

    QPdfWriter pdf(path);
    pdf.setPageSize(QPageSize(QPageSize::A4));
    pdf.setResolution(100);
    pdf.setTitle("타임시트");
    QPainter p(&pdf);
    if (!p.isActive()) {
        QMessageBox::warning(this, "내보내기 실패", "PDF 파일을 만들 수 없습니다.");
        return;
    }
    const int layers = doc_->layerCount();
    const int frames = doc_->frameCount();
    const int pageW = pdf.width(), pageH = pdf.height();
    const int margin = 40, headerH = 70, rowH = 20;
    const int rowsPerPage = (pageH - 2 * margin - headerH) / rowH;
    const int numW = 48;
    const int colW = std::min(120, (pageW - 2 * margin - numW) / std::max(1, layers));

    QFont f = p.font();
    f.setPointSize(8);
    p.setFont(f);
    int page = 0;
    for (int start = 0; start < frames; start += rowsPerPage, ++page) {
        if (page > 0) pdf.newPage();
        p.setPen(Qt::black);
        const QString name = doc_->path().isEmpty() ? QString("이름 없는 프로젝트") : QFileInfo(doc_->path()).fileName();
        p.drawText(margin, margin + 14, QString("타임시트 - %1   (%2 fps, %3 프레임)   %4쪽").arg(name).arg(doc_->fps()).arg(frames).arg(page + 1));
        const int top = margin + headerH - rowH;
        p.drawRect(margin, top, numW, rowH);
        p.drawText(QRect(margin, top, numW, rowH), Qt::AlignCenter, "프레임");
        for (int c = 0; c < layers; ++c) {
            const QRect r(margin + numW + c * colW, top, colW, rowH);
            p.drawRect(r);
            p.drawText(r.adjusted(3, 0, -3, 0), Qt::AlignVCenter | Qt::AlignLeft, doc_->layer(layers - 1 - c).name);
        }
        for (int i = 0; i < rowsPerPage && start + i < frames; ++i) {
            const int fr = start + i;
            const int y = top + rowH * (i + 1);
            p.drawRect(margin, y, numW, rowH);
            p.drawText(QRect(margin, y, numW, rowH), Qt::AlignCenter, QString::number(fr + 1));
            for (int c = 0; c < layers; ++c) {
                const int l = layers - 1 - c;
                const QRect r(margin + numW + c * colW, y, colW, rowH);
                p.drawRect(r);
                const int k = doc_->keyIndexAt(l, fr);
                if (k >= 0) p.drawText(r, Qt::AlignCenter, k == fr ? QString::number(k + 1) : QStringLiteral("|"));
                if ((fr + 1) % 6 == 0) {
                    p.save();
                    p.setPen(QColor(60, 90, 200));
                    p.drawLine(margin, y + rowH, margin + numW + layers * colW, y + rowH);
                    p.restore();
                }
            }
        }
    }
    p.end();
    statusBar()->showMessage("타임시트를 PDF로 내보냈습니다.", 4000);
}

// ---------------------------------------------------------------- 재생
void MainWindow::gotoFrame(int f) {
    doc_->setCurrentFrame(f);
}

void MainWindow::setProxyChoice(int idx) {
    proxyChoice_ = idx == 1 ? 2 : (idx == 2 ? 4 : 1);
    if (playTimer_.isActive()) doc_->setProxyDivisor(proxyChoice_);
}

void MainWindow::togglePlay() {
    if (playTimer_.isActive()) {
        playTimer_.stop();
        doc_->setProxyDivisor(1);
        emit doc_->changed();
        if (player_) player_->pause();
    } else {
        playTimer_.setInterval(std::max(1, int(1000 / (doc_->fps() * (speedBox_ ? speedBox_->currentData().toDouble() : 1.0)))));
        playDir_ = 1;
        doc_->setProxyDivisor(proxyChoice_);
        if (loopModeBtn_->isChecked() && loopA_ >= 0 && (doc_->currentFrame() < loopA_ || doc_->currentFrame() > loopB_))
            doc_->setCurrentFrame(loopA_);
        playTimer_.start();
        if (player_ && !doc_->audioPath().isEmpty()) {
            player_->setPosition(qint64(doc_->currentFrame()) * 1000 / doc_->fps());
            player_->play();
        }
    }
}

void MainWindow::stepPlay() {
    const bool useLoop = loopModeBtn_->isChecked() && loopA_ >= 0;
    const int lo = useLoop ? loopA_ : 0;
    const int hi = useLoop ? loopB_ : doc_->frameCount() - 1;
    int next = doc_->currentFrame() + playDir_;
    if (pingBtn_ && pingBtn_->isChecked()) {
        // 왕복: 끝에 닿으면 방향을 바꾼다
        if (next > hi) { playDir_ = -1; next = std::max(lo, hi - 1); }
        else if (next < lo) { playDir_ = 1; next = std::min(hi, lo + 1); }
    } else if (useLoop && next > hi) {
        next = lo;
    } else if (next >= doc_->frameCount()) {
        if (loopBox_->isChecked()) {
            next = 0;
            if (player_ && !doc_->audioPath().isEmpty()) player_->setPosition(0);
        } else {
            togglePlay();
            return;
        }
    }
    doc_->setCurrentFrame(std::clamp(next, 0, doc_->frameCount() - 1));
}

void MainWindow::updateTitle() {
    const QString name = doc_->path().isEmpty() ? QStringLiteral("이름 없는 프로젝트") : QFileInfo(doc_->path()).fileName();
    QString tag;
    if (collabOn_ && !doc_->path().isEmpty()) {
        const Collab::LockInfo i = Collab::checkLock(doc_->path());
        if (collabReadOnly_) tag = QStringLiteral("   [보기 전용]");
        else if (i.mine) tag = QString("   [%1 사용 중]").arg(Collab::userName());
        else if (i.locked) tag = QString("   [%1 사용 중]").arg(i.user);
    }
    setWindowTitle(QString("ONY - %1%2%3").arg(name, doc_->modified() ? "*" : "", tag));
}

void MainWindow::updateInfo() {
    infoCoord_->setText(QString("X: %1  Y: %2").arg(cursorX_).arg(cursorY_));
    QString col = QStringLiteral("RGBA: -");
    if (cursorX_ >= 0 && cursorY_ >= 0 && cursorX_ < doc_->size().width() && cursorY_ < doc_->size().height()) {
        if (!hoverCacheValid_) {
            hoverCache_ = doc_->composite(doc_->currentFrame());
            hoverCacheValid_ = true;
        }
        const QColor c = hoverCache_.pixelColor(cursorX_, cursorY_);
        col = QString("RGBA: %1, %2, %3, %4").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
    }
    infoColor_->setText(col);
    const int n = doc_->frameCount();
    infoProject_->setText(QString("프로젝트: %1  1–%2 (%3 프레임)")
                              .arg(doc_->path().isEmpty() ? QStringLiteral("이름 없음") : QFileInfo(doc_->path()).completeBaseName())
                              .arg(n).arg(n));
    infoClip_->setText(QString("클립 %1/%2: %3  1–%4 (%5 프레임)").arg(clipIdx_ + 1).arg(clipNames_.size()).arg(clipNames_[clipIdx_]).arg(n).arg(n));
    const int l = doc_->currentLayer();
    int first = -1, last = -1, cnt = 0;
    for (int f = 0; f < n; ++f)
        if (!doc_->layer(l).frames[f].isNull()) {
            if (first < 0) first = f;
            last = f;
            ++cnt;
        }
    infoLayer_->setText(QString("레이어 %1/%2: %3  %4 (%5 이미지)")
                            .arg(l + 1).arg(doc_->layerCount()).arg(doc_->layer(l).name)
                            .arg(first < 0 ? QStringLiteral("비어 있음") : QString("%1–%2").arg(first + 1).arg(last + 1))
                            .arg(cnt));
    infoImage_->setText(QString("이미지 %1").arg(doc_->currentFrame() + 1));
    const QString projName = doc_->path().isEmpty() ? QStringLiteral("이름 없음") : QFileInfo(doc_->path()).completeBaseName();
    syncClipTabs();
    canvasInfo_->setText(QString("프로젝트: \"%1\"  클립: \"%2\"  %3%  0°  현재 레이어: \"%4\"  현재 이미지: %5%6")
                             .arg(projName, clipNames_[clipIdx_])
                             .arg(canvas_->zoom() * 100.0, 0, 'f', 2)
                             .arg(doc_->layer(l).name)
                             .arg(doc_->currentFrame() + 1)
                             .arg(doc_->path().isEmpty() ? QString() : QString("  (%1)").arg(QDir::toNativeSeparators(doc_->path()))));
    zoomLabel_->setText(QString("%1%").arg(canvas_->zoom() * 100.0, 0, 'f', 2));
}

// ---------------------------------------------------------------- 스크럽(손) / 서랍 자동 숨김
bool MainWindow::eventFilter(QObject* o, QEvent* e) {
    if (o == clipTabs_ && e->type() == QEvent::MouseButtonDblClick) {
        renameClip();
        syncClipTabs();
        return true;
    }
    if (o == scrubBtn_) {
        if (e->type() == QEvent::MouseButtonPress) {
            scrubActive_ = true;
            scrubOrigin_ = doc_->currentFrame();
            if (playTimer_.isActive()) togglePlay();
        } else if (e->type() == QEvent::MouseMove && scrubActive_) {
            auto* me = static_cast<QMouseEvent*>(e);
            const QPoint p = timeline_->mapFromGlobal(me->globalPosition().toPoint());
            const int f = (p.x() - TimelineWidget::kNameW) / timeline_->cell();
            gotoFrame(std::clamp(f, 0, doc_->frameCount() - 1));
        } else if (e->type() == QEvent::MouseButtonRelease && scrubActive_) {
            scrubActive_ = false;
            gotoFrame(scrubOrigin_);
        }
    }
    return QMainWindow::eventFilter(o, e);
}

void MainWindow::drawerTick() {
    const QPoint pos = mapFromGlobal(QCursor::pos());
    const bool inside = rect().contains(pos);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    auto handle = [&](bool locked, bool& hidden, QList<QDockWidget*>& saved, qint64& lastIn, const QList<QDockWidget*>& docks, bool leftSide) {
        if (locked) {
            if (hidden) {
                for (auto* d : saved) d->show();
                saved.clear();
                hidden = false;
            }
            return;
        }
        // 가장자리에 마우스를 대면 나타나고, 서랍 밖으로 1.2초 있으면 숨는다.
        bool over = false;
        if (inside) {
            if (hidden) over = leftSide ? pos.x() < 8 : pos.x() > width() - 8;
            else
                for (auto* d : docks)
                    if (d->isVisible() && d->geometry().adjusted(-4, -4, 4, 4).contains(pos)) over = true;
        }
        if (over) lastIn = now;
        if (hidden && over) {
            for (auto* d : saved) d->show();
            saved.clear();
            hidden = false;
        } else if (!hidden && !over && now - lastIn > 1200) {
            saved.clear();
            for (auto* d : docks)
                if (d->isVisible() && !d->isFloating()) {
                    saved.push_back(d);
                    d->hide();
                }
            hidden = !saved.isEmpty();
        }
    };
    handle(leftLocked_, leftHidden_, leftHiddenDocks_, leftLastIn_, {dockMain_, dockColor_, dockTool_}, true);
    handle(rightLocked_, rightHidden_, rightHiddenDocks_, rightLastIn_,
           {dockBrush_, dockLibrary_, dockTimesheet_, dockFx_, dockCamera_, dockStory_, dockScript_}, false);
}

// ---------------------------------------------------------------- 메시 워프 / 스캔 / XDTS
void MainWindow::meshWarpDialog() {
    const int l = doc_->currentLayer(), k = doc_->keyIndexAt(l, doc_->currentFrame());
    if (k < 0 || doc_->layer(l).locked) {
        statusBar()->showMessage(QStringLiteral("편집 가능한 이미지가 없습니다."), 3000);
        return;
    }
    MeshWarpDialog dlg(doc_->keyImage(l, k), this);
    if (dlg.exec() != QDialog::Accepted) return;
    const QImage res = dlg.result();
    applyEffect("메시 워프", [&](const QImage&) { return res; });
}

void MainWindow::scanImportDialog() {
    const QString path = QFileDialog::getOpenFileName(this, "스캔 이미지 선택", QString(), "이미지 (*.png *.jpg *.jpeg *.bmp *.tif *.tiff)");
    if (path.isEmpty()) return;
    QImage scan(path);
    if (scan.isNull()) {
        QMessageBox::warning(this, "가져오기 실패", "이미지를 읽을 수 없습니다.");
        return;
    }
    const int l = doc_->currentLayer(), f = doc_->currentFrame();
    if (doc_->layer(l).locked) {
        statusBar()->showMessage(QStringLiteral("잠긴 레이어입니다."), 3000);
        return;
    }
    // 프로젝트 크기에 맞춰 흰 종이 위에 배치
    QImage fit(doc_->size(), QImage::Format_ARGB32_Premultiplied);
    fit.fill(Qt::white);
    {
        QPainter p(&fit);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        const QImage sc = scan.scaled(doc_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        p.drawImage((fit.width() - sc.width()) / 2, (fit.height() - sc.height()) / 2, sc);
    }
    QDialog dlg(this);
    dlg.setWindowTitle("스캔 클리너");
    auto* form = new QFormLayout(&dlg);
    auto* th = new QSpinBox; th->setRange(1, 255); th->setValue(170);
    auto* ds = new QSpinBox; ds->setRange(0, 500); ds->setValue(6);
    auto* useA = new QCheckBox("현재 A 색으로 선 칠하기");
    auto* prev = new QLabel;
    prev->setMinimumSize(360, 220);
    prev->setAlignment(Qt::AlignCenter);
    form->addRow("임계값 (이보다 어두우면 선)", th);
    form->addRow("얼룩 제거 (픽셀 이하)", ds);
    form->addRow(useA);
    form->addRow(prev);
    const QColor ink = useA->isChecked() ? canvas_->colorA() : QColor(Qt::black);
    auto render = [&] {
        const QColor c = useA->isChecked() ? canvas_->colorA() : QColor(Qt::black);
        const QImage r = Extras::scanClean(fit, th->value(), ds->value(), c);
        QImage bg(r.size(), QImage::Format_ARGB32_Premultiplied);
        bg.fill(Qt::white);
        QPainter p(&bg);
        p.drawImage(0, 0, r);
        prev->setPixmap(QPixmap::fromImage(bg).scaled(prev->minimumSize(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    };
    Q_UNUSED(ink);
    connect(th, &QSpinBox::valueChanged, &dlg, render);
    connect(ds, &QSpinBox::valueChanged, &dlg, render);
    connect(useA, &QCheckBox::toggled, &dlg, render);
    render();
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(bb);
    if (dlg.exec() != QDialog::Accepted) return;
    const QImage cleaned = Extras::scanClean(fit, th->value(), ds->value(), useA->isChecked() ? canvas_->colorA() : QColor(Qt::black));
    doc_->pushKeyEdit(l, f, doc_->layer(l).frames[f], cleaned, QStringLiteral("스캔 가져오기"));
}

// ---------------------------------------------------------------- 동영상 가져오기
void MainWindow::importVideoDialog() {
    const QString path = QFileDialog::getOpenFileName(this, "가져올 동영상", QString(),
                                                      "동영상 (*.mp4 *.mov *.avi *.mkv *.webm *.m4v *.wmv *.gif);;모든 파일 (*)");
    if (path.isEmpty()) return;
    statusBar()->showMessage(QStringLiteral("동영상 정보를 읽는 중..."), 2000);
    QApplication::processEvents();
    const VideoInfo info = VideoExport::probe(path);
    QDialog dlg(this);
    dlg.setWindowTitle("동영상 가져오기");
    dlg.setMinimumWidth(430);
    auto* f = new QFormLayout(&dlg);
    f->addRow(new QLabel(info.ok ? QString("%1\n%2 x %3,  %4 fps,  길이 %5초%6")
                                       .arg(QFileInfo(path).fileName())
                                       .arg(info.size.width()).arg(info.size.height())
                                       .arg(info.fps, 0, 'f', 2).arg(info.duration, 0, 'f', 1)
                                       .arg(info.hasAudio ? QStringLiteral(",  소리 있음") : QString())
                                 : QString("%1\n(정보를 읽지 못했습니다. ffmpeg 가 있으면 가져오기는 될 수 있습니다.)").arg(QFileInfo(path).fileName())));
    auto* fps = new QDoubleSpinBox;
    fps->setRange(0, 120);
    fps->setDecimals(2);
    fps->setSpecialValueText("원본 그대로");
    fps->setValue(doc_->fps());
    fps->setSuffix(" fps");
    auto* start = new QDoubleSpinBox; start->setRange(0, 100000); start->setDecimals(2); start->setSuffix(" 초");
    auto* dur = new QDoubleSpinBox; dur->setRange(0, 100000); dur->setDecimals(2); dur->setSpecialValueText("끝까지"); dur->setSuffix(" 초");
    auto* maxf = new QSpinBox; maxf->setRange(1, 20000); maxf->setValue(600); maxf->setSuffix(" 프레임");
    auto* where = new QComboBox;
    where->addItems({"새 레이어에 넣기", "현재 레이어에 넣기"});
    auto* at = new QSpinBox; at->setRange(1, std::max(1, doc_->frameCount())); at->setValue(doc_->currentFrame() + 1);
    auto* grow = new QCheckBox("필요하면 프로젝트 길이를 늘리기");
    grow->setChecked(true);
    auto* setFps = new QCheckBox("프로젝트 프레임 속도를 이 값으로 맞추기");
    auto* withAudio = new QCheckBox("동영상의 소리도 가져오기");
    withAudio->setChecked(info.hasAudio);
    withAudio->setEnabled(info.hasAudio);
    f->addRow("가져올 속도", fps);
    f->addRow("시작 위치", start);
    f->addRow("가져올 길이", dur);
    f->addRow("최대 프레임 수", maxf);
    f->addRow("넣을 곳", where);
    f->addRow("시작 프레임", at);
    f->addRow(grow);
    f->addRow(setFps);
    f->addRow(withAudio);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    f->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;

    ImportOptions io;
    io.fps = fps->value();
    io.startSec = start->value();
    io.durationSec = dur->value();
    io.maxFrames = maxf->value();
    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        QMessageBox::warning(this, "동영상 가져오기", "임시 폴더를 만들 수 없습니다.");
        return;
    }
    QProgressDialog prog("동영상에서 그림을 뽑는 중...", "취소", 0, 100, this);
    prog.setWindowModality(Qt::WindowModal);
    prog.setMinimumDuration(0);
    QStringList files;
    const QString err = VideoExport::extractFrames(path, tmp.path(), io, &files, [&](int done, int total) {
        prog.setValue(total > 0 ? std::min(99, done * 100 / total) : 0);
        QApplication::processEvents();
        return !prog.wasCanceled();
    });
    if (!err.isEmpty()) {
        prog.close();
        if (err != "취소됨") QMessageBox::warning(this, "동영상 가져오기 실패", err);
        return;
    }
    const int l = where->currentIndex() == 0 ? doc_->addLayer(QFileInfo(path).completeBaseName()) : doc_->currentLayer();
    if (doc_->layer(l).locked) {
        prog.close();
        statusBar()->showMessage(QStringLiteral("잠긴 레이어입니다."), 3000);
        return;
    }
    const int startFrame = at->value() - 1;
    if (grow->isChecked() && startFrame + files.size() > doc_->frameCount()) doc_->setFrameCount(startFrame + files.size());
    int ok = 0;
    for (int i = 0; i < files.size(); ++i) {
        const int fr = startFrame + i;
        if (fr >= doc_->frameCount()) break;
        QImage src(files[i]);
        if (src.isNull()) continue;
        QImage fit(doc_->size(), QImage::Format_ARGB32_Premultiplied);
        fit.fill(Qt::transparent);
        QPainter p(&fit);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        const QImage sc = src.size() == doc_->size() ? src : src.scaled(doc_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        p.drawImage((fit.width() - sc.width()) / 2, (fit.height() - sc.height()) / 2, sc);
        p.end();
        doc_->setKeyImage(l, fr, fit);
        ++ok;
        prog.setValue(std::min(99, int(100 * qint64(i) / std::max<qsizetype>(1, files.size()))));
        QApplication::processEvents();
        if (prog.wasCanceled()) break;
    }
    if (setFps->isChecked() && fps->value() > 0) doc_->setFps(int(std::round(fps->value())));
    if (withAudio->isChecked() && info.hasAudio) {
        const QString wav = backupDir() + "/" + QFileInfo(path).completeBaseName() + "_소리.wav";
        QDir().mkpath(backupDir());
        const QString ff = VideoExport::findFfmpeg();
        if (!ff.isEmpty()) {
            QProcess ap;
            QStringList aargs{"-y", "-v", "error"};
            if (io.startSec > 0) aargs << "-ss" << QString::number(io.startSec, 'f', 3);
            aargs << "-i" << path;
            if (io.durationSec > 0) aargs << "-t" << QString::number(io.durationSec, 'f', 3);
            aargs << "-vn" << wav;
            ap.start(ff, aargs);
            ap.waitForFinished(120000);
            if (ap.exitCode() == 0 && QFile::exists(wav)) {
                doc_->addAudioTrack(wav, QFileInfo(path).completeBaseName());
                syncAudio();
            }
        }
    }
    doc_->undoStack()->clear();
    doc_->notifyStructure();
    prog.close();
    statusBar()->showMessage(QString("동영상에서 %1 프레임을 가져왔습니다 (레이어: %2).").arg(ok).arg(doc_->layer(l).name), 8000);
}

// ---------------------------------------------------------------- PSD
void MainWindow::importPsdDialog() {
    const QString path = QFileDialog::getOpenFileName(this, "가져올 PSD", QString(), "포토샵 파일 (*.psd)");
    if (path.isEmpty()) return;
    Psd::File pf;
    const QString err = Psd::read(path, &pf);
    if (!err.isEmpty()) {
        QMessageBox::warning(this, "PSD 가져오기 실패", err);
        return;
    }
    const int nLayers = pf.layers.size();
    const QString msg = QString("%1\n크기 %2 x %3, 레이어 %4개\n\n현재 프레임(%5)에 넣습니다. 캔버스 크기가 다르면 가운데 맞춤으로 들어갑니다.")
                            .arg(QFileInfo(path).fileName()).arg(pf.size.width()).arg(pf.size.height())
                            .arg(nLayers).arg(doc_->currentFrame() + 1);
    QMessageBox box(QMessageBox::Question, "PSD 가져오기", msg, QMessageBox::Cancel, this);
    QPushButton* asLayers = nLayers > 0 ? box.addButton("레이어 그대로 가져오기", QMessageBox::AcceptRole) : nullptr;
    QPushButton* flat = box.addButton("한 장으로 합쳐서 가져오기", QMessageBox::AcceptRole);
    box.exec();
    QAbstractButton* clicked = box.clickedButton();
    if (clicked != static_cast<QAbstractButton*>(asLayers) && clicked != static_cast<QAbstractButton*>(flat)) return;
    auto place = [this](const QImage& src) {
        QImage fit(doc_->size(), QImage::Format_ARGB32_Premultiplied);
        fit.fill(Qt::transparent);
        QPainter p(&fit);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        const QImage sc = src.size() == doc_->size() ? src : src.scaled(doc_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        p.drawImage((fit.width() - sc.width()) / 2, (fit.height() - sc.height()) / 2, sc);
        return fit;
    };
    const int f = doc_->currentFrame();
    int made = 0;
    if (clicked == static_cast<QAbstractButton*>(asLayers)) {
        for (const Psd::LayerData& ld : pf.layers) {
            const int l = doc_->addLayer(ld.name);
            doc_->layer(l).opacity = std::clamp(ld.opacity, 0, 255) / 255.0;
            doc_->layer(l).visible = ld.visible;
            doc_->layer(l).blend = std::clamp(Psd::onyModeFromBlendKey(ld.blend), 0, Blend::Count - 1);
            doc_->setKeyImage(l, f, place(ld.image));
            ++made;
        }
    } else {
        QImage m = pf.merged;
        if (m.isNull()) {
            m = QImage(pf.size, QImage::Format_ARGB32_Premultiplied);
            m.fill(Qt::transparent);
            QPainter p(&m);
            for (const Psd::LayerData& ld : pf.layers) {
                if (!ld.visible) continue;
                p.setOpacity(std::clamp(ld.opacity, 0, 255) / 255.0);
                p.drawImage(0, 0, ld.image);
            }
        }
        const int l = doc_->addLayer(QFileInfo(path).completeBaseName());
        doc_->setKeyImage(l, f, place(m));
        made = 1;
    }
    doc_->undoStack()->clear();
    doc_->notifyStructure();
    statusBar()->showMessage(QString("PSD 에서 레이어 %1개를 가져왔습니다.").arg(made), 7000);
}

void MainWindow::exportPsdDialog() {
    QDialog dlg(this);
    dlg.setWindowTitle("PSD로 내보내기");
    auto* f = new QFormLayout(&dlg);
    auto* what = new QComboBox;
    what->addItems({"현재 프레임 (레이어 그대로)", "현재 프레임 (한 장으로 합쳐서)"});
    auto* fr = new QSpinBox;
    fr->setRange(1, doc_->frameCount());
    fr->setValue(doc_->currentFrame() + 1);
    auto* cam = new QCheckBox("카메라 시점으로 내보내기");
    cam->setEnabled(doc_->cameraEnabled());
    f->addRow("내보낼 것", what);
    f->addRow("프레임", fr);
    f->addRow(cam);
    f->addRow(new QLabel("PSD 는 8비트 RGB 로 저장합니다. 레이어 이름과 불투명도, 숨김 상태가 함께 들어갑니다."));
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    f->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;
    const QString base = doc_->path().isEmpty() ? QStringLiteral("ony") : QFileInfo(doc_->path()).completeBaseName();
    QString out = QFileDialog::getSaveFileName(this, "PSD 저장", base + ".psd", "포토샵 파일 (*.psd)");
    if (out.isEmpty()) return;
    if (!out.endsWith(".psd", Qt::CaseInsensitive)) out += ".psd";
    const int frame = fr->value() - 1;
    const bool useCam = cam->isChecked() && doc_->cameraEnabled();
    QVector<Psd::LayerData> ls;
    QImage merged;
    if (what->currentIndex() == 0) {
        for (int l = 0; l < doc_->layerCount(); ++l) {
            const int k = doc_->resolveKey(l, frame);
            Psd::LayerData ld;
            ld.name = doc_->layer(l).name;
            ld.opacity = std::clamp(int(doc_->layer(l).opacity * 255 + 0.5), 0, 255);
            ld.visible = doc_->layer(l).visible;
            ld.blend = Psd::blendKeyFromOny(doc_->layer(l).blend);
            QImage im(doc_->size(), QImage::Format_ARGB32_Premultiplied);
            im.fill(Qt::transparent);
            if (k >= 0) {
                QPainter p(&im);
                p.drawImage(0, 0, doc_->keyImage(l, k));
            }
            ld.image = im;
            ls.push_back(ld);
        }
        merged = doc_->renderFrame(frame, QColor(0, 0, 0, 0), useCam);
    } else {
        merged = doc_->renderFrame(frame, QColor(0, 0, 0, 0), useCam);
    }
    const QSize sz = merged.isNull() ? doc_->size() : merged.size();
    if (!ls.isEmpty() && sz != doc_->size()) ls.clear();   // 렌더 카메라로 크기가 달라지면 레이어 없이 저장
    const QString err = Psd::write(out, sz, ls, merged);
    if (err.isEmpty()) statusBar()->showMessage("PSD 로 저장했습니다: " + QDir::toNativeSeparators(out), 8000);
    else QMessageBox::warning(this, "PSD 내보내기 실패", err);
}

void MainWindow::importSequenceDialog() {
    QStringList files = QFileDialog::getOpenFileNames(this, "이미지 시퀀스 선택 (파일 이름 순서대로 프레임에 들어갑니다)", QString(),
                                                      "이미지 (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.webp)");
    if (files.isEmpty()) return;
    files.sort(Qt::CaseInsensitive);
    const int l = doc_->currentLayer();
    if (doc_->layer(l).locked) {
        statusBar()->showMessage(QStringLiteral("잠긴 레이어입니다."), 3000);
        return;
    }
    const int start = doc_->currentFrame();
    if (start + files.size() > doc_->frameCount()) doc_->setFrameCount(start + files.size());
    int ok = 0;
    for (int i = 0; i < files.size(); ++i) {
        QImage src(files[i]);
        if (src.isNull()) continue;
        QImage fit(doc_->size(), QImage::Format_ARGB32_Premultiplied);
        fit.fill(Qt::transparent);
        QPainter p(&fit);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        const QImage sc = src.size() == doc_->size() ? src : src.scaled(doc_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        p.drawImage((fit.width() - sc.width()) / 2, (fit.height() - sc.height()) / 2, sc);
        p.end();
        doc_->setKeyImage(l, start + i, fit);
        ++ok;
    }
    doc_->undoStack()->clear();
    doc_->notifyStructure();
    statusBar()->showMessage(QString("%1 / %2 개 이미지를 가져왔습니다.").arg(ok).arg(files.size()), 4000);
}

void MainWindow::exportXdtsDialog() {
    QString path = QFileDialog::getSaveFileName(this, "XDTS 내보내기", QString(), "XDTS (*.xdts)");
    if (path.isEmpty()) return;
    if (!path.endsWith(".xdts", Qt::CaseInsensitive)) path += ".xdts";
    QString err;
    if (!Extras::exportXdts(doc_, path, &err)) QMessageBox::warning(this, "내보내기 실패", err);
    else statusBar()->showMessage("XDTS 를 내보냈습니다.", 4000);
}

// ---------------------------------------------------------------- 다중 클립
void MainWindow::syncClipFiles() {
    // 현재 클립 상태를 임시 파일에 기록한다 (프로젝트 경로/수정 여부는 그대로 유지).
    const QString keepPath = doc_->path();
    const bool keepMod = doc_->modified();
    if (clipFiles_[clipIdx_].isEmpty()) clipFiles_[clipIdx_] = clipDir_.filePath(QString("clip%1_%2.ony").arg(clipIdx_).arg(QDateTime::currentMSecsSinceEpoch()));
    doc_->save(clipFiles_[clipIdx_]);
    doc_->setPath(keepPath);
    doc_->setModified(keepMod);
}

void MainWindow::switchClip(int idx) {
    if (idx == clipIdx_ || idx < 0 || idx >= clipNames_.size()) return;
    if (playTimer_.isActive()) togglePlay();
    const QString keepPath = doc_->path();
    const bool keepMod = doc_->modified();
    const QSize sz = doc_->size();
    const int fps = doc_->fps(), frames = doc_->frameCount();
    syncClipFiles();
    clipIdx_ = idx;
    if (clipFiles_[idx].isEmpty() || !QFile::exists(clipFiles_[idx])) doc_->newProject(sz, frames, fps);
    else doc_->load(clipFiles_[idx]);
    doc_->setPath(keepPath);
    doc_->setModified(keepMod);
    syncAudio();
    updateInfo();
    updateTitle();
    canvas_->zoomFit();
}

void MainWindow::syncClipTabs() {
    if (!clipTabs_) return;
    bool same = clipTabs_->count() == clipNames_.size() && clipTabs_->currentIndex() == clipIdx_;
    for (int i = 0; same && i < clipNames_.size(); ++i) same = clipTabs_->tabText(i) == clipNames_[i];
    if (same) return;
    QSignalBlocker b(clipTabs_);
    while (clipTabs_->count()) clipTabs_->removeTab(0);
    for (const QString& n : clipNames_) clipTabs_->addTab(n);
    clipTabs_->setCurrentIndex(clipIdx_);
    if (clipScroll_) {
        clipTabs_->resize(clipTabs_->sizeHint().width(), 32);
        clipScroll_->ensureWidgetVisible(clipTabs_, clipTabs_->tabRect(clipIdx_).right() + 4, 0);
    }
}

void MainWindow::newClip() {
    const QSize sz = doc_->size();
    const int fps = doc_->fps(), frames = doc_->frameCount();
    clipNames_ << QString("클립 %1").arg(clipNames_.size() + 1);
    clipFiles_ << QString();
    // 새 클립은 같은 크기/프레임 속도로 비어 있게 시작
    Q_UNUSED(sz); Q_UNUSED(fps); Q_UNUSED(frames);
    switchClip(clipNames_.size() - 1);
    doc_->setModified(true);
}

// 현재 클립을 그대로 복사해 새 클립으로 만든다
void MainWindow::duplicateClip() {
    syncClipFiles();
    const QString src = clipFiles_[clipIdx_];
    const QString dst = clipDir_.filePath(QString("clipdup%1_%2.ony").arg(clipNames_.size()).arg(QDateTime::currentMSecsSinceEpoch()));
    if (src.isEmpty() || !QFile::copy(src, dst)) {
        statusBar()->showMessage(QStringLiteral("클립을 복제하지 못했습니다."), 4000);
        return;
    }
    clipNames_ << (clipNames_[clipIdx_] + QStringLiteral(" 복사"));
    clipFiles_ << dst;
    switchClip(clipNames_.size() - 1);
    doc_->setModified(true);
    syncClipTabs();
    rebuildClipMenu();
    statusBar()->showMessage(QString("\"%1\" 을 만들었습니다.").arg(clipNames_.last()), 5000);
}

// 다른 클립의 프레임을 현재 클립 뒤에 이어 붙인다
void MainWindow::mergeClip() {
    if (clipNames_.size() < 2) {
        statusBar()->showMessage(QStringLiteral("이어 붙일 다른 클립이 없습니다."), 4000);
        return;
    }
    syncClipFiles();
    QStringList items;
    QVector<int> ids;
    for (int i = 0; i < clipNames_.size(); ++i) {
        if (i == clipIdx_) continue;
        items << clipNames_[i];
        ids << i;
    }
    bool ok = false;
    const QString s = QInputDialog::getItem(this, "클립 이어 붙이기", "현재 클립 뒤에 붙일 클립:", items, 0, false, &ok);
    if (!ok) return;
    const int other = ids.value(items.indexOf(s), -1);
    if (other < 0 || clipFiles_[other].isEmpty() || !QFile::exists(clipFiles_[other])) {
        statusBar()->showMessage(QStringLiteral("그 클립은 아직 내용이 없습니다."), 4000);
        return;
    }
    Document src;
    if (!src.load(clipFiles_[other])) {
        statusBar()->showMessage(QStringLiteral("그 클립을 읽지 못했습니다."), 4000);
        return;
    }
    if (src.size() != doc_->size()) {
        QMessageBox::warning(this, "클립 이어 붙이기",
                             QString("캔버스 크기가 달라 붙일 수 없습니다.\n현재 %1 x %2 / 상대 %3 x %4")
                                 .arg(doc_->size().width()).arg(doc_->size().height())
                                 .arg(src.size().width()).arg(src.size().height()));
        return;
    }
    const int at = doc_->frameCount();
    const FrameClip fc = src.copyFrames(0, 0, src.frameCount() - 1, true);
    // 레이어 수가 모자라면 채운다
    while (doc_->layerCount() < fc.data.size()) doc_->addLayer(fc.layerNames.value(doc_->layerCount(), QStringLiteral("레이어")));
    if (doc_->pasteFrames(0, at, fc, true)) {
        doc_->setCurrentFrame(at);
        statusBar()->showMessage(QString("\"%1\" 의 %2 프레임을 %3 번 프레임 뒤에 붙였습니다.").arg(s).arg(fc.count).arg(at), 7000);
    } else {
        statusBar()->showMessage(QStringLiteral("붙이지 못했습니다."), 4000);
    }
}

void MainWindow::renameClip() {
    bool ok = false;
    const QString n = QInputDialog::getText(this, "클립 이름", "이름:", QLineEdit::Normal, clipNames_[clipIdx_], &ok);
    if (ok && !n.isEmpty()) {
        clipNames_[clipIdx_] = n;
        updateInfo();
    }
}

void MainWindow::deleteClip() {
    if (clipNames_.size() <= 1) {
        statusBar()->showMessage(QStringLiteral("마지막 클립은 삭제할 수 없습니다."), 3000);
        return;
    }
    const int del = clipIdx_;
    const int target = del == 0 ? 1 : del - 1;
    switchClip(target);
    const int newIdx = target > del ? target - 1 : target;
    clipNames_.removeAt(del);
    clipFiles_.removeAt(del);
    clipIdx_ = newIdx;
    doc_->setModified(true);
    updateInfo();
}

void MainWindow::rebuildClipMenu() {
    // 구분선 뒤의 항목(클립 전환 목록)만 다시 만든다.
    const auto acts = clipMenu_->actions();
    bool afterSep = false;
    for (QAction* a : acts) {
        if (afterSep) clipMenu_->removeAction(a), a->deleteLater();
        else if (a->isSeparator()) afterSep = true;
    }
    for (int i = 0; i < clipNames_.size(); ++i) {
        QAction* a = clipMenu_->addAction(clipNames_[i]);
        a->setCheckable(true);
        a->setChecked(i == clipIdx_);
        connect(a, &QAction::triggered, this, [this, i] { switchClip(i); });
    }
    clipMenu_->addSeparator();
    auto addAct = [&](const QString& t, auto fn, const QKeySequence& ks = QKeySequence()) {
        QAction* a = clipMenu_->addAction(t);
        if (!ks.isEmpty()) a->setShortcut(ks);
        connect(a, &QAction::triggered, this, fn);
        Shortcuts::instance().adopt(a, QStringLiteral("클립"));
    };
    addAct("프레임 삽입", [this] { doc_->insertFrame(doc_->currentFrame()); });
    addAct("프레임 삭제", [this] { doc_->deleteFrame(doc_->currentFrame()); });
    clipMenu_->addSeparator();
    addAct("프레임 복사", [this] { frameEdit(0); }, QKeySequence("Ctrl+Alt+C"));
    addAct("프레임 잘라내기 (뒤를 당김)", [this] { frameEdit(1); }, QKeySequence("Ctrl+Alt+X"));
    addAct("프레임 붙여넣기 (끼워 넣기)", [this] { frameEdit(2); }, QKeySequence("Ctrl+Alt+V"));
    addAct("프레임 붙여넣기 (덮어쓰기)", [this] { frameEdit(3); }, QKeySequence("Ctrl+Alt+Shift+V"));
    addAct("프레임 내용 지우기 (자리는 유지)", [this] { frameEdit(4); });
    addAct("프레임 삭제 (뒤를 당김)", [this] { frameEdit(5); });
    addAct("프레임 옮기기...", [this] { frameEdit(6); });
    clipMenu_->addSeparator();
    addAct("클립 복제", [this] { duplicateClip(); });
    addAct("다른 클립 이어 붙이기...", [this] { mergeClip(); });
    clipMenu_->addSeparator();
    addAct("프레임 편집 대상: 모든 레이어 / 현재 레이어 바꾸기", [this] {
        frameAllLayers_ = !frameAllLayers_;
        statusBar()->showMessage(frameAllLayers_ ? QStringLiteral("프레임 편집 대상: 모든 레이어") : QStringLiteral("프레임 편집 대상: 현재 레이어"), 4000);
    });
    clipMenu_->addSeparator();
    addAct("노출 늘리기", [this] {
        if (!doc_->extendExposure(doc_->currentLayer(), doc_->currentFrame()))
            statusBar()->showMessage(QStringLiteral("노출을 늘릴 이미지가 없습니다."), 3000);
        else gotoFrame(doc_->currentFrame() + 1);
    }, QKeySequence("Ctrl+Right"));
    addAct("노출 줄이기", [this] {
        if (!doc_->shrinkExposure(doc_->currentLayer(), doc_->currentFrame()))
            statusBar()->showMessage(QStringLiteral("다음 칸이 새 이미지라서 줄일 수 없습니다."), 3000);
    }, QKeySequence("Ctrl+Left"));
    clipMenu_->addSeparator();
    addAct("첫 프레임", [this] { gotoFrame(0); }, QKeySequence(Qt::Key_Home));
    addAct("마지막 프레임", [this] { gotoFrame(doc_->frameCount() - 1); }, QKeySequence(Qt::Key_End));
    addAct("재생 / 정지", [this] { togglePlay(); });
}

// ---------------------------------------------------------------- 사용자 패널
void MainWindow::rebuildCustomMenu() {
    customMenu_->clear();
    customMenu_->addAction("사용자 패널 만들기", this, [this] {
        CustomPanelDef d;
        if (!editCustomPanel(this, &d)) return;
        customDefs_.push_back(d);
        saveCustomPanels(customDefs_);
        rebuildCustomMenu();
        openCustomPanel(customDefs_.size() - 1);
    });
    customMenu_->addAction("스크립트 콘솔 열기", this, [this] {
        dockScript_->show();
        dockScript_->raise();
    });
    customMenu_->addSeparator();
    for (int i = 0; i < customDefs_.size(); ++i) {
        QMenu* m = customMenu_->addMenu(customDefs_[i].name);
        m->addAction("열기", this, [this, i] { openCustomPanel(i); });
        m->addAction("편집", this, [this, i] {
            CustomPanelDef d = customDefs_[i];
            if (!editCustomPanel(this, &d)) return;
            customDefs_[i] = d;
            saveCustomPanels(customDefs_);
            rebuildCustomMenu();
        });
        m->addAction("삭제", this, [this, i] {
            customDefs_.removeAt(i);
            saveCustomPanels(customDefs_);
            rebuildCustomMenu();
        });
    }
}

void MainWindow::openCustomPanel(int i) {
    if (i < 0 || i >= customDefs_.size()) return;
    auto* d = new QDockWidget(customDefs_[i].name, this);
    d->setAttribute(Qt::WA_DeleteOnClose);
    auto* panel = new CustomPanel(doc_, customDefs_[i]);
    panel->setHost([this](const QStringList& t) { return scriptHostCommand(t); });
    connect(panel, &CustomPanel::layoutChanged, this, [this, i](const CustomPanelDef& nd) {
        if (i < 0 || i >= customDefs_.size()) return;
        customDefs_[i].geoms = nd.geoms;
        saveCustomPanels(customDefs_);
        statusBar()->showMessage(QStringLiteral("버튼 배치를 저장했습니다."), 2000);
    });
    d->setWidget(panel);
    addDockWidget(Qt::RightDockWidgetArea, d);
    d->setFloating(true);
    d->resize(240, 70 + 34 * customDefs_[i].labels.size());
    d->show();
}
