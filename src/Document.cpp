#include "Document.h"
#include "Blend.h"

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
constexpr quint32 kExtMagic = 0x4F4E5845; // "ONXE"

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
    bookmarks_.clear();
    bookmarksOn_ = true;
    markColors_ = {{QStringLiteral("빨강"), QColor(0xe5, 0x39, 0x35)}};
    soloCurrent_ = false;
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

// ---------------- 키 해석 (프리/포스트 비헤이비어) ----------------
int Document::firstKey(int layer) const {
    if (layer < 0 || layer >= layers_.size()) return -1;
    const auto& fr = layers_[layer].frames;
    for (int i = 0; i < frameCount_; ++i)
        if (!fr[i].isNull()) return i;
    return -1;
}

int Document::lastKey(int layer) const {
    if (layer < 0 || layer >= layers_.size()) return -1;
    const auto& fr = layers_[layer].frames;
    for (int i = frameCount_ - 1; i >= 0; --i)
        if (!fr[i].isNull()) return i;
    return -1;
}

int Document::resolveKey(int l, int f) const {
    if (l < 0 || l >= layers_.size() || f < 0 || f >= frameCount_) return -1;
    const Layer& L = layers_[l];
    const int first = firstKey(l);
    if (first < 0) return -1;
    const int end = (L.endFrame >= 0) ? std::clamp(L.endFrame, first + 1, frameCount_) : frameCount_;
    if (f >= first && f < end) return keyIndexAt(l, f);
    const int len = end - first;
    const int mode = f < first ? L.preBehavior : L.postBehavior;
    switch (mode) {
    case 1: { // 반복
        const int pos = ((f - first) % len + len) % len;
        return keyIndexAt(l, first + pos);
    }
    case 2: { // 핑퐁
        if (len <= 1) return keyIndexAt(l, first);
        const int period = 2 * len - 2;
        const int t = ((f - first) % period + period) % period;
        return keyIndexAt(l, first + (t < len ? t : period - t));
    }
    case 3: // 정지: 가까운 끝의 이미지를 유지
        return keyIndexAt(l, f < first ? first : end - 1);
    default: return -1; // 없음
    }
}

// ---------------- 북마크 / 이미지 마크 / 컬러 그룹 ----------------
void Document::toggleBookmark(int frame) {
    if (bookmarks_.contains(frame)) bookmarks_.remove(frame);
    else bookmarks_.insert(frame);
    notifyChanged();
}

void Document::clearBookmarks() {
    bookmarks_.clear();
    notifyChanged();
}

int Document::nearestBookmark(int from, int dir) const {
    int best = -1;
    for (int b : bookmarks_) {
        if (dir > 0 && b > from && (best < 0 || b < best)) best = b;
        if (dir < 0 && b < from && (best < 0 || b > best)) best = b;
    }
    return best;
}

void Document::setMarkColors(const QVector<MarkColor>& m) {
    markColors_ = m;
    notifyChanged();
}

void Document::setImageMark(int layer, int frame, int colorIndex) {
    if (layer < 0 || layer >= layers_.size()) return;
    const int k = keyIndexAt(layer, frame);
    if (k < 0) return;
    if (colorIndex < 0) layers_[layer].imageMarks.remove(k);
    else layers_[layer].imageMarks[k] = colorIndex;
    notifyChanged();
}

QColor Document::colorGroupColor(int i) {
    static const QColor c[] = {QColor("#e53935"), QColor("#fb8c00"), QColor("#fdd835"), QColor("#7cb342"),
                               QColor("#43a047"), QColor("#00acc1"), QColor("#1e88e5"), QColor("#3949ab"),
                               QColor("#8e24aa"), QColor("#d81b60"), QColor("#6d4c41"), QColor("#8d6e63"),
                               QColor("#78909c"), QColor("#bdbdbd"), QColor("#ffffff"), QColor("#212121")};
    return c[std::clamp(i, 0, 15)];
}

QString Document::colorGroupName(int i) {
    static const char* n[] = {"빨강", "주황", "노랑", "연두", "초록", "청록", "파랑", "남색", "보라", "분홍", "갈색", "연갈색", "회청", "밝은 회색", "흰색", "검정"};
    return QString::fromUtf8(n[std::clamp(i, 0, 15)]);
}

