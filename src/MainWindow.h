#pragma once
#include <QColor>
#include <QImage>
#include <QList>
#include <QMainWindow>
#include <optional>
#include <QTemporaryDir>
#include <QTimer>
#include <QVector>
#include <functional>

#include "Brush.h"
#include "CanvasWidget.h"
#include "Extras.h"
#include "Warp.h"
#include "Plugins.h"
#include "MorePanels.h"

class Document;
class ColorPanel;
class ToolPanel;
class CameraPanel;
class TimelineWidget;
class QLabel;
class QDockWidget;
class QScrollArea;
class QToolButton;
class QSpinBox;
class QSlider;
class QCheckBox;
class QMediaPlayer;
class QAudioOutput;
class QAction;
class QMenu;
class QComboBox;
class QSplitter;
class QToolBar;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    void openLastProject();
    Document* document() const { return doc_; }
    struct PublishOptions {
        bool mp4 = true, gif = false, png = false, pdf = true, project = true, thumb = true;
        int quality = 1;
        std::function<bool(int, int)> progress;
    };
    QStringList publishTo(const QString& folder, const QString& name, const PublishOptions& po);
    bool runTransition(int layer, int f0, int n, int kind);
    class ColorPanel* colorPanel() const { return color_; }
    std::optional<QString> scriptHostCommand(const QStringList& tokens);
    bool savePalette(const QString& path) const;
    int loadPalette(const QString& path);
    bool lightTableOn() const { return lightAct_ && lightAct_->isChecked(); }
    CanvasWidget* canvas() const { return canvas_; }
    void applyTheme(int index);

