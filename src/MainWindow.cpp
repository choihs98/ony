#include "MainWindow.h"
#include "CanvasWidget.h"
#include "Document.h"
#include "Extras.h"
#include "Effects.h"
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
#include <QPainter>
#include <QPalette>
#include <QPdfWriter>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSplitter>
#include <QTabWidget>
#include <QCursor>
#include <QDateTime>
#include <QFile>
#include <QImageReader>
#include <QMouseEvent>
#include <QShortcut>
#include <QSpinBox>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QUndoStack>
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
        playTimer_.setInterval(1000 / doc_->fps());
    });
    connect(doc_, &Document::currentChanged, this, [this] {
        updateInfo();
        const int x = TimelineWidget::kNameW + doc_->currentFrame() * TimelineWidget::kCell;
        tlScroll_->ensureVisible(x, 0, 60, 0);
    });
    connect(&playTimer_, &QTimer::timeout, this, &MainWindow::stepPlay);
    playTimer_.setInterval(1000 / doc_->fps());

    new QShortcut(QKeySequence(Qt::Key_N), this, [this] { color_->swapColors(); });
    new QShortcut(QKeySequence(Qt::Key_Left), this, [this] { gotoFrame(doc_->currentFrame() - 1); });
    new QShortcut(QKeySequence(Qt::Key_Right), this, [this] { gotoFrame(doc_->currentFrame() + 1); });
    new QShortcut(QKeySequence(Qt::Key_Return), this, [this] {
        if (canvas_->hasPendingWarp()) canvas_->commitWarp();
        else togglePlay();
    });

    updateTitle();
    updateInfo();
    applyRoomDefault(0);
    statusBar()->showMessage(QStringLiteral("준비"));
}

