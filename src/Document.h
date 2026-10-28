#pragma once
#include <QColor>
#include <QHash>
#include <QMap>
#include <QSet>
#include <QVariantMap>
#include <QObject>
#include <QImage>
#include <QPolygonF>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QTransform>
#include <QVector>
#include <algorithm>
#include <functional>

#include "Brush.h"
#include "Effects.h"

class QUndoStack;

struct CameraKey {
    qreal x = 0, y = 0;       // 카메라 중심의 이동량(픽셀)
    qreal scale = 1.0;        // 1.0 = 100%
    qreal rotation = 0;       // 도
    // 이 키에서 다음 키까지의 속도 곡선: 0 직선, 1 느리게 시작, 2 느리게 끝, 3 느리게 시작·끝, 4 계단(다음 키에서 한 번에 이동)
    int ease = 0;
};

// 벡터 레이어의 획 하나 (점을 나중에 다시 옮기거나 굵기·색을 바꿀 수 있다)
struct VecStroke {
    QVector<QPointF> pts;
    QVector<float> pressure;
    QColor color{Qt::black};
    double width = 6;
    bool eraser = false;
    bool closed = false;
    bool filled = false;
    QColor fill{Qt::transparent};
    bool visible = true;
};

struct Layer {
    QString name;
    bool visible = true;
    bool locked = false;
    qreal opacity = 1.0;
    // 프레임별 키 이미지. null 이면 직전 키 이미지가 그대로 노출(홀드)된다.
    QVector<QImage> frames;
    int folder = -1;          // 소속 폴더 인덱스 (-1: 없음)
    bool ctgLines = false;    // CTG 선화 레이어: 바로 아래 레이어가 색 레이어
    // 멀티플레인: 카메라가 움직일 때 이 레이어가 얼마나 따라갈지 (%).
    // 100 = 카메라와 똑같이(기준면), 작을수록 멀리 있는 배경처럼 덜 움직이고, 클수록 앞쪽처럼 더 움직인다.
    int camFollow = 100;
    QVector<Effects::Item> fx;
    // ---- 타임라인 옵션 (매뉴얼: Animation additional functions) ----
    int preBehavior = 0;      // 첫 이미지 이전: 0 없음, 1 반복, 2 핑퐁, 3 정지(홀드)
    int postBehavior = 3;     // 마지막 이미지 이후: 같은 값 (기본 3 = 마지막 이미지 유지)
    int endFrame = -1;        // 이 레이어 내용의 끝(미포함). -1 = 프로젝트 끝까지
    int blend = 0;            // Blend::Mode (0 = 색상)
    bool collapsed = false;   // 레이어 스택에서 접기
    int colorGroup = -1;      // 컬러 그룹 번호 (-1 = 없음)
    bool stencil = false;     // 이 레이어를 스텐실로 사용 (다른 레이어에는 불투명한 곳에만 그릴 수 있다)
    bool stencilInvert = false;
    bool preserveAlpha = false; // 알파 채널 보존: 이미 칠해진 곳에만 칠한다
    QMap<int, QString> instanceNames; // 인스턴스 이름 (키 프레임 -> 이름)
    QMap<int, int> imageMarks;        // 이미지 마크 (키 프레임 -> 마크 색 번호)
    // ---- 퍼펫 레이어: 키 이미지는 그대로 두고 이동/회전/크기를 키로 애니메이션한다 ----
    bool puppet = false;
    int puppetId = 0;                 // 레이어를 옮겨도 변하지 않는 번호 (부모 연결에 사용)
    int puppetParent = 0;             // 부모 퍼펫 레이어의 puppetId (0 = 없음)
    qreal pivotX = -1, pivotY = -1;   // 회전·크기 기준점 (그림 좌표, 음수 = 가운데)
    QMap<int, CameraKey> pkeys; // 프레임 -> 변환 키
    // ---- 벡터 레이어 ----
    bool vector = false;                          // 켜면 이 레이어의 그림은 벡터 획으로 보관된다
    QMap<int, QVector<VecStroke>> vstrokes;       // 키 프레임 -> 획 목록
};

