#include "Document.h"

#include <QDataStream>
#include <QFile>
#include <QPainter>
#include <QUndoCommand>
#include <QUndoStack>
#include <algorithm>
#include <cmath>

namespace {
constexpr quint32 kMagic = 0x4F4E5931;    // "ONY1"
constexpr quint32 kLegacyMagic = 0x54565058; // 이전 이름의 파일("TVPX")도 열 수 있다
constexpr quint32 kVersion = 2;

class KeyEditCmd : public QUndoCommand {
public:
    KeyEditCmd(Document* d, int l, int k, QImage b, QImage a, const QString& t)
        : QUndoCommand(t), d_(d), l_(l), k_(k), b_(std::move(b)), a_(std::move(a)) {}
    void undo() override { d_->setKeyImage(l_, k_, b_); }
    void redo() override { d_->setKeyImage(l_, k_, a_); }
private:
    Document* d_;
    int l_, k_;
    QImage b_, a_;
};
}

Document::Document(QObject* parent) : QObject(parent), undo_(new QUndoStack(this)) {
    undo_->setUndoLimit(60);
    newProject(QSize(1280, 720), 48, 24);
}

void Document::resetLayersTo(const QString& firstName) {
    layers_.clear();
    Layer l;
    l.name = firstName;
    l.frames.resize(frameCount_);
    layers_.push_back(l);
}

void Document::newProject(QSize size, int frameCount, int fps) {
    size_ = size;
    frameCount_ = std::max(1, frameCount);
    fps_ = std::max(1, fps);
    resetLayersTo(QStringLiteral("레이어 1"));
    folders_.clear();
    cameraEnabled_ = false;
    cameraKeys_.clear();
    libNames_.clear();
    libImages_.clear();
    audioPath_.clear();
    notes_.clear();
    curLayer_ = 0;
    curFrame_ = 0;
    path_.clear();
    undo_->clear();
    modified_ = false;
    emit structureChanged();
    emit currentChanged();
    emit changed();
    emit libraryChanged();
    emit cameraChanged();
    emit modifiedChanged();
}

void Document::setFps(int fps) {
    fps_ = std::max(1, fps);
    notifyStructure();
}

void Document::setCurrentLayer(int l) {
    l = std::clamp(l, 0, layerCount() - 1);
    if (l == curLayer_) return;
    curLayer_ = l;
    emit currentChanged();
}

void Document::setCurrentFrame(int f) {
    f = std::clamp(f, 0, frameCount_ - 1);
    if (f == curFrame_) return;
    curFrame_ = f;
    emit currentChanged();
}

// ---------------- 키 이미지 ----------------
int Document::keyIndexAt(int layer, int frame) const {
    if (layer < 0 || layer >= layers_.size()) return -1;
    const auto& fr = layers_[layer].frames;
    for (int i = std::min(frame, int(fr.size()) - 1); i >= 0; --i)
        if (!fr[i].isNull()) return i;
    return -1;
}

int Document::nextKeyIndex(int layer, int frame) const {
    if (layer < 0 || layer >= layers_.size()) return -1;
    const auto& fr = layers_[layer].frames;
    for (int i = frame + 1; i < fr.size(); ++i)
        if (!fr[i].isNull()) return i;
    return -1;
}

QImage Document::keyImage(int layer, int key) const {
    if (layer < 0 || layer >= layers_.size() || key < 0 || key >= frameCount_) return QImage();
    return layers_[layer].frames[key];
}