protected:
    void closeEvent(QCloseEvent*) override;
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    struct HistEntry {
        CanvasWidget::Tool tool;
        int row;
        BrushParams params;
        QColor color;
    };

    void buildCentral();
    void buildDocks();
    void buildMenus();

    bool maybeSave();
    void newProjectDialog();
    void projectSettingsDialog();
    void ensureBlankKey();
    void nextFrameAction(bool forceNewImage);
    void openProject();
    bool saveProject();
    bool saveProjectAs();
    void exportPngSequence();
    void exportVideoDialog();
    void exportCurrentPng();
    void exportTimesheetPdf();
    void preferencesDialog();

    void transformDialog();
    void perspectiveDialog();
    void folderAction(int kind);
    void loadAudio();
    void updateWaveform();
    void syncAudio();

    void togglePlay();
    void stepPlay();
    void gotoFrame(int f);
    void updateTitle();
    void updateInfo();
    void applyRoom(int index);
    void applyRoomDefault(int index);
    void renameRoom();
    void saveRooms();
    void loadRooms();
    void addDockToggle(class QToolBar* tb, QDockWidget* dock, const QString& iconName, const QString& tip);
    void publishDialog();
    void printDialog();
    void renderCameraDialog();
    void vectorEditDialog(int kind);
    void inbetweenDialog();
    void lipSyncDialog();
    void referenceDialog();
    void boneWarpDialog();
    void checkCollabOnOpen(const QString& path);
    void collabDialog();
    void releaseCollabLock();
    void rebuildPluginMenu();
    void pluginManagerDialog();
    void runPluginCommand(const Plugins::Command& c);
    void symmetryDialog();
    void colorAdjustDialog(bool levelsMode);
    void undoHistoryDialog();
    void ffmpegDialog();
    void frameEdit(int kind);
    void gradientTextDialog();
    void strokeAnimationDialog();
    void transitionDialog(int kind, const QString& name);
    void applyEffect(const QString& text, const std::function<QImage(const QImage&)>& fn);
    void showHistory(const QPoint& globalPos);
    void applyHistory(const HistEntry& e);

    void meshWarpDialog();
    void scanImportDialog();
    void exportXdtsDialog();
    void importSequenceDialog();
    void importVideoDialog();
    void importPsdDialog();
    void exportPsdDialog();
    void newClip();
    void openPath(const QString& path);
    void setupAutosave();
    void addRecent(const QString& path);
    QString backupDir() const;
    QString templateDir() const;
    void switchClip(int idx);
    void renameClip();
    void duplicateClip();
    void mergeClip();
    void deleteClip();
    void rebuildClipMenu();
    void rebuildCustomMenu();
    void openCustomPanel(int i);
    void drawerTick();
    void setProxyChoice(int idx);
    void syncClipFiles();
    bool writeProject(const QString& path);
    void resetClips();
    void loadClipSiblings(const QString& path);

    Document* doc_;
    CanvasWidget* canvas_ = nullptr;
    ColorPanel* color_ = nullptr;
    ToolPanel* tool_ = nullptr;
    CameraPanel* cameraPanel_ = nullptr;
    TimelineWidget* timeline_ = nullptr;
    QScrollArea* tlScroll_ = nullptr;
    QToolButton* playBtn_ = nullptr;
    QToolButton* loopBox_ = nullptr;
    QToolButton* soundBtn_ = nullptr;
    QToolButton* autoNewBtn_ = nullptr;
    QToolButton* camViewBtn_ = nullptr;
    QAction* camViewAct_ = nullptr;
    QLabel *infoCoord_ = nullptr, *infoColor_ = nullptr, *infoProject_ = nullptr, *infoClip_ = nullptr, *infoLayer_ = nullptr, *infoImage_ = nullptr;
    QImage hoverCache_;
    bool hoverCacheValid_ = false;
    QSpinBox* fpsSpin_ = nullptr;
    QLabel* canvasInfo_ = nullptr;
    QLabel* zoomLabel_ = nullptr;
    class ScrubBar* scrub_ = nullptr;
    class WaveformBar* wave_ = nullptr;
    QString waveSource_;
    bool waveVisible_ = true;
    bool exportBusy_ = false;
    QSlider* layerOpacity_ = nullptr;
    class QTabBar* clipTabs_ = nullptr;
    void syncClipTabs();
    QTimer playTimer_;
    QImage clip_;
    int cursorX_ = 0, cursorY_ = 0;
    class QScrollArea* clipScroll_ = nullptr;
    QToolButton* pingBtn_ = nullptr;
    class QComboBox* speedBox_ = nullptr;
    int playDir_ = 1;
    QAction* recAct_ = nullptr;
    QAction* symAct_ = nullptr;
    FrameClip frameClip_;
    QVector<Warp::Bone> boneRig_;
    bool frameAllLayers_ = false;
    QStringList lastPublishErrors_;
    class QTimer* autosaveTimer_ = nullptr;
    int brushRow_ = 0;
    int lastPen_ = 0;
    QList<HistEntry> history_;
    QMediaPlayer* player_ = nullptr;
    QAudioOutput* audioOut_ = nullptr;
    // 소리 스크럽: 재생 중이 아닐 때 프레임을 옮기면 그 자리 소리를 짧게 들려준다
    bool audioScrub_ = true;
    QTimer scrubStop_;
    void scrubAudioAt(int frame);
    // 서랍 자동 숨김
    QTimer drawerTimer_;
    bool leftLocked_ = true, rightLocked_ = true;
    bool leftHidden_ = false, rightHidden_ = false;
    QList<QDockWidget*> leftHiddenDocks_, rightHiddenDocks_;
    qint64 leftLastIn_ = 0, rightLastIn_ = 0;
    // 재생 보조
    QToolButton* scrubBtn_ = nullptr;
    QToolButton* loopModeBtn_ = nullptr;
    bool scrubActive_ = false;
    int scrubOrigin_ = 0;
    int loopA_ = -1, loopB_ = -1;
    int proxyChoice_ = 1;
    // 다중 클립 (프로젝트 한 개 안에 여러 클립, 세션 단위로 전환)
    QStringList clipNames_{QStringLiteral("클립 1")};
    QStringList clipFiles_{QString()};
    int clipIdx_ = 0;
    QTemporaryDir clipDir_;
    QMenu* clipMenu_ = nullptr;
    QMenu* customMenu_ = nullptr;
    QMenu* pluginMenu_ = nullptr;
    bool collabOn_ = false;
    bool collabReadOnly_ = false;
    QString collabNote_;
    class QTimer* collabTimer_ = nullptr;
    QVector<Plugins::Plugin> plugins_;
    QVector<CustomPanelDef> customDefs_;
    QList<QDockWidget*> customDocks_;
    QDockWidget* dockScript_ = nullptr;
    QDockWidget* dockStory_ = nullptr;
    QDockWidget* dockNavigator_ = nullptr;
    QDockWidget* dockGuides_ = nullptr;
    QDockWidget* dockLight_ = nullptr;
    QDockWidget* dockRemote_ = nullptr;
    QDockWidget* dockFlip_ = nullptr;
    QDockWidget* dockShare_ = nullptr;
    QSplitter* bottomSplit_ = nullptr;
    QComboBox* roomCombo_ = nullptr;
    QStringList roomNames_{QStringLiteral("드로잉"), QStringLiteral("애니메이션"), QStringLiteral("컬러링"), QStringLiteral("스토리보드")};
    QVector<QByteArray> roomStates_{QByteArray(), QByteArray(), QByteArray(), QByteArray()};
    int curRoom_ = 0;
    QAction* lightAct_ = nullptr;
    QAction* timelineToggle_ = nullptr;
    QList<QDockWidget*> docks_;
    QDockWidget* dockMain_ = nullptr;
    QDockWidget* dockColor_ = nullptr;
    QDockWidget* dockTool_ = nullptr;
    QDockWidget* dockBrush_ = nullptr;
    BrushPanel* brushPanel_ = nullptr;
    QDockWidget* dockLibrary_ = nullptr;
    QDockWidget* dockTimesheet_ = nullptr;
    QDockWidget* dockFx_ = nullptr;
    QDockWidget* dockCamera_ = nullptr;
};