struct Folder {
    QString name;
    bool visible = true;
    qreal opacity = 1.0;
    int parent = -1;          // 상위 폴더 인덱스 (-1 = 최상위). 폴더 중첩.
};

// 타임라인 프레임 클립보드
struct FrameClip {
    QSize size;
    int count = 0;                   // 프레임 수
    QVector<QVector<QImage>> data;   // [레이어][프레임] — null 이면 홀드(직전 이미지 유지)
    QStringList layerNames;          // 참고용 (여러 레이어를 복사했을 때)
    bool isEmpty() const { return count <= 0 || data.isEmpty(); }
};

// 드로잉 레코더가 기록한 획 하나
struct StrokeRec {
    int layer = 0, frame = 0;
    BrushParams brush;
    QColor color{Qt::black};
    bool eraser = false;
    QVector<QPointF> pts;
    QVector<float> pressure;
    QVector<int> ms;      // 획 시작부터의 시간(ms)
};

// 렌더 카메라: 프로젝트(그리는 캔버스) 크기와 별개로, 최종 렌더(내보내기·퍼블리시·인쇄)의 출력 크기와 맞춤 방식을 정한다.
struct RenderCamera {
    bool on = false;
    QSize size{1920, 1080};
    int fit = 0;   // 0 비율 유지(남는 곳은 배경색), 1 채우기(넘치는 곳은 잘림), 2 늘이기
};

class Document : public QObject {
    Q_OBJECT
public:
    explicit Document(QObject* parent = nullptr);

    void newProject(QSize size, int frameCount, int fps);
    // 캔버스 크기 변경. mode: 0 = 그림을 새 크기에 맞춰 늘이기/줄이기, 1 = 크기만 바꾸고 가운데 정렬(넘치면 잘림),
    // 2 = 크기만 바꾸고 왼쪽 위 기준. 실행 취소 기록은 지워진다.
    void resizeCanvas(QSize newSize, int mode);

    QSize size() const { return size_; }
    int fps() const { return fps_; }
    void setFps(int fps);
    int frameCount() const { return frameCount_; }

    // ---- 레이어 ----
    int layerCount() const { return layers_.size(); }
    Layer& layer(int i) { return layers_[i]; }
    const Layer& layer(int i) const { return layers_[i]; }
    int currentLayer() const { return curLayer_; }
    int currentFrame() const { return curFrame_; }
    void setCurrentLayer(int l);
    void setCurrentFrame(int f);

    int addLayer(const QString& name);
    void removeLayer(int i);
    int duplicateLayer(int i);
    bool mergeDown(int i);                    // i 번 레이어를 바로 아래 레이어에 합친다
    bool mergeVisible();                      // 보이는 레이어를 모두 한 레이어로 합친다
    void moveLayer(int i, int delta);
    int addCtgPair();                         // 색 레이어 + 선화 레이어를 함께 만든다. 선화 레이어 인덱스 반환

    // ---- 폴더 ----
    int folderCount() const { return folders_.size(); }
    Folder& folder(int i) { return folders_[i]; }
    const Folder& folder(int i) const { return folders_[i]; }
    int addFolder(const QString& name);
    void removeFolder(int i);
    void setLayerFolder(int layer, int folder);
    void setFolderParent(int folder, int parent);      // 폴더 중첩 (순환은 거절)
    int folderDepth(int folder) const;                 // 최상위 = 0
    bool folderVisibleEffective(int folder) const;     // 상위 폴더까지 따져 본 표시 여부
    qreal folderOpacityEffective(int folder) const;    // 상위 폴더까지 곱한 불투명도
    QString folderPath(int folder) const;              // "상위 / 하위" 형태

    // ---- 타임라인 프레임 편집 ----
    FrameClip copyFrames(int layer, int a, int b, bool allLayers) const;
    // insert = true 면 끼워 넣고(뒤를 민다), false 면 덮어쓴다. 성공하면 true.
    bool pasteFrames(int layer, int at, const FrameClip& c, bool insert);
    // shift = true 면 뒤 프레임을 당겨 길이를 줄이고, false 면 그 자리를 비운다.
    bool deleteFrames(int layer, int a, int b, bool allLayers, bool shift);
    bool moveFrames(int layer, int a, int b, int dest, bool allLayers);