// ---------------------------------------------------------------- 테마
void MainWindow::applyTheme(int index) {
    struct Scheme { QColor window, base, text, button; };
    static const Scheme schemes[] = {
        {QColor(0x2a, 0x2d, 0x35), QColor(0x1f, 0x22, 0x2a), QColor(0xea, 0xec, 0xef), QColor(0x32, 0x36, 0x42)}, // 밤
        {QColor(0x3a, 0x3d, 0x46), QColor(0x2c, 0x2f, 0x38), QColor(0xea, 0xec, 0xef), QColor(0x45, 0x49, 0x56)}, // 황혼
        {QColor(0xe8, 0xe9, 0xed), QColor(0xff, 0xff, 0xff), QColor(0x1b, 0x1d, 0x22), QColor(0xf0, 0xf1, 0xf4)}, // 낮
        {QColor(0xc9, 0xcb, 0xd2), QColor(0xe1, 0xe3, 0xe8), QColor(0x1b, 0x1d, 0x22), QColor(0xd3, 0xd5, 0xdb)}, // 새벽
    };
    const Scheme& s = schemes[std::clamp(index, 0, 3)];
    QPalette p;
    p.setColor(QPalette::Window, s.window);
    p.setColor(QPalette::WindowText, s.text);
    p.setColor(QPalette::Base, s.base);
    p.setColor(QPalette::AlternateBase, s.base.darker(108));
    p.setColor(QPalette::Text, s.text);
    p.setColor(QPalette::Button, s.button);
    p.setColor(QPalette::ButtonText, s.text);
    p.setColor(QPalette::Highlight, QColor(0x4a, 0x90, 0xe2));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::ToolTipBase, s.base);
    p.setColor(QPalette::ToolTipText, s.text);
    qApp->setPalette(p);
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
    // 화면
    auto* disp = new QWidget;
    auto* df = new QFormLayout(disp);
    auto* theme = new QComboBox;
    theme->addItems({"밤 (기본)", "황혼", "낮", "새벽"});
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
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    top->addWidget(bb);
    if (dlg.exec() != QDialog::Accepted) return;
    st.setValue("theme", theme->currentIndex());
    st.setValue("undoLevels", undo->value());
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

    canvas_ = new CanvasWidget(doc_);
    lay->addWidget(canvas_, 1);

    // ---- 재생 바 (문서 순서: 소리, 반복, 카메라 보기, FPS, | 재생 컨트롤 | 재중앙) ----
    auto* pb = new QWidget;
    auto* ph = new QHBoxLayout(pb);
    ph->setContentsMargins(6, 2, 6, 2);
    ph->setSpacing(3);
    auto addBtn = [&](const QString& t, const QString& tip, auto fn, bool checkable = false) {
        auto* b = new QToolButton;
        b->setText(t);
        b->setToolTip(tip);
        b->setCheckable(checkable);
        if (checkable) connect(b, &QToolButton::toggled, this, fn);
        else connect(b, &QToolButton::clicked, this, fn);
        ph->addWidget(b);
        return b;
    };
    soundBtn_ = addBtn("소리", "소리 켜기/끄기", [this](bool on) {
        if (audioOut_) audioOut_->setMuted(!on);
    }, true);
    soundBtn_->setChecked(true);
    loopBox_ = addBtn("반복", "반복 재생 / 한 번만 재생", [](bool) {}, true);
    loopBox_->setChecked(true);
    camViewBtn_ = addBtn("프레임", "카메라 보기로 재생", [this](bool on) {
        canvas_->setCameraView(on);
        if (camViewAct_ && camViewAct_->isChecked() != on) camViewAct_->setChecked(on);
    }, true);
    ph->addWidget(new QLabel(QStringLiteral("FPS")));
    fpsSpin_ = new QSpinBox;
    fpsSpin_->setRange(1, 120);
    fpsSpin_->setValue(doc_->fps());
    connect(fpsSpin_, &QSpinBox::valueChanged, this, [this](int v) { doc_->setFps(v); });
    ph->addWidget(fpsSpin_);
    scrubBtn_ = new QToolButton;
    scrubBtn_->setText("✋");
    scrubBtn_->setToolTip(QStringLiteral("누른 채 좌우로 끌면 재생 헤드를 부드럽게 이동 (놓으면 원위치)"));
    scrubBtn_->installEventFilter(this);
    ph->addWidget(scrubBtn_);
    ph->addStretch();
    addBtn("▶", "재생 (Enter)", [this] {
        if (!playTimer_.isActive()) togglePlay();
    });
    addBtn("■", "정지", [this] {
        if (playTimer_.isActive()) togglePlay();
    });
    addBtn("|◀", "첫 프레임", [this] { gotoFrame(0); });
    addBtn("▶|", "마지막 프레임", [this] { gotoFrame(doc_->frameCount() - 1); });
    addBtn("◀◆", "이전 이미지(키)", [this] {
        const int l = doc_->currentLayer(), f = doc_->currentFrame();
        int k = doc_->keyIndexAt(l, f);
        if (k >= 0 && k == f) {
            int p = -1;
            for (int i = f - 1; i >= 0; --i)
                if (!doc_->layer(l).frames[i].isNull()) { p = i; break; }
            if (p >= 0) gotoFrame(p);
        } else if (k >= 0) gotoFrame(k);
    });
    addBtn("◆▶", "다음 이미지(키)", [this] {
        const int n = doc_->nextKeyIndex(doc_->currentLayer(), doc_->currentFrame());
        if (n >= 0) gotoFrame(n);
    });
    addBtn("◀", "이전 프레임 (←)", [this] { gotoFrame(doc_->currentFrame() - 1); });
    addBtn("▶", "다음 프레임 (→)", [this] { gotoFrame(doc_->currentFrame() + 1); });
    ph->addStretch();
    addBtn("⌖", "재생 헤드를 타임라인 중앙으로", [this] {
        const int x = TimelineWidget::kNameW + doc_->currentFrame() * TimelineWidget::kCell;
        tlScroll_->horizontalScrollBar()->setValue(x - tlScroll_->viewport()->width() / 2);
    });
    loopModeBtn_ = addBtn("구간", "루프 모드: Shift+타임라인 위쪽 눈금을 끌어 구간 지정", [this](bool on) {
        if (on && loopA_ < 0) {
            statusBar()->showMessage(QStringLiteral("타임라인 위쪽 눈금을 Shift+드래그해서 구간을 지정하세요."), 4000);
        }
    }, true);
    lay->addWidget(pb);

    // ---- 하단: 레이어 스택 | 타임라인 ----
    timeline_ = new TimelineWidget(doc_);
    tlScroll_ = new QScrollArea;
    tlScroll_->setWidget(timeline_);
    tlScroll_->setWidgetResizable(true);
    tlScroll_->setFrameShape(QFrame::NoFrame);
    auto* split = new QSplitter(Qt::Horizontal);
    bottomSplit_ = split;
    split->addWidget(new LayerStackPanel(doc_));
    split->addWidget(tlScroll_);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setSizes({260, 1000});
    split->setMinimumHeight(170);
    split->setMaximumHeight(280);
    lay->addWidget(split);

    // ---- 정보 바 (문서 순서: 펜 좌표, 색, 프록시, 프로젝트, 클립, 레이어, 현재 이미지) ----
    auto* info = new QWidget;
    auto* ih = new QHBoxLayout(info);
    ih->setContentsMargins(6, 2, 6, 2);
    ih->setSpacing(14);
    infoCoord_ = new QLabel;
    infoColor_ = new QLabel;
    auto* proxy = new QComboBox;
    proxy->addItems({"프록시: 끔", "프록시: 1/2", "프록시: 1/4"});
    proxy->setToolTip(QStringLiteral("프록시: 재생 중 낮은 해상도로 합성해 속도를 높입니다"));
    connect(proxy, &QComboBox::currentIndexChanged, this, [this](int i) { setProxyChoice(i); });
    infoProject_ = new QLabel;
    infoClip_ = new QLabel;
    infoLayer_ = new QLabel;
    infoImage_ = new QLabel;
    for (QWidget* w : {static_cast<QWidget*>(infoCoord_), static_cast<QWidget*>(infoColor_), static_cast<QWidget*>(proxy),
                       static_cast<QWidget*>(infoProject_), static_cast<QWidget*>(infoClip_), static_cast<QWidget*>(infoLayer_)})
        ih->addWidget(w);
    ih->addStretch();
    ih->addWidget(infoImage_);
    auto* house = new QToolButton;
    house->setText("⌂");
    house->setToolTip(QStringLiteral("룸: 이름 변경 / 불러오기 / 저장"));
    house->setPopupMode(QToolButton::InstantPopup);
    auto* hm = new QMenu(house);
    hm->addAction("이름 변경...", this, [this] { renameRoom(); });
    hm->addAction("불러오기...", this, [this] { loadRooms(); });
    hm->addAction("저장...", this, [this] { saveRooms(); });
    house->setMenu(hm);
    roomCombo_ = new QComboBox;
    roomCombo_->addItems(roomNames_);
    roomCombo_->setToolTip(QStringLiteral("룸 전환 (Ctrl+Alt 를 누르고 선택하면 현재 룸 설정을 복사)"));
    connect(roomCombo_, &QComboBox::currentIndexChanged, this, [this](int i) { applyRoom(i); });
    ih->addWidget(house);
    ih->addWidget(roomCombo_);
    lay->addWidget(info);

    setCentralWidget(central);

    connect(timeline_, &TimelineWidget::rangeChanged, this, [this](int a, int b) {
        loopA_ = a;
        loopB_ = b;
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
    dockBrush_ = makeDock("브러시 패널", brushPanel_, Qt::RightDockWidgetArea);
    dockLibrary_ = makeDock("라이브러리", new LibraryPanel(doc_), Qt::RightDockWidgetArea);
    dockTimesheet_ = makeDock("타임시트", new TimesheetPanel(doc_), Qt::RightDockWidgetArea);
    dockFx_ = makeDock("FX 스택", new FxPanel(doc_), Qt::RightDockWidgetArea);
    dockCamera_ = makeDock("카메라", cameraPanel_, Qt::RightDockWidgetArea);
    dockStory_ = makeDock("스토리보드", new StoryboardPanel(doc_), Qt::RightDockWidgetArea);
    dockScript_ = makeDock("스크립트 콘솔", new ScriptConsole(doc_), Qt::RightDockWidgetArea);
    tabifyDockWidget(dockCamera_, dockStory_);
    tabifyDockWidget(dockStory_, dockScript_);
    dockScript_->hide();

    auto* lt = new LightTablePanel(canvas_);
    dockNavigator_ = makeDock("내비게이터", new NavigatorPanel(doc_, canvas_), Qt::RightDockWidgetArea);
    dockGuides_ = makeDock("가이드라인", new GuidelinesPanel(canvas_), Qt::RightDockWidgetArea);
    dockLight_ = makeDock("라이트 테이블", lt, Qt::RightDockWidgetArea);
    auto* remote = new RemotePanel;
    dockRemote_ = makeDock("리모컨", remote, Qt::RightDockWidgetArea);
    auto* share = new ShareManagerPanel;
    dockShare_ = makeDock("공유 관리자", share, Qt::RightDockWidgetArea);
    for (QDockWidget* d : {dockNavigator_, dockGuides_, dockLight_, dockRemote_, dockShare_}) tabifyDockWidget(dockScript_, d);
    for (QDockWidget* d : {dockNavigator_, dockGuides_, dockLight_, dockRemote_, dockShare_}) d->hide();
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
    connect(main, &MainPanel::clearRequested, canvas_, &CanvasWidget::clearCurrent);
    connect(main, &MainPanel::smoothingToggled, canvas_, &CanvasWidget::setSmoothing);
    connect(main, &MainPanel::paperToggled, canvas_, &CanvasWidget::setPaper);
    connect(main, &MainPanel::stencilToggled, canvas_, &CanvasWidget::setStencil);
    connect(main, &MainPanel::historyRequested, this, &MainWindow::showHistory);
    connect(color_, &ColorPanel::colorAChanged, canvas_, &CanvasWidget::setColorA);
    connect(tool_, &ToolPanel::paramsChanged, canvas_, &CanvasWidget::setBrush);
    connect(tool_, &ToolPanel::brushSelected, this, [this](int row) {
        using T = CanvasWidget::Tool;
        brushRow_ = row;
        canvas_->setTool(brushPreset(row).eraser ? T::Eraser : T::Pen);
        if (brushPanel_) brushPanel_->setCurrent(row);
    });
    connect(brushPanel_, &BrushPanel::presetSelected, tool_, &ToolPanel::selectPreset);
    canvas_->setBrush(tool_->params());
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
        for (int i = 0; i < doc_->folderCount(); ++i) items << doc_->folder(i).name;
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
    default: break;
    }
}