QImage Document::stencilMaskFor(int layer, int frame) const {
    QImage mask;
    for (int l = 0; l < layers_.size(); ++l) {
        const Layer& S = layers_[l];
        if (l == layer || !S.stencil || !S.visible) continue;
        const int k = resolveKey(l, frame);
        if (k < 0) continue;
        if (mask.isNull()) {
            mask = QImage(size_, QImage::Format_ARGB32_Premultiplied);
            mask.fill(Qt::transparent);
        }
        // 여러 스텐실은 합쳐진다 (불투명한 곳의 합집합). 반전 스텐실은 투명한 곳이 허용 영역.
        QImage m(size_, QImage::Format_ARGB32_Premultiplied);
        m.fill(Qt::transparent);
        {
            QPainter p(&m);
            if (S.stencilInvert) {
                p.fillRect(m.rect(), Qt::black);
                p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
            }
            p.drawImage(0, 0, S.frames[k]);
        }
        QPainter mp(&mask);
        mp.setCompositionMode(QPainter::CompositionMode_Lighten);
        mp.drawImage(0, 0, m);
    }
    return mask;
}

// ---------------- 합성 ----------------
void Document::drawLayers(QImage& target, const QTransform& xf, int frame) const {
    for (int l = 0; l < layers_.size(); ++l) {
        const Layer& L = layers_[l];
        bool vis = L.visible;
        qreal op = L.opacity;
        if (L.folder >= 0 && L.folder < folders_.size()) {
            vis = vis && folders_[L.folder].visible;
            op *= folders_[L.folder].opacity;
        }
        if (soloCurrent_) vis = (l == curLayer_);
        if (!vis) continue;
        const int k = resolveKey(l, frame);
        if (k < 0) continue;
        QImage img = L.frames[k];
        for (const auto& fx : L.fx) img = Effects::apply(img, fx);
        if (L.blend == 0) {
            QPainter p(&target);
            p.setTransform(xf);
            p.setOpacity(op);
            p.drawImage(0, 0, img);
        } else {
            // 블렌딩 모드: 같은 변환으로 레이어를 따로 그린 뒤 픽셀 단위로 섞는다
            QImage layerImg(target.size(), QImage::Format_ARGB32_Premultiplied);
            layerImg.fill(Qt::transparent);
            {
                QPainter p(&layerImg);
                p.setTransform(xf);
                p.drawImage(0, 0, img);
            }
            Blend::apply(target, layerImg, L.blend, op);
        }
    }
}