    // ---- 키 이미지 ----
    int keyIndexAt(int layer, int frame) const;
    // 프리/포스트 비헤이비어와 레이어 끝까지 고려해, 이 프레임에 실제로 보일 키 이미지 번호 (없으면 -1)
    int resolveKey(int layer, int frame) const;
    int firstKey(int layer) const;
    int lastKey(int layer) const;
    // 레이어 내용이 끝나는 프레임(미포함). 이 뒤부터 포스트 비헤이비어가 적용된다.
    int contentEnd(int layer) const;
    int nextKeyIndex(int layer, int frame) const;
    QImage keyImage(int layer, int key) const;
    QImage* editableImage(int layer, int frame);
    void setKeyImage(int layer, int key, const QImage& img);

    // 노출(홀드) 조절: 현재 프레임 뒤에 빈 칸을 넣거나 뺀다 (해당 레이어만)
    bool extendExposure(int layer, int frame);
    bool shrinkExposure(int layer, int frame);

    // ---- 합성 ----
    QImage composite(int frame, bool paper = true, bool useCamera = false) const;
    // 이미 만들어 둔 합성 그림의 일부만 다시 그린다 (획을 그리는 동안 쓰면 훨씬 빠르다).
    // 블렌딩 모드나 카메라·프록시가 걸려 있으면 false 를 돌려주니, 그때는 composite() 를 쓰면 된다.
    bool compositeRegion(QImage& target, const QRect& region, int frame, bool paper) const;
    // 내보내기용: 배경색(alpha 0 = 투명)과 카메라 적용 여부를 지정해 원본 해상도로 합성한다
    QImage renderFrame(int frame, const QColor& background, bool useCamera) const;

    // ---- 프레임 ----
    void insertFrame(int at);
    void deleteFrame(int at);
    void setFrameCount(int n);

    // ---- FX 스택 ----
    void addFx(int layer, const Effects::Item& item);
    void removeFx(int layer, int index);

    // ---- 카메라 ----
    bool cameraEnabled() const { return cameraEnabled_; }
    void setCameraEnabled(bool on);
    CameraKey cameraAt(int frame) const;
    bool hasCameraKey(int frame) const { return cameraKeys_.contains(frame); }
    void setCameraKey(int frame, const CameraKey& k);
    void removeCameraKey(int frame);
    const QMap<int, CameraKey>& cameraKeys() const { return cameraKeys_; }
    QTransform cameraTransform(const CameraKey& k) const;
    // ---- 자동 채색 (일괄 채색) ----
    // 한 번의 페인트통 칠을 적어 둔 것. 이것을 다른 프레임에 똑같이 다시 칠한다.
    struct FillMark {
        QPoint at;
        QColor color;
    };
    const QVector<FillMark>& fillMarks() const { return fillMarks_; }
    void recordFill(const QPoint& at, const QColor& c);
    void clearFillMarks();
    // 한 장에 칠하기. 칠한 픽셀 수를 돌려준다.
    static int floodFill(QImage& img, const QPoint& seed, const QColor& color, int tolerance = 48);
    // layer 의 from~to 프레임에 marks 를 그대로 다시 칠한다. 칠한 프레임 수를 돌려준다.
    // 이미지가 있는 프레임(키)만 칠하고, 홀드 중인 칸은 건너뛴다.
    int batchFill(int layer, int from, int to, const QVector<FillMark>& marks, int tolerance = 48);

    // ---- 색 공간 (ICC) ----
    // 0 sRGB, 1 Display P3, 2 Adobe RGB, 3 ProPhoto RGB, 4 색 공간 없음
    int colorSpace() const { return colorSpace_; }
    void setColorSpace(int i);
    static QString colorSpaceName(int i);
    static int colorSpaceCount();
    // 내보낼 그림에 색 공간을 붙인다 (없음이면 그대로 둔다)
    void tagColorSpace(QImage& img) const;