// ---------------------------------------------------------------- 사운드
void MainWindow::loadAudio() {
    const QString path = QFileDialog::getOpenFileName(this, "사운드 트랙 불러오기", QString(), "오디오 (*.wav *.mp3 *.ogg *.flac *.m4a)");
    if (path.isEmpty()) return;
    doc_->setAudioPath(path);
    syncAudio();
}

void MainWindow::syncAudio() {
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
        return a;
    };

    // 파일
    QMenu* file = menuBar()->addMenu("파일");
    act(file, "새 프로젝트...", [this] { newProjectDialog(); }, QKeySequence::New);
    act(file, "프로젝트 열기...", [this] { openProject(); }, QKeySequence::Open);
    file->addSeparator();
    act(file, "저장", [this] { saveProject(); }, QKeySequence::Save);
    act(file, "다른 이름으로 저장...", [this] { saveProjectAs(); }, QKeySequence::SaveAs);
    file->addSeparator();
    act(file, "현재 프레임 PNG로 내보내기...", [this] { exportCurrentPng(); });
    act(file, "PNG 시퀀스로 내보내기...", [this] { exportPngSequence(); });
    act(file, "타임시트 PDF로 내보내기...", [this] { exportTimesheetPdf(); });
    act(file, "타임시트 XDTS로 내보내기...", [this] { exportXdtsDialog(); });
    act(file, "스캔 이미지 가져오기 (스캔 클리너)...", [this] { scanImportDialog(); });
    act(file, "이미지 시퀀스 가져오기 (PNG/JPG)...", [this] { importSequenceDialog(); });
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
    edit->addAction(undo);
    edit->addAction(redo);
    edit->addSeparator();
    act(edit, "잘라내기", [this] {
        const int k = doc_->keyIndexAt(doc_->currentLayer(), doc_->currentFrame());
        if (k < 0) return;
        clip_ = doc_->keyImage(doc_->currentLayer(), k);
        canvas_->clearCurrent();
    }, QKeySequence::Cut);
    act(edit, "복사", [this] {
        const int k = doc_->keyIndexAt(doc_->currentLayer(), doc_->currentFrame());
        if (k >= 0) clip_ = doc_->keyImage(doc_->currentLayer(), k);
    }, QKeySequence::Copy);
    act(edit, "붙여넣기", [this] {
        if (clip_.isNull() || clip_.size() != doc_->size()) return;
        const int l = doc_->currentLayer(), f = doc_->currentFrame();
        if (doc_->layer(l).locked) return;
        doc_->pushKeyEdit(l, f, doc_->layer(l).frames[f], clip_, QStringLiteral("붙여넣기"));
    }, QKeySequence::Paste);
    act(edit, "모두 선택", [this] { canvas_->selectAll(); }, QKeySequence::SelectAll);
    edit->addSeparator();
    act(edit, "환경설정...", [this] { preferencesDialog(); });

    // 프로젝트
    QMenu* proj = menuBar()->addMenu("프로젝트");
    act(proj, "프로젝트 정보", [this] {
        QMessageBox::information(this, "프로젝트 정보",
            QString("크기: %1 x %2\n프레임 수: %3\n프레임 속도: %4\n레이어: %5\n폴더: %6\n라이브러리: %7개")
                .arg(doc_->size().width()).arg(doc_->size().height()).arg(doc_->frameCount())
                .arg(doc_->fps()).arg(doc_->layerCount()).arg(doc_->folderCount()).arg(doc_->libraryCount()));
    });
    act(proj, "프레임 수 변경...", [this] {
        bool ok = false;
        const int n = QInputDialog::getInt(this, "프레임 수", "프레임 수:", doc_->frameCount(), 1, 5000, 1, &ok);
        if (ok) doc_->setFrameCount(n);
    });
    act(proj, "프레임 속도 변경...", [this] {
        bool ok = false;
        const int n = QInputDialog::getInt(this, "프레임 속도", "초당 프레임:", doc_->fps(), 1, 120, 1, &ok);
        if (ok) doc_->setFps(n);
    });
    proj->addSeparator();
    act(proj, "사운드 트랙 불러오기...", [this] { loadAudio(); });
    act(proj, "사운드 트랙 제거", [this] {
        doc_->setAudioPath(QString());
        syncAudio();
    });

    // 클립
    QMenu* clip = menuBar()->addMenu("클립");
    clipMenu_ = clip;
    act(clip, "새 클립", [this] { newClip(); });
    act(clip, "클립 이름 변경...", [this] { renameClip(); });
    act(clip, "클립 삭제", [this] { deleteClip(); });
    clip->addSeparator();
    connect(clip, &QMenu::aboutToShow, this, [this] { rebuildClipMenu(); });
    rebuildClipMenu();

    // 레이어
    QMenu* layer = menuBar()->addMenu("레이어");
    act(layer, "새 레이어", [this] { doc_->addLayer(QString("레이어 %1").arg(doc_->layerCount() + 1)); }, QKeySequence("Ctrl+Shift+N"));
    act(layer, "새 CTG 레이어 (색 + 선화)", [this] { doc_->addCtgPair(); });
    act(layer, "레이어 복제", [this] { doc_->duplicateLayer(doc_->currentLayer()); });
    act(layer, "레이어 삭제", [this] { doc_->removeLayer(doc_->currentLayer()); });
    layer->addSeparator();
    act(layer, "위로 이동", [this] { doc_->moveLayer(doc_->currentLayer(), +1); });
    act(layer, "아래로 이동", [this] { doc_->moveLayer(doc_->currentLayer(), -1); });
    layer->addSeparator();
    QMenu* folder = layer->addMenu("폴더");
    act(folder, "새 폴더 (현재 레이어 포함)...", [this] { folderAction(0); });
    act(folder, "현재 레이어를 폴더에 넣기...", [this] { folderAction(1); });
    act(folder, "폴더 보이기/숨기기...", [this] { folderAction(2); });
    act(folder, "폴더 불투명도...", [this] { folderAction(3); });
    act(folder, "폴더 삭제...", [this] { folderAction(4); });
    layer->addSeparator();
    act(layer, "아래 레이어와 병합", [this] {
        if (!doc_->mergeDown(doc_->currentLayer())) statusBar()->showMessage(QStringLiteral("합칠 아래 레이어가 없습니다."), 3000);
    });
    act(layer, "보이는 레이어 모두 병합", [this] {
        if (!doc_->mergeVisible()) statusBar()->showMessage(QStringLiteral("보이는 레이어가 2개 이상 필요합니다."), 3000);
    });
    act(layer, "레이어 이름 변경...", [this] {
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
    act(image, "변형...", [this] { transformDialog(); }, QKeySequence("Ctrl+T"));
    act(image, "원근...", [this] { perspectiveDialog(); });
    act(image, "메시 워프...", [this] { meshWarpDialog(); });
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
    QAction* lt = act(view, "라이트 테이블", [] {}, QKeySequence(Qt::Key_L));
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
    act(view, "전체 화면", [this] { setWindowState(windowState() ^ Qt::WindowFullScreen); }, QKeySequence(Qt::Key_V));
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
    act(help, "정보", [this] {
        QMessageBox::about(this, "정보", "Ony — 키 이미지(원화) 중심의 2D 프레임 애니메이션 도구 (개발 중)");
    });

    // 메뉴 아래 단축 아이콘 줄 (문서 순서: 파일 | 편집 | 패널 토글 | 도움말)
    auto* tb = addToolBar("단축");
    tb->setObjectName("단축");
    tb->setMovable(false);
    auto tbAct = [&](const QString& t, const QString& tip, auto fn) {
        QAction* a = tb->addAction(t);
        a->setToolTip(tip);
        connect(a, &QAction::triggered, this, fn);
    };
    tbAct("새로", "새 프로젝트", [this] { newProjectDialog(); });
    tbAct("열기", "프로젝트 열기", [this] { openProject(); });
    tbAct("저장", "프로젝트 저장", [this] { saveProject(); });
    tb->addSeparator();
    undo->setText("실행 취소");
    redo->setText("다시 실행");
    tb->addAction(undo);
    tb->addAction(redo);
    tb->addSeparator();
    addDockToggle(tb, dockMain_, "메인", "메인 패널");
    addDockToggle(tb, dockColor_, "색상", "색상 패널 (P)");
    addDockToggle(tb, dockTool_, "도구", "도구 패널 (Shift+A)");
    {
        QAction* a = tb->addAction("타임라인");
        a->setCheckable(true);
        a->setChecked(true);
        a->setToolTip(QStringLiteral("타임라인과 레이어 스택 (0)"));
        connect(a, &QAction::toggled, this, [this](bool on) { bottomSplit_->setVisible(on); });
        timelineToggle_ = a;
    }
    addDockToggle(tb, dockLight_, "라이트", "라이트 테이블 패널");
    addDockToggle(tb, dockRemote_, "리모컨", "리모컨 패널");
    addDockToggle(tb, dockNavigator_, "내비", "내비게이터 패널");
    addDockToggle(tb, dockFx_, "FX", "FX 스택");
    addDockToggle(tb, dockLibrary_, "라이브러리", "이미지 라이브러리");
    addDockToggle(tb, dockGuides_, "가이드", "가이드라인 패널");
    addDockToggle(tb, dockShare_, "공유", "공유 관리자");
    tb->addSeparator();
    auto* helpSearch = new QLineEdit;
    helpSearch->setPlaceholderText(QStringLiteral("? 매뉴얼 검색"));
    helpSearch->setMaximumWidth(180);
    connect(helpSearch, &QLineEdit::returnPressed, this, [helpSearch] {
        const QString q = helpSearch->text().trimmed();
        if (!q.isEmpty()) QDesktopServices::openUrl(QUrl("https://doc.tvpaint.com/search?q=" + QUrl::toPercentEncoding(q)));
    });
    tb->addWidget(helpSearch);

    new QShortcut(QKeySequence(Qt::Key_0), this, [this] { timelineToggle_->toggle(); });
    new QShortcut(QKeySequence(Qt::Key_P), this, [this] { dockColor_->setVisible(!dockColor_->isVisible()); });
    new QShortcut(QKeySequence("Shift+A"), this, [this] { dockTool_->setVisible(!dockTool_->isVisible()); });
}

void MainWindow::addDockToggle(QToolBar* tb, QDockWidget* dock, const QString& text, const QString& tip) {
    QAction* a = tb->addAction(text);
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
                                       dockScript_, dockNavigator_, dockGuides_, dockLight_, dockRemote_, dockShare_};
    QList<QDockWidget*> show;
    switch (index) {
    case 0: show = {dockBrush_, dockNavigator_}; break;
    case 1: show = {dockTimesheet_, dockLight_, dockCamera_, dockRemote_}; break;
    case 2: show = {dockFx_, dockLibrary_, dockGuides_}; break;
    default: show = {dockStory_, dockShare_}; break;
    }
    for (QDockWidget* d : right) d->setVisible(show.contains(d));
    for (QDockWidget* d : show) d->raise();
    if (!show.isEmpty()) show.first()->raise();
}