QImage* Document::editableImage(int layer, int frame) {
    if (layer < 0 || layer >= layers_.size()) return nullptr;
    int k = keyIndexAt(layer, frame);
    if (k < 0) {
        k = frame;
        QImage img(size_, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        layers_[layer].frames[k] = img;
    }
    return &layers_[layer].frames[k];
}

void Document::setKeyImage(int layer, int key, const QImage& img) {
    if (layer < 0 || layer >= layers_.size() || key < 0 || key >= frameCount_) return;
    layers_[layer].frames[key] = img;
    notifyChanged();
}

bool Document::extendExposure(int layer, int frame) {
    if (layer < 0 || layer >= layers_.size() || frame < 0 || frame >= frameCount_) return false;
    auto& fr = layers_[layer].frames;
    if (keyIndexAt(layer, frame) < 0) return false;
    fr.insert(frame + 1, QImage());
    fr.removeLast();
    undo_->clear();
    notifyStructure();
    return true;
}

bool Document::shrinkExposure(int layer, int frame) {
    if (layer < 0 || layer >= layers_.size() || frame < 0 || frame + 1 >= frameCount_) return false;
    auto& fr = layers_[layer].frames;
    if (!fr[frame + 1].isNull()) return false; // 다음 칸이 새 이미지면 줄일 수 없다
    fr.remove(frame + 1);
    fr.append(QImage());
    undo_->clear();
    notifyStructure();
    return true;
}

// ---------------- 합성 ----------------
void Document::drawLayers(QPainter& p, int frame) const {
    for (int l = 0; l < layers_.size(); ++l) {
        const Layer& L = layers_[l];
        bool vis = L.visible;
        qreal op = L.opacity;
        if (L.folder >= 0 && L.folder < folders_.size()) {
            vis = vis && folders_[L.folder].visible;
            op *= folders_[L.folder].opacity;
        }
        if (!vis) continue;
        const int k = keyIndexAt(l, frame);
        if (k < 0) continue;
        p.setOpacity(op);
        if (L.fx.isEmpty()) {
            p.drawImage(0, 0, L.frames[k]);
        } else {
            QImage img = L.frames[k];
            for (const auto& fx : L.fx) img = Effects::apply(img, fx);
            p.drawImage(0, 0, img);
        }
    }
    p.setOpacity(1.0);
}

QImage Document::composite(int frame, bool paper, bool useCamera) const {
    if (proxyDiv_ > 1) {
        const double sc = 1.0 / proxyDiv_;
        QImage small(QSize(std::max(1, int(size_.width() * sc)), std::max(1, int(size_.height() * sc))),
                     QImage::Format_ARGB32_Premultiplied);
        small.fill(paper ? QColor(Qt::white) : QColor(Qt::transparent));
        QPainter p(&small);
        QTransform total = QTransform::fromScale(sc, sc);
        if (useCamera && cameraEnabled_) total = cameraTransform(cameraAt(frame)) * total;
        p.setTransform(total);
        drawLayers(p, frame);
        p.end();
        return small.scaled(size_, Qt::IgnoreAspectRatio, Qt::FastTransformation)
            .convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }
    QImage out(size_, QImage::Format_ARGB32_Premultiplied);
    if (useCamera && cameraEnabled_) {
        QImage world(size_, QImage::Format_ARGB32_Premultiplied);
        world.fill(Qt::transparent);
        {
            QPainter wp(&world);
            drawLayers(wp, frame);
        }
        out.fill(paper ? QColor(Qt::white) : QColor(Qt::transparent));
        QPainter p(&out);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.setTransform(cameraTransform(cameraAt(frame)));
        // 월드 영역 밖은 종이색으로 보이도록 흰 배경을 먼저 깐다
        if (paper) p.fillRect(QRect(QPoint(0, 0), size_), Qt::white);
        p.drawImage(0, 0, world);
        return out;
    }
    out.fill(paper ? QColor(Qt::white) : QColor(Qt::transparent));
    QPainter p(&out);
    drawLayers(p, frame);
    return out;
}

// ---------------- 레이어 ----------------
int Document::addLayer(const QString& name) {
    Layer l;
    l.name = name;
    l.frames.resize(frameCount_);
    const int at = curLayer_ + 1;
    layers_.insert(at, l);
    curLayer_ = at;
    undo_->clear();
    notifyStructure();
    emit currentChanged();
    return at;
}

int Document::addCtgPair() {
    Layer color;
    color.name = QStringLiteral("CTG 색");
    color.frames.resize(frameCount_);
    Layer lines;
    lines.name = QStringLiteral("CTG 선화");
    lines.frames.resize(frameCount_);
    lines.ctgLines = true;
    const int at = curLayer_ + 1;
    layers_.insert(at, color);
    layers_.insert(at + 1, lines);
    curLayer_ = at + 1;
    undo_->clear();
    notifyStructure();
    emit currentChanged();
    return curLayer_;
}

void Document::removeLayer(int i) {
    if (layers_.size() <= 1 || i < 0 || i >= layers_.size()) return;
    layers_.remove(i);
    curLayer_ = std::clamp(curLayer_, 0, int(layers_.size()) - 1);
    undo_->clear();
    notifyStructure();
    emit currentChanged();
}

int Document::duplicateLayer(int i) {
    if (i < 0 || i >= layers_.size()) return -1;
    Layer l = layers_[i];
    l.name += QStringLiteral(" 복사");
    l.ctgLines = false;
    layers_.insert(i + 1, l);
    curLayer_ = i + 1;
    undo_->clear();
    notifyStructure();
    emit currentChanged();
    return curLayer_;
}

bool Document::mergeDown(int i) {
    if (i <= 0 || i >= layers_.size()) return false;
    Layer& up = layers_[i];
    Layer& dn = layers_[i - 1];
    QVector<QImage> out(frameCount_);
    for (int f = 0; f < frameCount_; ++f) {
        if (up.frames[f].isNull() && dn.frames[f].isNull()) continue;
        QImage img(size_, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        QPainter p(&img);
        auto draw = [&](const Layer& L) {
            const int k = keyIndexAt(&L - layers_.constData(), f);
            if (k < 0 || !L.visible) return;
            p.setOpacity(L.opacity);
            QImage src = L.frames[k];
            for (const auto& fx : L.fx) src = Effects::apply(src, fx);
            p.drawImage(0, 0, src);
        };
        draw(dn);
        draw(up);
        p.end();
        out[f] = img;
    }
    dn.frames = out;
    dn.opacity = 1.0;
    dn.visible = true;
    dn.fx.clear();
    layers_.remove(i);
    curLayer_ = i - 1;
    undo_->clear();
    notifyStructure();
    emit currentChanged();
    return true;
}

bool Document::mergeVisible() {
    int count = 0;
    for (const Layer& L : layers_) count += L.visible ? 1 : 0;
    if (count < 2) return false;
    // 아래부터 차례로 합친다
    for (int i = 1; i < layers_.size();) {
        if (layers_[i].visible && layers_[i - 1].visible) mergeDown(i);
        else ++i;
    }
    return true;
}

void Document::moveLayer(int i, int delta) {
    const int j = i + delta;
    if (i < 0 || j < 0 || i >= layers_.size() || j >= layers_.size()) return;
    layers_.swapItemsAt(i, j);
    curLayer_ = j;
    undo_->clear();
    notifyStructure();
    emit currentChanged();
}

// ---------------- 폴더 ----------------
int Document::addFolder(const QString& name) {
    Folder f;
    f.name = name;
    folders_.push_back(f);
    notifyStructure();
    return folders_.size() - 1;
}

void Document::removeFolder(int i) {
    if (i < 0 || i >= folders_.size()) return;
    for (Layer& l : layers_) {
        if (l.folder == i) l.folder = -1;
        else if (l.folder > i) --l.folder;
    }
    folders_.remove(i);
    notifyStructure();
}

void Document::setLayerFolder(int layer, int folder) {
    if (layer < 0 || layer >= layers_.size()) return;
    if (folder < -1 || folder >= folders_.size()) return;
    layers_[layer].folder = folder;
    notifyStructure();
}

// ---------------- 프레임 ----------------
void Document::insertFrame(int at) {
    at = std::clamp(at, 0, frameCount_);
    for (auto& l : layers_) l.frames.insert(at, QImage());
    ++frameCount_;
    undo_->clear();
    notifyStructure();
}

void Document::deleteFrame(int at) {
    if (frameCount_ <= 1 || at < 0 || at >= frameCount_) return;
    for (auto& l : layers_) {
        if (!l.frames[at].isNull() && at + 1 < frameCount_ && l.frames[at + 1].isNull())
            l.frames[at + 1] = l.frames[at];
        l.frames.remove(at);
    }
    --frameCount_;
    curFrame_ = std::min(curFrame_, frameCount_ - 1);
    undo_->clear();
    notifyStructure();
    emit currentChanged();
}

void Document::setFrameCount(int n) {
    n = std::max(1, n);
    if (n == frameCount_) return;
    for (auto& l : layers_) l.frames.resize(n);
    frameCount_ = n;
    curFrame_ = std::min(curFrame_, frameCount_ - 1);
    undo_->clear();
    notifyStructure();
    emit currentChanged();
}

// ---------------- FX ----------------
void Document::addFx(int layer, const Effects::Item& item) {
    if (layer < 0 || layer >= layers_.size()) return;
    layers_[layer].fx.push_back(item);
    notifyStructure();
}

void Document::removeFx(int layer, int index) {
    if (layer < 0 || layer >= layers_.size()) return;
    auto& fx = layers_[layer].fx;
    if (index < 0 || index >= fx.size()) return;
    fx.remove(index);
    notifyStructure();
}

// ---------------- 카메라 ----------------
void Document::setCameraEnabled(bool on) {
    cameraEnabled_ = on;
    setModified(true);
    emit cameraChanged();
    emit changed();
}

CameraKey Document::cameraAt(int frame) const {
    if (cameraKeys_.isEmpty()) return CameraKey();
    auto hi = cameraKeys_.lowerBound(frame); // frame 이상인 첫 키
    if (hi == cameraKeys_.begin()) return hi.value();
    if (hi == cameraKeys_.end()) return std::prev(hi).value();
    if (hi.key() == frame) return hi.value();
    auto lo = std::prev(hi);
    const double t = double(frame - lo.key()) / double(hi.key() - lo.key());
    CameraKey a = lo.value(), b = hi.value(), r;
    r.x = a.x + (b.x - a.x) * t;
    r.y = a.y + (b.y - a.y) * t;
    r.scale = a.scale + (b.scale - a.scale) * t;
    r.rotation = a.rotation + (b.rotation - a.rotation) * t;
    return r;
}

void Document::setCameraKey(int frame, const CameraKey& k) {
    cameraKeys_[frame] = k;
    setModified(true);
    emit cameraChanged();
    emit changed();
}

void Document::removeCameraKey(int frame) {
    if (!cameraKeys_.remove(frame)) return;
    setModified(true);
    emit cameraChanged();
    emit changed();
}

QTransform Document::cameraTransform(const CameraKey& k) const {
    const double s = std::max(0.01, double(k.scale));
    QTransform t;
    t.translate(size_.width() / 2.0, size_.height() / 2.0);
    t.rotate(-k.rotation);
    t.scale(s, s);
    t.translate(-(size_.width() / 2.0 + k.x), -(size_.height() / 2.0 + k.y));
    return t;
}

QPolygonF Document::cameraFrame(int frame) const {
    bool ok = false;
    const QTransform inv = cameraTransform(cameraAt(frame)).inverted(&ok);
    if (!ok) return QPolygonF(QRectF(QPointF(0, 0), QSizeF(size_)));
    return inv.map(QPolygonF(QRectF(QPointF(0, 0), QSizeF(size_))));
}

// ---------------- 라이브러리 ----------------
int Document::addToLibrary(const QString& name, const QImage& img) {
    libNames_.push_back(name);
    libImages_.push_back(img);
    setModified(true);
    emit libraryChanged();
    return libNames_.size() - 1;
}

void Document::removeFromLibrary(int i) {
    if (i < 0 || i >= libNames_.size()) return;
    libNames_.removeAt(i);
    libImages_.remove(i);
    setModified(true);
    emit libraryChanged();
}

void Document::setAudioPath(const QString& p) {
    audioPath_ = p;
    setModified(true);
}

// ---------------- 실행 취소 ----------------
void Document::pushKeyEdit(int layer, int key, const QImage& before, const QImage& after, const QString& text) {
    undo_->push(new KeyEditCmd(this, layer, key, before, after, text));
}

bool Document::transformCurrent(const std::function<QImage(const QImage&)>& fn, const QString& text) {
    const int l = curLayer_;
    const int k = keyIndexAt(l, curFrame_);
    if (k < 0 || layers_[l].locked) return false;
    const QImage before = layers_[l].frames[k];
    QImage after = fn(before);
    if (after.isNull()) return false;
    after = after.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    pushKeyEdit(l, k, before, after, text);
    return true;
}

// ---------------- 파일 ----------------
bool Document::save(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    QDataStream ds(&f);
    ds.setVersion(QDataStream::Qt_6_0);
    ds << kMagic << kVersion << size_ << qint32(fps_) << qint32(frameCount_) << qint32(layers_.size());
    for (const Layer& l : layers_) {
        ds << l.name << l.visible << l.locked << double(l.opacity);
        for (int i = 0; i < frameCount_; ++i) ds << l.frames.value(i);
        ds << qint32(l.folder) << l.ctgLines << qint32(l.fx.size());
        for (const auto& fx : l.fx) ds << qint32(fx.type) << qint32(fx.param);
    }
    ds << qint32(folders_.size());
    for (const Folder& fo : folders_) ds << fo.name << fo.visible << double(fo.opacity);
    ds << cameraEnabled_ << qint32(cameraKeys_.size());
    for (auto it = cameraKeys_.cbegin(); it != cameraKeys_.cend(); ++it)
        ds << qint32(it.key()) << double(it.value().x) << double(it.value().y) << double(it.value().scale)
           << double(it.value().rotation);
    ds << qint32(libNames_.size());
    for (int i = 0; i < libNames_.size(); ++i) ds << libNames_[i] << libImages_[i];
    ds << audioPath_;
    ds << qint32(notes_.size());
    for (auto it = notes_.cbegin(); it != notes_.cend(); ++it) ds << qint32(it.key()) << it.value();
    if (ds.status() != QDataStream::Ok) return false;
    path_ = path;
    setModified(false);
    return true;
}

bool Document::load(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QDataStream ds(&f);
    ds.setVersion(QDataStream::Qt_6_0);
    quint32 magic = 0, ver = 0;
    QSize size;
    qint32 fps = 0, frames = 0, nLayers = 0;
    ds >> magic >> ver >> size >> fps >> frames >> nLayers;
    if (ds.status() != QDataStream::Ok || (magic != kMagic && magic != kLegacyMagic) || ver < 1 || ver > kVersion) return false;
    if (size.width() < 1 || size.height() < 1 || size.width() > 16384 || size.height() > 16384) return false;
    if (fps < 1 || fps > 240 || frames < 1 || frames > 100000 || nLayers < 1 || nLayers > 1024) return false;

    QVector<Layer> loaded;
    for (int i = 0; i < nLayers; ++i) {
        Layer l;
        double op = 1.0;
        ds >> l.name >> l.visible >> l.locked >> op;
        l.opacity = std::clamp(op, 0.0, 1.0);
        l.frames.resize(frames);
        for (int j = 0; j < frames; ++j) {
            QImage img;
            ds >> img;
            if (!img.isNull()) {
                if (img.size() != size) return false;
                img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            }
            l.frames[j] = img;
        }
        if (ver >= 2) {
            qint32 folder = -1, nfx = 0;
            ds >> folder >> l.ctgLines >> nfx;
            if (nfx < 0 || nfx > 64) return false;
            l.folder = folder;
            for (int k = 0; k < nfx; ++k) {
                qint32 t = 0, p = 0;
                ds >> t >> p;
                if (t < 0 || t > int(Effects::Type::Blur)) return false;
                l.fx.push_back({Effects::Type(t), p});
            }
        }
        if (ds.status() != QDataStream::Ok) return false;
        loaded.push_back(std::move(l));
    }

    QVector<Folder> loadedFolders;
    bool camOn = false;
    QMap<int, CameraKey> camKeys;
    QStringList libNames;
    QVector<QImage> libImages;
    QString audio;
    QMap<int, QString> loadedNotes;
    if (ver >= 2) {
        qint32 nf = 0;
        ds >> nf;
        if (nf < 0 || nf > 1024) return false;
        for (int i = 0; i < nf; ++i) {
            Folder fo;
            double op = 1.0;
            ds >> fo.name >> fo.visible >> op;
            fo.opacity = std::clamp(op, 0.0, 1.0);
            loadedFolders.push_back(fo);
        }
        qint32 nk = 0;
        ds >> camOn >> nk;
        if (nk < 0 || nk > 100000) return false;
        for (int i = 0; i < nk; ++i) {
            qint32 key = 0;
            double x = 0, y = 0, s = 1, r = 0;
            ds >> key >> x >> y >> s >> r;
            CameraKey ck;
            ck.x = x; ck.y = y; ck.scale = s; ck.rotation = r;
            camKeys[key] = ck;
        }
        qint32 nl = 0;
        ds >> nl;
        if (nl < 0 || nl > 10000) return false;
        for (int i = 0; i < nl; ++i) {
            QString n;
            QImage img;
            ds >> n >> img;
            libNames.push_back(n);
            libImages.push_back(img.convertToFormat(QImage::Format_ARGB32_Premultiplied));
        }
        ds >> audio;
        if (ds.status() != QDataStream::Ok) return false;
        if (!ds.atEnd()) {
            qint32 nn = 0;
            ds >> nn;
            if (nn < 0 || nn > 100000) return false;
            for (int i = 0; i < nn; ++i) {
                qint32 fr = 0;
                QString t;
                ds >> fr >> t;
                if (fr >= 0 && fr < frames) loadedNotes[fr] = t;
            }
            if (ds.status() != QDataStream::Ok) return false;
        }
        for (const Layer& l : loaded)
            if (l.folder < -1 || l.folder >= loadedFolders.size()) return false;
    }

    size_ = size;
    fps_ = fps;
    frameCount_ = frames;
    layers_ = std::move(loaded);
    folders_ = std::move(loadedFolders);
    cameraEnabled_ = camOn;
    cameraKeys_ = std::move(camKeys);
    libNames_ = std::move(libNames);
    libImages_ = std::move(libImages);
    audioPath_ = audio;
    notes_ = std::move(loadedNotes);
    curLayer_ = 0;
    curFrame_ = 0;
    path_ = path;
    undo_->clear();
    modified_ = false;
    emit structureChanged();
    emit currentChanged();
    emit changed();
    emit libraryChanged();
    emit cameraChanged();
    emit modifiedChanged();
    return true;
}

void Document::setModified(bool m) {
    if (modified_ == m) return;
    modified_ = m;
    emit modifiedChanged();
}

bool Document::exportFrame(int frame, const QString& path) const {
    Document* self = const_cast<Document*>(this);
    const int keep = proxyDiv_;
    self->proxyDiv_ = 1;
    const bool ok = composite(frame, true, true).save(path, "PNG");
    self->proxyDiv_ = keep;
    return ok;
}

void Document::setNote(int frame, const QString& t) {
    if (t.isEmpty()) notes_.remove(frame);
    else notes_[frame] = t;
    setModified(true);
    emit libraryChanged();
}

void Document::notifyChanged() {
    setModified(true);
    emit changed();
}

void Document::notifyStructure() {
    setModified(true);
    emit structureChanged();
    emit changed();
}