    // 레이어의 따라가기 비율(%)만큼 약하게 만든 카메라 키
    CameraKey cameraAtFollow(int frame, int followPct) const;
    // 따라가기 비율이 100 이 아닌 보이는 레이어가 하나라도 있는가
    bool hasMultiplane() const;
    QPolygonF cameraFrame(int frame) const;   // 월드 좌표계에서 본 카메라 영역

    // ---- 렌더 카메라 ----
    const RenderCamera& renderCamera() const { return renderCam_; }
    void setRenderCamera(const RenderCamera& rc) { renderCam_ = rc; setModified(true); emit changed(); }
    QSize renderSize() const { return renderCam_.on ? renderCam_.size : size_; }

    // ---- 드로잉 레코더 / 획 애니메이션 ----
    bool recording() const { return recording_; }
    void setRecording(bool on) { recording_ = on; emit recorderChanged(); }
    const QVector<StrokeRec>& strokes() const { return strokes_; }
    void addStroke(const StrokeRec& s);
    void clearStrokes(int frame = -1);                  // -1 = 전부
    int strokeCount(int frame = -1) const;
    // 기록된 획을 fraction(0~1)만큼 그려 낸 그림. byTime 이면 그린 속도대로, 아니면 획의 길이에 비례해 진행한다.
    QImage renderStrokes(int frame, double fraction, bool byTime) const;
    // 기록된 획을 새 레이어의 프레임들로 풀어 쓴다 (획이 그려지는 과정이 애니메이션이 된다). 만든 레이어 번호를 돌려준다 (-1 = 실패).
    int strokesToFrames(int srcFrame, int startFrame, int nFrames, bool byTime);

    // ---- 벡터 레이어 ----
    void setVectorLayer(int layer, bool on);
    bool isVectorLayer(int layer) const { return layer >= 0 && layer < layers_.size() && layers_[layer].vector; }
    const QVector<VecStroke>* vectorStrokes(int layer, int frame) const;
    void setVectorStrokes(int layer, int frame, const QVector<VecStroke>& s);  // 넣고 그림을 다시 그린다
    void addVectorStroke(int layer, int frame, const VecStroke& s);
    QImage renderVectorFrame(int layer, int frame) const;      // 획들을 그려 낸 그림
    void rebuildVectorFrame(int layer, int frame);             // 그려서 frames[frame] 에 넣는다
    int vectorStrokeCount(int layer, int frame) const;
    // 벡터 획을 래스터(보통 그림)로 굳힌다. 벡터 데이터는 지운다.
    void flattenVectorLayer(int layer);
    // 두 벡터 프레임 사이를 t(0~1) 로 섞은 획 목록
    static QVector<VecStroke> interpolateStrokes(const QVector<VecStroke>& a, const QVector<VecStroke>& b, double t);

    // ---- 자동 중간 그림 (인비트위닝) ----
    // a, b 는 프레임 번호. 그 사이를 채운다. mode: 0 자동(벡터면 벡터), 1 모양 섞기(모프), 2 겹쳐 지나가기(크로스페이드)
    // ease: 0 직선, 1 느리게 시작, 2 느리게 끝, 3 느리게 시작·끝
    // 만든 중간 그림 개수를 돌려준다 (0 = 실패)
    int makeInbetweens(int layer, int a, int b, int mode, int ease);
    static double easeValue(double t, int ease);

    // ---- 퍼펫 레이어 ----
    void setPuppet(int layer, bool on);
    CameraKey puppetAt(int layer, int frame) const;          // 키가 없으면 단위 변환
    QTransform puppetTransform(int layer, int frame) const;  // 부모 연결까지 포함
    void setPuppetKey(int layer, int frame, const CameraKey& k);
    void removePuppetKey(int layer, int frame);
    void setPuppetPivot(int layer, qreal x, qreal y);
    void setPuppetParent(int layer, int parentLayer);        // -1 = 연결 해제
    int puppetParentLayer(int layer) const;                  // 부모 레이어 인덱스 (-1 = 없음)

