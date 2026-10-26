#pragma once
#include <QMap>
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

#include "Effects.h"

class QUndoStack;

struct Layer {
    QString name;
    bool visible = true;
    bool locked = false;
    qreal opacity = 1.0;
    // 프레임별 키 이미지. null 이면 직전 키 이미지가 그대로 노출(홀드)된다.
    QVector<QImage> frames;
    int folder = -1;          // 소속 폴더 인덱스 (-1: 없음)
    bool ctgLines = false;    // CTG 선화 레이어: 바로 아래 레이어가 색 레이어
    QVector<Effects::Item> fx;
};

struct Folder {
    QString name;
    bool visible = true;
    qreal opacity = 1.0;
};

struct CameraKey {
    qreal x = 0, y = 0;       // 카메라 중심의 이동량(픽셀)
    qreal scale = 1.0;        // 1.0 = 100%
    qreal rotation = 0;       // 도
};

class Document : public QObject {
    Q_OBJECT
public:
    explicit Document(QObject* parent = nullptr);

    void newProject(QSize size, int frameCount, int fps);

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

    // ---- 키 이미지 ----
    int keyIndexAt(int layer, int frame) const;
    int nextKeyIndex(int layer, int frame) const;
    QImage keyImage(int layer, int key) const;
    QImage* editableImage(int layer, int frame);
    void setKeyImage(int layer, int key, const QImage& img);

    // 노출(홀드) 조절: 현재 프레임 뒤에 빈 칸을 넣거나 뺀다 (해당 레이어만)
    bool extendExposure(int layer, int frame);
    bool shrinkExposure(int layer, int frame);

    // ---- 합성 ----
    QImage composite(int frame, bool paper = true, bool useCamera = false) const;

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
    QPolygonF cameraFrame(int frame) const;   // 월드 좌표계에서 본 카메라 영역

    // ---- 이미지 라이브러리 ----
    int libraryCount() const { return libNames_.size(); }
    QString libraryName(int i) const { return libNames_.value(i); }
    QImage libraryImage(int i) const { return libImages_.value(i); }
    int addToLibrary(const QString& name, const QImage& img);
    void removeFromLibrary(int i);

    // ---- 사운드 ----
    QString audioPath() const { return audioPath_; }
    void setAudioPath(const QString& p);

    // ---- 실행 취소 ----
    void pushKeyEdit(int layer, int key, const QImage& before, const QImage& after, const QString& text);
    bool transformCurrent(const std::function<QImage(const QImage&)>& fn, const QString& text);
    QUndoStack* undoStack() const { return undo_; }

    // ---- 파일 ----
    bool save(const QString& path);
    bool load(const QString& path);
    QString path() const { return path_; }
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
    void changed();
    void structureChanged();
    void currentChanged();
    void modifiedChanged();
    void libraryChanged();
    void cameraChanged();

private:
    void resetLayersTo(const QString& firstName);
    void drawLayers(class QPainter& p, int frame) const;

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
    QVector<QImage> libImages_;
    QString audioPath_;
    QMap<int, QString> notes_;
    int proxyDiv_ = 1;
    QUndoStack* undo_;
    QString path_;
    bool modified_ = false;
};