QImage Document::composite(int frame, bool paper, bool useCamera) const {
    if (proxyDiv_ > 1) {
        const double sc = 1.0 / proxyDiv_;
        QImage small(QSize(std::max(1, int(size_.width() * sc)), std::max(1, int(size_.height() * sc))),
                     QImage::Format_ARGB32_Premultiplied);
        small.fill(paper ? QColor(Qt::white) : QColor(Qt::transparent));
        QTransform total = QTransform::fromScale(sc, sc);
        if (useCamera && cameraEnabled_) total = cameraTransform(cameraAt(frame)) * total;
        drawLayers(small, total, frame);
        return small.scaled(size_, Qt::IgnoreAspectRatio, Qt::FastTransformation)
            .convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }
    QImage out(size_, QImage::Format_ARGB32_Premultiplied);
    if (useCamera && cameraEnabled_) {
        QImage world(size_, QImage::Format_ARGB32_Premultiplied);
        world.fill(Qt::transparent);
        drawLayers(world, QTransform(), frame);
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
    drawLayers(out, QTransform(), frame);
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

void Document::resizeCanvas(QSize ns, int mode) {
    ns = QSize(std::clamp(ns.width(), 16, 8192), std::clamp(ns.height(), 16, 8192));
    if (ns == size_) return;
    const QSize old = size_;
    for (auto& l : layers_)
        for (auto& img : l.frames) {
            if (img.isNull()) continue;
            QImage out(ns, QImage::Format_ARGB32_Premultiplied);
            out.fill(Qt::transparent);
            QPainter p(&out);
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            if (mode == 0) {
                p.drawImage(QRect(QPoint(0, 0), ns), img);
            } else {
                const QPoint off = mode == 1 ? QPoint((ns.width() - old.width()) / 2, (ns.height() - old.height()) / 2) : QPoint(0, 0);
                p.drawImage(off, img);
            }
            p.end();
            img = out;
        }
    size_ = ns;
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
namespace {
QVariantMap layerExtSave(const Layer& l) {
    QVariantMap m;
    m["pre"] = l.preBehavior;
    m["post"] = l.postBehavior;
    m["end"] = l.endFrame;
    m["blend"] = l.blend;
    m["collapsed"] = l.collapsed;
    m["cg"] = l.colorGroup;
    m["stencil"] = l.stencil;
    m["stencilInv"] = l.stencilInvert;
    m["pa"] = l.preserveAlpha;
    QVariantMap names, marks;
    for (auto it = l.instanceNames.cbegin(); it != l.instanceNames.cend(); ++it) names[QString::number(it.key())] = it.value();
    for (auto it = l.imageMarks.cbegin(); it != l.imageMarks.cend(); ++it) marks[QString::number(it.key())] = it.value();
    m["names"] = names;
    m["marks"] = marks;
    return m;
}

void layerExtLoad(Layer& l, const QVariantMap& m) {
    l.preBehavior = std::clamp(m.value("pre", 0).toInt(), 0, 3);
    l.postBehavior = std::clamp(m.value("post", 3).toInt(), 0, 3);
    l.endFrame = m.value("end", -1).toInt();
    l.blend = std::clamp(m.value("blend", 0).toInt(), 0, Blend::Count - 1);
    l.collapsed = m.value("collapsed", false).toBool();
    l.colorGroup = m.value("cg", -1).toInt();
    l.stencil = m.value("stencil", false).toBool();
    l.stencilInvert = m.value("stencilInv", false).toBool();
    l.preserveAlpha = m.value("pa", false).toBool();
    const QVariantMap names = m.value("names").toMap(), marks = m.value("marks").toMap();
    for (auto it = names.cbegin(); it != names.cend(); ++it) l.instanceNames[it.key().toInt()] = it.value().toString();
    for (auto it = marks.cbegin(); it != marks.cend(); ++it) l.imageMarks[it.key().toInt()] = it.value().toInt();
}
} // namespace

QVariantMap Document::extraSave() const {
    QVariantMap m;
    QVariantList bm;
    for (int b : bookmarks_) bm << b;
    m["bookmarks"] = bm;
    m["bookmarksOn"] = bookmarksOn_;
    QVariantList mc;
    for (const MarkColor& c : markColors_) mc << QVariantList{c.name, c.color.name()};
    m["markColors"] = mc;
    return m;
}

void Document::extraLoad(const QVariantMap& m) {
    bookmarks_.clear();
    for (const QVariant& v : m.value("bookmarks").toList()) bookmarks_.insert(v.toInt());
    bookmarksOn_ = m.value("bookmarksOn", true).toBool();
    QVector<MarkColor> mc;
    for (const QVariant& v : m.value("markColors").toList()) {
        const QVariantList e = v.toList();
        if (e.size() == 2) mc.push_back({e[0].toString(), QColor(e[1].toString())});
    }
    markColors_ = mc.isEmpty() ? QVector<MarkColor>{{QStringLiteral("빨강"), QColor(0xe5, 0x39, 0x35)}} : mc;
}

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
    // 확장 블록: 새 기능은 키-값으로 덧붙여서 포맷 버전을 올리지 않아도 된다
    {
        QVariantMap docExt = extraSave();
        QVariantList layersExt;
        for (const Layer& l : layers_) layersExt << layerExtSave(l);
        ds << kExtMagic << docExt << layersExt;
    }
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
    QVariantMap pendingExt;
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
        if (!ds.atEnd()) {
            quint32 em = 0;
            ds >> em;
            if (em == kExtMagic) {
                QVariantMap docExt;
                QVariantList layersExt;
                ds >> docExt >> layersExt;
                if (ds.status() == QDataStream::Ok) {
                    for (int i = 0; i < loaded.size() && i < layersExt.size(); ++i) layerExtLoad(loaded[i], layersExt[i].toMap());
                    pendingExt = docExt;
                }
            }
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
    extraLoad(pendingExt);
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