    // ---- 북마크 / 이미지 마크 ----
    bool hasBookmark(int frame) const { return bookmarks_.contains(frame); }
    const QSet<int>& bookmarks() const { return bookmarks_; }
    void toggleBookmark(int frame);
    void clearBookmarks();
    bool bookmarksEnabled() const { return bookmarksOn_; }
    void setBookmarksEnabled(bool on) { bookmarksOn_ = on; notifyChanged(); }
    int nearestBookmark(int from, int dir) const; // dir = +1 / -1, 없으면 -1
    struct MarkColor { QString name; QColor color; };
    const QVector<MarkColor>& markColors() const { return markColors_; }
    void setMarkColors(const QVector<MarkColor>& m);
    void setImageMark(int layer, int frame, int colorIndex); // colorIndex < 0 이면 제거
    // ---- 컬러 그룹 ----
    static int colorGroupCount() { return 16; }
    static QColor colorGroupColor(int i);
    static QString colorGroupName(int i);
    // 현재 레이어만 보기 ('=')
    bool soloCurrent() const { return soloCurrent_; }
    void setSoloCurrent(bool on) { soloCurrent_ = on; notifyChanged(); }
    // 마지막 이미지 뒤(또는 첫 이미지 앞)의 프레임에 그리면 그 자리에 새 이미지를 만든다.
    // 끄면 예전처럼 유지되던 이미지를 그대로 고친다.
    bool newImagePastEnd() const { return newImagePastEnd_; }
    void setNewImagePastEnd(bool on) { newImagePastEnd_ = on; }
    // 스텐실: layer 에 그릴 때 허용되는 영역(0~255 알파 마스크). 스텐실이 없으면 null 이미지.
    QImage stencilMaskFor(int layer, int frame) const;

    // ---- 이미지 라이브러리 ----
    int libraryCount() const { return libNames_.size(); }
    QString libraryName(int i) const { return libNames_.value(i); }
    QImage libraryImage(int i) const { return libImages_.value(i); }
    int addToLibrary(const QString& name, const QImage& img);
    void removeFromLibrary(int i);
    QString libraryCategory(int i) const { return libCats_.value(i); }
    void setLibraryCategory(int i, const QString& cat);
    void setLibraryName(int i, const QString& name);
    QStringList libraryCategories() const;      // 쓰이고 있는 분류 목록 (가나다순, 중복 없음)

    // ---- 사운드 ----
    struct AudioTrack {
        QString path;
        QString name;
        double volume = 1.0;   // 0 ~ 1.5
        int offsetFrames = 0;  // 타임라인에서 트랙이 시작하는 프레임 (음수 가능)
        bool muted = false;
        bool locked = false;
    };
    const QVector<AudioTrack>& audioTracks() const { return audioTracks_; }
    int addAudioTrack(const QString& path, const QString& name = QString());
    void removeAudioTrack(int i);
    int duplicateAudioTrack(int i);
    void moveAudioTrack(int i, int delta);
    void updateAudioTrack(int i, const AudioTrack& t); // 값만 바꾼다 (재생 갱신은 audioChanged 로)
    double masterVolume() const { return masterVolume_; }
    bool masterMuted() const { return masterMuted_; }
    void setMaster(double volume, bool muted);
    // 이전 버전 호환: 첫 번째 트랙의 경로 / 트랙 하나로 교체
    QString audioPath() const { return audioTracks_.isEmpty() ? QString() : audioTracks_[0].path; }
    void setAudioPath(const QString& p);

    // ---- 실행 취소 ----
    void pushKeyEdit(int layer, int key, const QImage& before, const QImage& after, const QString& text);
    bool transformCurrent(const std::function<QImage(const QImage&)>& fn, const QString& text);
    QUndoStack* undoStack() const { return undo_; }