// ---------------------------------------------------------------- 파일
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
    if (maybeSave()) e->accept();
    else e->ignore();
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
    const QString path = QFileDialog::getOpenFileName(this, "프로젝트 열기", QString(), "Ony 프로젝트 (*.ony *.tvpx)");
    if (path.isEmpty()) return;
    if (playTimer_.isActive()) togglePlay();
    if (!doc_->load(path)) {
        QMessageBox::warning(this, "열기 실패", "프로젝트 파일을 읽을 수 없습니다.");
        return;
    }
    loadClipSiblings(path);
    syncAudio();
    canvas_->zoomFit();
}

bool MainWindow::writeProject(const QString& path) {
    const int keep = clipIdx_;
    if (clipNames_.size() > 1 && clipIdx_ != 0) switchClip(0);
    const bool ok = doc_->save(path);
    if (ok) {
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
    if (!writeProject(doc_->path())) {
        QMessageBox::warning(this, "저장 실패", "파일을 저장할 수 없습니다.");
        return false;
    }
    statusBar()->showMessage("저장했습니다.", 3000);
    return true;
}

bool MainWindow::saveProjectAs() {
    QString path = QFileDialog::getSaveFileName(this, "다른 이름으로 저장", QString(), "Ony 프로젝트 (*.ony)");
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
        playTimer_.setInterval(1000 / doc_->fps());
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
    int next = doc_->currentFrame() + 1;
    if (loopModeBtn_->isChecked() && loopA_ >= 0 && next > loopB_) next = loopA_;
    else if (next >= doc_->frameCount()) {
        if (loopBox_->isChecked()) {
            next = 0;
            if (player_ && !doc_->audioPath().isEmpty()) player_->setPosition(0);
        } else {
            togglePlay();
            return;
        }
    }
    doc_->setCurrentFrame(next);
}

void MainWindow::updateTitle() {
    const QString name = doc_->path().isEmpty() ? QStringLiteral("이름 없는 프로젝트") : QFileInfo(doc_->path()).fileName();
    setWindowTitle(QString("Ony - %1%2").arg(name, doc_->modified() ? "*" : ""));
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
}

// ---------------------------------------------------------------- 스크럽(손) / 서랍 자동 숨김
bool MainWindow::eventFilter(QObject* o, QEvent* e) {
    if (o == scrubBtn_) {
        if (e->type() == QEvent::MouseButtonPress) {
            scrubActive_ = true;
            scrubOrigin_ = doc_->currentFrame();
            if (playTimer_.isActive()) togglePlay();
        } else if (e->type() == QEvent::MouseMove && scrubActive_) {
            auto* me = static_cast<QMouseEvent*>(e);
            const QPoint p = timeline_->mapFromGlobal(me->globalPosition().toPoint());
            const int f = (p.x() - TimelineWidget::kNameW) / TimelineWidget::kCell;
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
    };
    addAct("프레임 삽입", [this] { doc_->insertFrame(doc_->currentFrame()); });
    addAct("프레임 삭제", [this] { doc_->deleteFrame(doc_->currentFrame()); });
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
    customMenu_->addAction("사용자 패널 만들기...", this, [this] {
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
        m->addAction("편집...", this, [this, i] {
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
    d->setWidget(new CustomPanel(doc_, customDefs_[i]));
    addDockWidget(Qt::RightDockWidgetArea, d);
    d->setFloating(true);
    d->resize(220, 40 + 34 * customDefs_[i].labels.size());
    d->show();
}