    // ---- 파일 ----
    bool save(const QString& path);
    bool load(const QString& path);
    bool saveCopy(const QString& path); // 현재 경로/수정 상태를 바꾸지 않고 사본만 저장
    // 프로젝트 + 소리 파일을 한 폴더로 모아 저장한다 (옮겨도 소리가 따라간다)
    bool packTo(const QString& onyPath, QString* report = nullptr);
    // 소리 파일이 제자리에 없으면 .ony 옆 / _files 폴더에서 찾아 경로를 고친다. 고친 개수를 돌려준다.
    int relinkMissingAudio();
    QString path() const { return path_; }
    void clearPath() { path_.clear(); } // 템플릿에서 만든 프로젝트는 이름 없음 상태로 시작
    // 이미지 메뉴용
    void reverseKeys(int layer, int a, int b);          // [a,b] 구간의 이미지 순서를 뒤집는다
    void setUniformExposure(int layer, int frames);
    // 프레임 속도를 바꾸면서 전체 길이(초)는 유지한다: 모든 키/카메라 키/북마크/메모 위치를 비율대로 옮긴다
    void stretchToFps(int newFps);
    // [a,b] 구간의 이미지를 times 번 더 반복해 뒤에 이어 붙인다 (pingpong 이면 짝수 번째는 거꾸로). 모자라면 프로젝트 길이를 늘린다.
    int repeatKeys(int layer, int a, int b, int times, bool pingpong);      // 모든 키 이미지의 노출을 frames 프레임으로 맞춘다
    void setPath(const QString& p) { path_ = p; }
    // 스토리보드 메모 (프레임 -> 텍스트)
    QString note(int frame) const { return notes_.value(frame); }
    void setNote(int frame, const QString& t);
    const QMap<int, QString>& notes() const { return notes_; }
    // 프록시: 재생 중 합성을 1/n 해상도로 그린다 (1 = 끔)
    void setProxyDivisor(int n) { proxyDiv_ = std::max(1, n); }
    int proxyDivisor() const { return proxyDiv_; }
    bool modified() const { return modified_; }
    void setModified(bool m);
    bool exportFrame(int frame, const QString& path) const;

    void notifyChanged();
    void notifyStructure();

signals:
    void recorderChanged();
    void changed();
    void structureChanged();
    void currentChanged();
    void modifiedChanged();
    void libraryChanged();
    void cameraChanged();
    void audioChanged();

private:
    void resetLayersTo(const QString& firstName);
    QVariantMap extraSave() const;
    void extraLoad(const QVariantMap& m);
    void drawLayers(QImage& target, const QTransform& xf, int frame, const QRect& clip = QRect(),
                    bool multiplane = false) const;

    QSize size_{1280, 720};
    int fps_ = 24;
    int frameCount_ = 48;
    QVector<Layer> layers_;
    QVector<Folder> folders_;
    int curLayer_ = 0;
    int curFrame_ = 0;
    bool cameraEnabled_ = false;
    QMap<int, CameraKey> cameraKeys_;
    QStringList libNames_;
    QStringList libCats_;
    QVector<QImage> libImages_;
    QVector<AudioTrack> audioTracks_;
    double masterVolume_ = 1.0;
    bool masterMuted_ = false;
    QMap<int, QString> notes_;
    int proxyDiv_ = 1;
    QSet<int> bookmarks_;
    bool bookmarksOn_ = true;
    QVector<MarkColor> markColors_{{QStringLiteral("빨강"), QColor(0xe5, 0x39, 0x35)}};
    bool soloCurrent_ = false;
    bool newImagePastEnd_ = true;
    int colorSpace_ = 0;
    QVector<FillMark> fillMarks_;
    QUndoStack* undo_;
    // 효과(FX) 결과 캐시: 같은 그림에 같은 효과면 다시 계산하지 않는다
    struct FxCacheEntry {
        qint64 srcKey = 0;
        QString sig;
        QImage out;
    };
    mutable QHash<quint64, FxCacheEntry> fxCache_;
    QImage fxApplied(int layer, int key, const QImage& src) const;
    int nextPuppetId_ = 1;
    bool recording_ = false;
    RenderCamera renderCam_;
    QVector<StrokeRec> strokes_;
    QString path_;
    bool modified_ = false;
};
