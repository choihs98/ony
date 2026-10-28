#include <functional>
#include "Document.h"
#include <QColorSpace>
#include <QDir>
#include <QFileInfo>
#include "Blend.h"

#include <QDataStream>
#include <QFile>
#include <QPainter>
#include <QPainterPath>
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
    libCats_.clear();
    renderCam_ = RenderCamera();
    strokes_.clear();
    recording_ = false;
    libNames_.clear();
    libImages_.clear();
    audioTracks_.clear();
    masterVolume_ = 1.0;
    masterMuted_ = false;
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
    // 레이어 내용 범위 밖(마지막 이미지 뒤)에 그리면, 유지되던 이미지를 고치지 않고 그 자리에 새 이미지를 만든다.
    if (k >= 0 && newImagePastEnd_ && frame >= contentEnd(layer)) k = -1;
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

int Document::contentEnd(int l) const {
    if (l < 0 || l >= layers_.size()) return 0;
    const int first = firstKey(l);
    if (first < 0) return 0;
    const Layer& L = layers_[l];
    // 레이어 끝을 직접 정했으면 그걸 쓰고, 아니면 마지막 이미지 바로 뒤가 내용의 끝이다.
    if (L.endFrame >= 0) return std::clamp(L.endFrame, first + 1, frameCount_);
    return std::clamp(lastKey(l) + 1, first + 1, frameCount_);
}

int Document::resolveKey(int l, int f) const {
    if (l < 0 || l >= layers_.size() || f < 0 || f >= frameCount_) return -1;
    const Layer& L = layers_[l];
    const int first = firstKey(l);
    if (first < 0) return -1;
    const int end = contentEnd(l);
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
// 레이어의 FX 를 적용한 그림. 같은 그림·같은 효과면 캐시에서 꺼낸다.
QImage Document::fxApplied(int layer, int key, const QImage& src) const {
    const Layer& L = layers_[layer];
    if (L.fx.isEmpty()) return src;
    QString sig;
    for (const auto& f : L.fx) sig += QString::number(int(f.type)) + ':' + QString::number(f.param) + ';';
    const quint64 ck = (quint64(layer) << 32) | quint32(key);
    auto it = fxCache_.find(ck);
    if (it != fxCache_.end() && it->srcKey == src.cacheKey() && it->sig == sig && it->out.size() == src.size())
        return it->out;
    QImage out = src;
    for (const auto& f : L.fx) out = Effects::apply(out, f);
    FxCacheEntry e;
    e.srcKey = src.cacheKey();
    e.sig = sig;
    e.out = out;
    if (fxCache_.size() > 96) fxCache_.clear();   // 너무 커지면 비운다
    fxCache_.insert(ck, e);
    return out;
}

void Document::drawLayers(QImage& target, const QTransform& xf, int frame, const QRect& clip, bool multiplane) const {
    for (int l = 0; l < layers_.size(); ++l) {
        const Layer& L = layers_[l];
        bool vis = L.visible;
        qreal op = L.opacity;
        if (L.folder >= 0 && L.folder < folders_.size()) {
            vis = vis && folderVisibleEffective(L.folder);
            op *= folderOpacityEffective(L.folder);
        }
        if (soloCurrent_) vis = (l == curLayer_);
        if (!vis) continue;
        const int k = resolveKey(l, frame);
        if (k < 0) continue;
        const QImage img = fxApplied(l, k, L.frames[k]);
        // 멀티플레인: 레이어마다 따라가기 비율에 맞춘 카메라를 따로 건다
        const QTransform base = multiplane ? cameraTransform(cameraAtFollow(frame, L.camFollow)) * xf : xf;
        const QTransform lxf = L.puppet ? puppetTransform(l, frame) * base : base;
        if (L.blend == 0) {
            QPainter p(&target);
            if (!clip.isNull()) p.setClipRect(clip);
            p.setTransform(lxf);
            p.setOpacity(op);
            p.drawImage(0, 0, img);
        } else if (lxf.isIdentity() && img.size() == target.size()) {
            // 변환이 없으면 중간 그림을 만들 필요가 없다 (가장 흔한 경우)
            Blend::apply(target, img, L.blend, op);
        } else {
            // 블렌딩 모드: 같은 변환으로 레이어를 따로 그린 뒤 픽셀 단위로 섞는다
            QImage layerImg(target.size(), QImage::Format_ARGB32_Premultiplied);
            layerImg.fill(Qt::transparent);
            {
                QPainter p(&layerImg);
                p.setTransform(lxf);
                p.drawImage(0, 0, img);
            }
            Blend::apply(target, layerImg, L.blend, op);
        }
    }
}

// 일부만 다시 그리기. 블렌딩·카메라·프록시가 없을 때만 된다.
bool Document::compositeRegion(QImage& target, const QRect& region, int frame, bool paper) const {
    if (proxyDiv_ > 1 || target.size() != size_) return false;
    for (const Layer& L : layers_)
        if (L.visible && L.blend != 0) return false;
    const QRect r = region.intersected(QRect(QPoint(0, 0), size_));
    if (r.isEmpty()) return true;
    {
        QPainter p(&target);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.fillRect(r, paper ? QColor(Qt::white) : QColor(Qt::transparent));
    }
    drawLayers(target, QTransform(), frame, r);
    return true;
}

QImage Document::composite(int frame, bool paper, bool useCamera) const {
    if (proxyDiv_ > 1) {
        const double sc = 1.0 / proxyDiv_;
        QImage small(QSize(std::max(1, int(size_.width() * sc)), std::max(1, int(size_.height() * sc))),
                     QImage::Format_ARGB32_Premultiplied);
        small.fill(paper ? QColor(Qt::white) : QColor(Qt::transparent));
        QTransform total = QTransform::fromScale(sc, sc);
        const bool mp = useCamera && cameraEnabled_ && hasMultiplane();
        if (useCamera && cameraEnabled_ && !mp) total = cameraTransform(cameraAt(frame)) * total;
        drawLayers(small, total, frame, QRect(), mp);
        return small.scaled(size_, Qt::IgnoreAspectRatio, Qt::FastTransformation)
            .convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }
    QImage out(size_, QImage::Format_ARGB32_Premultiplied);
    if (useCamera && cameraEnabled_ && hasMultiplane()) {
        // 멀티플레인: 레이어마다 카메라가 다르므로 한 장으로 납작하게 만들 수 없다
        out.fill(paper ? QColor(Qt::white) : QColor(Qt::transparent));
        drawLayers(out, QTransform(), frame, QRect(), true);
        return out;
    }
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
    if (l.puppet) l.puppetId = nextPuppetId_++;
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
            const QImage src = fxApplied(&L - layers_.constData(), k, L.frames[k]);
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

static CameraKey interpKeys(const QMap<int, CameraKey>& cameraKeys_, int frame) {
    if (cameraKeys_.isEmpty()) return CameraKey();
    auto hi = cameraKeys_.lowerBound(frame); // frame 이상인 첫 키
    if (hi == cameraKeys_.begin()) return hi.value();
    if (hi == cameraKeys_.end()) return std::prev(hi).value();
    if (hi.key() == frame) return hi.value();
    auto lo = std::prev(hi);
    double t = double(frame - lo.key()) / double(hi.key() - lo.key());
    switch (lo.value().ease) {
    case 1: t = t * t * t; break;                                   // 느리게 시작
    case 2: t = 1.0 - (1.0 - t) * (1.0 - t) * (1.0 - t); break;     // 느리게 끝
    case 3: t = t * t * (3.0 - 2.0 * t); break;                     // 느리게 시작·끝
    case 4: t = 0.0; break;                                         // 계단
    default: break;
    }
    CameraKey a = lo.value(), b = hi.value(), r;
    r.ease = a.ease;
    r.x = a.x + (b.x - a.x) * t;
    r.y = a.y + (b.y - a.y) * t;
    r.scale = a.scale + (b.scale - a.scale) * t;
    r.rotation = a.rotation + (b.rotation - a.rotation) * t;
    return r;
}

CameraKey Document::cameraAt(int frame) const { return interpKeys(cameraKeys_, frame); }

// ---------------- 폴더 중첩 ----------------
void Document::setFolderParent(int folder, int parent) {
    if (folder < 0 || folder >= folders_.size()) return;
    if (parent == folder) return;
    if (parent < -1 || parent >= folders_.size()) parent = -1;
    // 순환 방지: parent 의 조상에 folder 가 있으면 거절
    for (int cur = parent, guard = 0; cur >= 0 && guard < 64; cur = folders_[cur].parent, ++guard)
        if (cur == folder) return;
    folders_[folder].parent = parent;
    setModified(true);
    notifyStructure();
}

int Document::folderDepth(int folder) const {
    int d = 0;
    for (int cur = folder; cur >= 0 && cur < folders_.size() && d < 64; cur = folders_[cur].parent) {
        if (folders_[cur].parent < 0) break;
        ++d;
    }
    return d;
}

bool Document::folderVisibleEffective(int folder) const {
    for (int cur = folder, guard = 0; cur >= 0 && cur < folders_.size() && guard < 64; cur = folders_[cur].parent, ++guard)
        if (!folders_[cur].visible) return false;
    return true;
}

qreal Document::folderOpacityEffective(int folder) const {
    qreal op = 1.0;
    for (int cur = folder, guard = 0; cur >= 0 && cur < folders_.size() && guard < 64; cur = folders_[cur].parent, ++guard)
        op *= folders_[cur].opacity;
    return op;
}

QString Document::folderPath(int folder) const {
    QStringList parts;
    for (int cur = folder, guard = 0; cur >= 0 && cur < folders_.size() && guard < 64; cur = folders_[cur].parent, ++guard)
        parts.prepend(folders_[cur].name);
    return parts.join(QStringLiteral(" / "));
}

// ---------------- 타임라인 프레임 편집 ----------------
FrameClip Document::copyFrames(int layer, int a, int b, bool allLayers) const {
    FrameClip c;
    if (frameCount_ <= 0) return c;
    a = std::clamp(a, 0, frameCount_ - 1);
    b = std::clamp(b, a, frameCount_ - 1);
    c.size = size_;
    c.count = b - a + 1;
    const int l0 = allLayers ? 0 : std::clamp(layer, 0, int(layers_.size()) - 1);
    const int l1 = allLayers ? layers_.size() - 1 : l0;
    for (int l = l0; l <= l1; ++l) {
        QVector<QImage> col;
        col.reserve(c.count);
        for (int f = a; f <= b; ++f) {
            if (f == a) {
                // 구간 맨 앞이 홀드면 직전 키 이미지를 이어받아, 붙여 넣어도 같은 그림이 나오게 한다
                const int k = keyIndexAt(l, f);
                col.push_back(k < 0 ? QImage() : layers_[l].frames[k]);
            } else {
                col.push_back(layers_[l].frames[f]);
            }
        }
        c.data.push_back(col);
        c.layerNames << layers_[l].name;
    }
    return c;
}

bool Document::pasteFrames(int layer, int at, const FrameClip& c, bool insert) {
    if (c.isEmpty() || c.size != size_) return false;
    at = std::clamp(at, 0, frameCount_);
    const int n = c.data.size();
    const int l0 = std::clamp(layer, 0, int(layers_.size()) - 1);
    if (insert) {
        for (auto& L : layers_) {
            for (int i = 0; i < c.count; ++i) L.frames.insert(at, QImage());
        }
        frameCount_ += c.count;
    } else if (at + c.count > frameCount_) {
        const int add = at + c.count - frameCount_;
        for (auto& L : layers_)
            for (int i = 0; i < add; ++i) L.frames.push_back(QImage());
        frameCount_ += add;
    }
    for (int i = 0; i < n; ++i) {
        const int l = l0 + i;
        if (l >= layers_.size()) break;
        for (int f = 0; f < c.count; ++f) layers_[l].frames[at + f] = c.data[i][f];
        // 붙여 넣은 구간 바로 뒤가 홀드였다면, 원래 보이던 그림이 유지되도록 키를 세운다
        const int after = at + c.count;
        if (!insert && after < frameCount_ && layers_[l].frames[after].isNull()) {
            // 덮어쓰기 전의 그림은 알 수 없으므로 그대로 둔다 (홀드가 새 그림을 이어받는다)
        }
    }
    undo_->clear();
    setModified(true);
    notifyStructure();
    return true;
}

bool Document::deleteFrames(int layer, int a, int b, bool allLayers, bool shift) {
    if (frameCount_ <= 0) return false;
    a = std::clamp(a, 0, frameCount_ - 1);
    b = std::clamp(b, a, frameCount_ - 1);
    const int n = b - a + 1;
    if (shift) {
        if (n >= frameCount_) return false;   // 전부 지울 수는 없다
        for (auto& L : layers_) {
            // 지우는 구간 바로 뒤가 홀드면, 보이던 그림이 사라지지 않게 키를 세운다
            if (b + 1 < frameCount_ && L.frames[b + 1].isNull()) {
                const int k = keyIndexAt(&L - layers_.constData(), b);
                if (k >= 0) L.frames[b + 1] = L.frames[k];
            }
            L.frames.remove(a, n);
        }
        frameCount_ -= n;
    } else {
        const int l0 = allLayers ? 0 : std::clamp(layer, 0, int(layers_.size()) - 1);
        const int l1 = allLayers ? layers_.size() - 1 : l0;
        for (int l = l0; l <= l1; ++l)
            for (int f = a; f <= b; ++f) layers_[l].frames[f] = QImage();
    }
    curFrame_ = std::clamp(curFrame_, 0, frameCount_ - 1);
    undo_->clear();
    setModified(true);
    notifyStructure();
    emit currentChanged();
    return true;
}

bool Document::moveFrames(int layer, int a, int b, int dest, bool allLayers) {
    const FrameClip c = copyFrames(layer, a, b, allLayers);
    if (c.isEmpty()) return false;
    a = std::clamp(a, 0, frameCount_ - 1);
    b = std::clamp(b, a, frameCount_ - 1);
    if (dest >= a && dest <= b + 1) return false;   // 제자리
    const int n = b - a + 1;
    const int l0 = allLayers ? 0 : std::clamp(layer, 0, int(layers_.size()) - 1);
    const int l1 = allLayers ? layers_.size() - 1 : l0;
    for (int l = l0; l <= l1; ++l) {
        QVector<QImage>& fr = layers_[l].frames;
        const QVector<QImage>& src = c.data[allLayers ? l : 0];
        QVector<QImage> rest;
        rest.reserve(frameCount_);
        for (int f = 0; f < frameCount_; ++f)
            if (f < a || f > b) rest.push_back(fr[f]);
        const int insertAt = dest > b ? dest - n : dest;
        for (int i = 0; i < n; ++i) rest.insert(std::clamp(insertAt, 0, int(rest.size())) + i, src[i]);
        rest.resize(frameCount_);
        fr = rest;
    }
    undo_->clear();
    setModified(true);
    notifyStructure();
    return true;
}

// ---------------- 드로잉 레코더 ----------------
void Document::addStroke(const StrokeRec& s) {
    if (s.pts.isEmpty()) return;
    strokes_.push_back(s);
    if (strokes_.size() > 5000) strokes_.remove(0, strokes_.size() - 5000);
    setModified(true);
    emit recorderChanged();
}

void Document::clearStrokes(int frame) {
    if (frame < 0) strokes_.clear();
    else
        for (int i = strokes_.size() - 1; i >= 0; --i)
            if (strokes_[i].frame == frame) strokes_.remove(i);
    setModified(true);
    emit recorderChanged();
}

int Document::strokeCount(int frame) const {
    if (frame < 0) return strokes_.size();
    int n = 0;
    for (const auto& s : strokes_) n += s.frame == frame ? 1 : 0;
    return n;
}

QImage Document::renderStrokes(int frame, double fraction, bool byTime) const {
    QImage acc(size_, QImage::Format_ARGB32_Premultiplied);
    acc.fill(Qt::transparent);
    QVector<const StrokeRec*> list;
    for (const auto& s : strokes_)
        if (s.frame == frame) list.push_back(&s);
    if (list.isEmpty()) return acc;
    auto weightOf = [&](const StrokeRec& s) {
        if (byTime) return double(std::max(80, s.ms.isEmpty() ? 80 : s.ms.last()));
        double len = 10;
        for (int i = 1; i < s.pts.size(); ++i) len += std::hypot(s.pts[i].x() - s.pts[i - 1].x(), s.pts[i].y() - s.pts[i - 1].y());
        return len;
    };
    double total = 0;
    for (auto* s : list) total += weightOf(*s);
    double budget = std::clamp(fraction, 0.0, 1.0) * total;
    int si = 0;
    for (auto* sp : list) {
        const StrokeRec& s = *sp;
        const double w = weightOf(s);
        if (budget <= 0) break;
        const double part = std::min(1.0, budget / w);   // 이 획을 얼마나 그릴지
        budget -= w;
        QImage buf(size_, QImage::Format_ARGB32_Premultiplied);
        buf.fill(Qt::transparent);
        QRandomGenerator rng(1234 + si++);
        BrushDabCache cache;
        double carry = 0;
        const QColor col = s.eraser ? QColor(Qt::black) : s.color;
        const int n = s.pts.size();
        // part 까지의 지점을 계산: 점 목록을 비율만큼 사용하고 마지막 구간은 보간한다
        const double upTo = part * (n - 1);
        brushStampAt(buf, s.brush, col, s.pts[0], s.pressure.value(0, 1.0), rng, cache);
        for (int i = 1; i < n && i - 1 < upTo + 1e-9; ++i) {
            double u = std::min(1.0, upTo - (i - 1));
            const QPointF a = s.pts[i - 1];
            const QPointF b = a + (s.pts[i] - a) * u;
            const double pa = s.pressure.value(i - 1, 1.0);
            const double pb = pa + (s.pressure.value(i, 1.0) - pa) * u;
            brushStampAlong(buf, s.brush, col, a, b, pa, pb, carry, rng, cache);
        }
        if (!s.eraser || true) brushApplyGrain(buf, brushPreset(s.brush.preset).grain);
        QPainter p(&acc);
        p.setOpacity(s.brush.opacity / 100.0);
        if (s.eraser) p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
        p.drawImage(0, 0, buf);
    }
    return acc;
}

int Document::strokesToFrames(int srcFrame, int startFrame, int nFrames, bool byTime) {
    if (strokeCount(srcFrame) == 0 || nFrames < 2 || startFrame < 0) return -1;
    if (startFrame + nFrames > frameCount_) setFrameCount(startFrame + nFrames);
    Layer L;
    L.name = QStringLiteral("획 애니메이션");
    L.frames.resize(frameCount_);
    for (int j = 0; j < nFrames; ++j)
        L.frames[startFrame + j] = renderStrokes(srcFrame, double(j + 1) / nFrames, byTime);
    layers_.push_back(L);
    curLayer_ = layers_.size() - 1;
    undo_->clear();
    setModified(true);
    notifyStructure();
    emit currentChanged();
    return curLayer_;
}

// ---------------- 벡터 레이어 ----------------
void Document::setVectorLayer(int layer, bool on) {
    if (layer < 0 || layer >= layers_.size()) return;
    if (layers_[layer].vector == on) return;
    layers_[layer].vector = on;
    if (!on) layers_[layer].vstrokes.clear();
    setModified(true);
    notifyStructure();
}

const QVector<VecStroke>* Document::vectorStrokes(int layer, int frame) const {
    if (layer < 0 || layer >= layers_.size() || !layers_[layer].vector) return nullptr;
    const int k = keyIndexAt(layer, frame);
    const int use = layers_[layer].vstrokes.contains(frame) ? frame : k;
    auto it = layers_[layer].vstrokes.constFind(use);
    return it == layers_[layer].vstrokes.constEnd() ? nullptr : &it.value();
}

int Document::vectorStrokeCount(int layer, int frame) const {
    const QVector<VecStroke>* s = vectorStrokes(layer, frame);
    return s ? s->size() : 0;
}

QImage Document::renderVectorFrame(int layer, int frame) const {
    QImage out(size_, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    const QVector<VecStroke>* list = vectorStrokes(layer, frame);
    if (!list) return out;
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing, true);
    for (const VecStroke& s : *list) {
        if (!s.visible || s.pts.size() < 1) continue;
        QPainterPath path;
        path.moveTo(s.pts[0]);
        if (s.pts.size() == 1) path.addEllipse(s.pts[0], s.width / 2, s.width / 2);
        for (int i = 1; i < s.pts.size(); ++i) {
            // 점 사이를 부드럽게 잇는다 (카트멀-롬 비슷하게)
            const QPointF p1 = s.pts[i - 1], p2 = s.pts[i];
            const QPointF p0 = i > 1 ? s.pts[i - 2] : p1;
            const QPointF p3 = i + 1 < s.pts.size() ? s.pts[i + 1] : p2;
            path.cubicTo(p1 + (p2 - p0) / 6.0, p2 - (p3 - p1) / 6.0, p2);
        }
        if (s.closed) path.closeSubpath();
        p.setCompositionMode(s.eraser ? QPainter::CompositionMode_DestinationOut : QPainter::CompositionMode_SourceOver);
        if (s.filled && s.pts.size() > 2) {
            p.setBrush(s.fill);
            p.setPen(Qt::NoPen);
            p.drawPath(path);
        }
        if (s.width > 0) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(s.eraser ? QColor(Qt::black) : s.color, std::max(0.5, s.width), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawPath(path);
        }
    }
    return out;
}

void Document::rebuildVectorFrame(int layer, int frame) {
    if (layer < 0 || layer >= layers_.size() || !layers_[layer].vector) return;
    if (frame < 0 || frame >= frameCount_) return;
    layers_[layer].frames[frame] = renderVectorFrame(layer, frame);
    setModified(true);
    emit changed();
}

void Document::setVectorStrokes(int layer, int frame, const QVector<VecStroke>& s) {
    if (layer < 0 || layer >= layers_.size() || !layers_[layer].vector) return;
    if (s.isEmpty()) layers_[layer].vstrokes.remove(frame);
    else layers_[layer].vstrokes[frame] = s;
    rebuildVectorFrame(layer, frame);
    notifyStructure();
}

void Document::addVectorStroke(int layer, int frame, const VecStroke& s) {
    if (layer < 0 || layer >= layers_.size() || !layers_[layer].vector || s.pts.isEmpty()) return;
    // 홀드 프레임에 그리면 그 자리에 새 키가 생긴다 (앞 키의 획을 물려받는다)
    if (!layers_[layer].vstrokes.contains(frame)) {
        const QVector<VecStroke>* prev = vectorStrokes(layer, frame);
        layers_[layer].vstrokes[frame] = prev ? *prev : QVector<VecStroke>();
    }
    layers_[layer].vstrokes[frame].push_back(s);
    rebuildVectorFrame(layer, frame);
    notifyStructure();
}

void Document::flattenVectorLayer(int layer) {
    if (layer < 0 || layer >= layers_.size() || !layers_[layer].vector) return;
    for (auto it = layers_[layer].vstrokes.cbegin(); it != layers_[layer].vstrokes.cend(); ++it)
        if (it.key() >= 0 && it.key() < frameCount_) layers_[layer].frames[it.key()] = renderVectorFrame(layer, it.key());
    layers_[layer].vstrokes.clear();
    layers_[layer].vector = false;
    undo_->clear();
    setModified(true);
    notifyStructure();
}

// 획 하나를 n 개의 점으로 고르게 다시 뽑는다
static QVector<QPointF> resampleStroke(const QVector<QPointF>& pts, int n) {
    QVector<QPointF> out;
    if (pts.isEmpty() || n < 2) return pts;
    if (pts.size() == 1) {
        for (int i = 0; i < n; ++i) out.push_back(pts[0]);
        return out;
    }
    QVector<double> acc{0.0};
    for (int i = 1; i < pts.size(); ++i)
        acc.push_back(acc.last() + std::hypot(pts[i].x() - pts[i - 1].x(), pts[i].y() - pts[i - 1].y()));
    const double total = acc.last();
    for (int i = 0; i < n; ++i) {
        const double d = total * i / (n - 1);
        int k = 0;
        while (k + 1 < acc.size() - 1 && acc[k + 1] < d) ++k;
        const double seg = acc[k + 1] - acc[k];
        const double u = seg < 1e-9 ? 0.0 : (d - acc[k]) / seg;
        out.push_back(pts[k] + (pts[k + 1] - pts[k]) * u);
    }
    return out;
}

QVector<VecStroke> Document::interpolateStrokes(const QVector<VecStroke>& a, const QVector<VecStroke>& b, double t) {
    t = std::clamp(t, 0.0, 1.0);
    QVector<VecStroke> out;
    const int n = std::max(a.size(), b.size());
    for (int i = 0; i < n; ++i) {
        const bool ha = i < a.size(), hb = i < b.size();
        if (!ha && !hb) continue;
        if (!hb) {   // 사라지는 획: 점점 옅어진다
            VecStroke s = a[i];
            s.color.setAlphaF(std::clamp(s.color.alphaF() * (1.0 - t), 0.0, 1.0));
            if (s.color.alpha() > 2) out.push_back(s);
            continue;
        }
        if (!ha) {   // 새로 생기는 획: 점점 진해진다
            VecStroke s = b[i];
            s.color.setAlphaF(std::clamp(s.color.alphaF() * t, 0.0, 1.0));
            if (s.color.alpha() > 2) out.push_back(s);
            continue;
        }
        const VecStroke& A = a[i];
        const VecStroke& B = b[i];
        const int pn = int(std::max<qsizetype>(2, std::max(A.pts.size(), B.pts.size())));
        const QVector<QPointF> pa = resampleStroke(A.pts, pn), pb = resampleStroke(B.pts, pn);
        VecStroke s = A;
        s.pts.clear();
        s.pressure.clear();
        for (int k = 0; k < pn; ++k) {
            s.pts.push_back(pa[k] + (pb[k] - pa[k]) * t);
            s.pressure.push_back(1.0f);
        }
        s.width = A.width + (B.width - A.width) * t;
        s.color = QColor::fromRgbF(A.color.redF() + (B.color.redF() - A.color.redF()) * t,
                                   A.color.greenF() + (B.color.greenF() - A.color.greenF()) * t,
                                   A.color.blueF() + (B.color.blueF() - A.color.blueF()) * t,
                                   A.color.alphaF() + (B.color.alphaF() - A.color.alphaF()) * t);
        out.push_back(s);
    }
    return out;
}

// ---------------- 자동 중간 그림 ----------------
double Document::easeValue(double t, int ease) {
    t = std::clamp(t, 0.0, 1.0);
    switch (ease) {
    case 1: return t * t * t;                                  // 느리게 시작
    case 2: return 1.0 - (1.0 - t) * (1.0 - t) * (1.0 - t);    // 느리게 끝
    case 3: return t * t * (3.0 - 2.0 * t);                    // 느리게 시작·끝
    default: return t;
    }
}

int Document::makeInbetweens(int layer, int a, int b, int mode, int ease) {
    if (layer < 0 || layer >= layers_.size()) return 0;
    if (layers_[layer].locked) return 0;
    a = std::clamp(a, 0, frameCount_ - 1);
    b = std::clamp(b, 0, frameCount_ - 1);
    if (b - a < 2) return 0;                      // 사이에 채울 자리가 없다
    const int ka = keyIndexAt(layer, a), kb = keyIndexAt(layer, b);
    if (ka != a || kb != b) return 0;             // 양 끝이 키여야 한다

    const bool useVector = layers_[layer].vector && layers_[layer].vstrokes.contains(a) && layers_[layer].vstrokes.contains(b) &&
                           (mode == 0 || mode == 1);
    const QImage imgA = layers_[layer].frames[a];
    const QImage imgB = layers_[layer].frames[b];
    int made = 0;
    for (int f = a + 1; f < b; ++f) {
        const double raw = double(f - a) / double(b - a);
        const double t = easeValue(raw, ease);
        if (useVector) {
            const QVector<VecStroke> s = interpolateStrokes(layers_[layer].vstrokes[a], layers_[layer].vstrokes[b], t);
            layers_[layer].vstrokes[f] = s;
            layers_[layer].frames[f] = renderVectorFrame(layer, f);
        } else if (mode == 2) {
            layers_[layer].frames[f] = Effects::crossfade(imgA, imgB, t);
        } else {
            layers_[layer].frames[f] = Effects::morph(imgA, imgB, t);
        }
        ++made;
    }
    undo_->clear();
    setModified(true);
    notifyStructure();
    return made;
}

// ---------------- 퍼펫 ----------------
void Document::setPuppet(int l, bool on) {
    if (l < 0 || l >= layers_.size()) return;
    Layer& L = layers_[l];
    if (L.puppet == on) return;
    L.puppet = on;
    if (on && L.puppetId == 0) L.puppetId = nextPuppetId_++;
    setModified(true);
    notifyStructure();
}

CameraKey Document::puppetAt(int l, int frame) const {
    if (l < 0 || l >= layers_.size() || layers_[l].pkeys.isEmpty()) return CameraKey();
    return interpKeys(layers_[l].pkeys, frame);
}

int Document::puppetParentLayer(int l) const {
    if (l < 0 || l >= layers_.size() || layers_[l].puppetParent == 0) return -1;
    for (int i = 0; i < layers_.size(); ++i)
        if (i != l && layers_[i].puppet && layers_[i].puppetId == layers_[l].puppetParent) return i;
    return -1;
}

QTransform Document::puppetTransform(int l, int frame) const {
    QTransform total;
    int cur = l, guard = 0;
    while (cur >= 0 && guard++ < 16) {            // 자식 → 부모 순으로 곱한다 (순환 연결은 16단계에서 끊는다)
        const Layer& L = layers_[cur];
        if (!L.puppet) break;
        const CameraKey k = puppetAt(cur, frame);
        const double px = L.pivotX < 0 ? size_.width() / 2.0 : L.pivotX;
        const double py = L.pivotY < 0 ? size_.height() / 2.0 : L.pivotY;
        const double s = std::max(0.01, double(k.scale));
        QTransform t;
        t.translate(k.x, k.y);
        t.translate(px, py);
        t.rotate(k.rotation);
        t.scale(s, s);
        t.translate(-px, -py);
        total = total * t;
        cur = puppetParentLayer(cur);
    }
    return total;
}

void Document::setPuppetKey(int l, int frame, const CameraKey& k) {
    if (l < 0 || l >= layers_.size() || !layers_[l].puppet) return;
    layers_[l].pkeys[frame] = k;
    setModified(true);
    notifyStructure();
}

void Document::removePuppetKey(int l, int frame) {
    if (l < 0 || l >= layers_.size() || !layers_[l].pkeys.remove(frame)) return;
    setModified(true);
    notifyStructure();
}

void Document::setPuppetPivot(int l, qreal x, qreal y) {
    if (l < 0 || l >= layers_.size()) return;
    layers_[l].pivotX = x;
    layers_[l].pivotY = y;
    setModified(true);
    notifyStructure();
}

void Document::setPuppetParent(int l, int parent) {
    if (l < 0 || l >= layers_.size()) return;
    if (parent < 0 || parent >= layers_.size() || parent == l || !layers_[parent].puppet) {
        layers_[l].puppetParent = 0;
    } else {
        // 순환 방지: parent 의 조상에 l 이 있으면 거절
        for (int cur = parent, guard = 0; cur >= 0 && guard < 16; cur = puppetParentLayer(cur), ++guard)
            if (cur == l) return;
        layers_[l].puppetParent = layers_[parent].puppetId;
    }
    setModified(true);
    notifyStructure();
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

// ---------------- 자동 채색 (일괄 채색) ----------------
int Document::floodFill(QImage& img, const QPoint& seed, const QColor& color, int tolerance) {
    if (img.isNull() || !img.rect().contains(seed)) return 0;
    if (img.format() != QImage::Format_ARGB32_Premultiplied)
        img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const int w = img.width(), h = img.height();
    const QRgb target = img.pixel(seed);
    const QRgb fill = qPremultiply(color.rgba());
    if (target == fill) return 0;
    const int tol = std::max(0, tolerance);
    auto similar = [&](QRgb c) {
        const int d = std::abs(qAlpha(c) - qAlpha(target)) + std::abs(qRed(c) - qRed(target)) +
                      std::abs(qGreen(c) - qGreen(target)) + std::abs(qBlue(c) - qBlue(target));
        return d <= tol;
    };
    QVector<uchar> seen(size_t(w) * h, 0);
    QVector<QPoint> stack;
    stack.push_back(seed);
    seen[size_t(seed.y()) * w + seed.x()] = 1;
    int painted = 0;
    while (!stack.isEmpty()) {
        const QPoint c = stack.takeLast();
        reinterpret_cast<QRgb*>(img.scanLine(c.y()))[c.x()] = fill;
        ++painted;
        const QPoint nb[4] = {{c.x() + 1, c.y()}, {c.x() - 1, c.y()}, {c.x(), c.y() + 1}, {c.x(), c.y() - 1}};
        for (const QPoint& n : nb) {
            if (n.x() < 0 || n.y() < 0 || n.x() >= w || n.y() >= h) continue;
            uchar& st = seen[size_t(n.y()) * w + n.x()];
            if (st) continue;
            st = 1;
            if (similar(reinterpret_cast<const QRgb*>(img.constScanLine(n.y()))[n.x()])) stack.push_back(n);
        }
    }
    return painted;
}

void Document::recordFill(const QPoint& at, const QColor& c) {
    if (fillMarks_.size() > 256) fillMarks_.removeFirst();
    fillMarks_.push_back({at, c});
}

void Document::clearFillMarks() {
    fillMarks_.clear();
}

int Document::batchFill(int layer, int from, int to, const QVector<FillMark>& marks, int tolerance) {
    if (layer < 0 || layer >= layers_.size() || marks.isEmpty()) return 0;
    if (layers_[layer].locked) return 0;
    from = std::clamp(from, 0, frameCount_ - 1);
    to = std::clamp(to, 0, frameCount_ - 1);
    if (to < from) std::swap(from, to);
    auto& fr = layers_[layer].frames;
    int done = 0;
    undo_->beginMacro(QStringLiteral("자동 채색"));
    for (int f = from; f <= to; ++f) {
        if (fr[f].isNull()) continue;            // 홀드 중인 칸은 건너뛴다 (키만 칠한다)
        const QImage before = fr[f];
        QImage after = before;
        int painted = 0;
        for (const FillMark& m : marks) painted += floodFill(after, m.at, m.color, tolerance);
        if (painted <= 0) continue;
        pushKeyEdit(layer, f, before, after, QStringLiteral("자동 채색"));
        ++done;
    }
    undo_->endMacro();
    if (done) notifyChanged();
    return done;
}

// ---------------- 색 공간 (ICC) ----------------
int Document::colorSpaceCount() { return 5; }

QString Document::colorSpaceName(int i) {
    static const char* n[] = {"sRGB (기본)", "Display P3", "Adobe RGB (1998)", "ProPhoto RGB", "붙이지 않음"};
    return QString::fromUtf8(n[std::clamp(i, 0, 4)]);
}

void Document::setColorSpace(int i) {
    i = std::clamp(i, 0, colorSpaceCount() - 1);
    if (i == colorSpace_) return;
    colorSpace_ = i;
    setModified(true);
    emit changed();
}

void Document::tagColorSpace(QImage& img) const {
    if (img.isNull()) return;
    QColorSpace cs;
    switch (colorSpace_) {
    case 0: cs = QColorSpace(QColorSpace::SRgb); break;
    case 1: cs = QColorSpace(QColorSpace::DisplayP3); break;
    case 2: cs = QColorSpace(QColorSpace::AdobeRgb); break;
    case 3: cs = QColorSpace(QColorSpace::ProPhotoRgb); break;
    default: return;   // 붙이지 않음
    }
    if (cs.isValid()) img.setColorSpace(cs);
}

CameraKey Document::cameraAtFollow(int frame, int followPct) const {
    CameraKey k = cameraAt(frame);
    const double f = std::clamp(followPct, 0, 400) / 100.0;
    if (std::abs(f - 1.0) < 1e-9) return k;
    // 카메라의 움직임을 비율만큼만 적용한다 (0 이면 아예 안 따라가고, 1 이면 지금과 같다)
    CameraKey o = k;
    o.x = k.x * f;
    o.y = k.y * f;
    o.rotation = k.rotation * f;
    o.scale = 1.0 + (k.scale - 1.0) * f;
    return o;
}

bool Document::hasMultiplane() const {
    for (const Layer& L : layers_)
        if (L.visible && L.camFollow != 100) return true;
    return false;
}

QPolygonF Document::cameraFrame(int frame) const {
    bool ok = false;
    const QTransform inv = cameraTransform(cameraAt(frame)).inverted(&ok);
    if (!ok) return QPolygonF(QRectF(QPointF(0, 0), QSizeF(size_)));
    return inv.map(QPolygonF(QRectF(QPointF(0, 0), QSizeF(size_))));
}

// ---------------- 라이브러리 ----------------
void Document::setLibraryCategory(int i, const QString& cat) {
    while (libCats_.size() < libNames_.size()) libCats_ << QString();
    if (i < 0 || i >= libCats_.size()) return;
    libCats_[i] = cat.trimmed();
    setModified(true);
    emit libraryChanged();
}

void Document::setLibraryName(int i, const QString& name) {
    if (i < 0 || i >= libNames_.size() || name.trimmed().isEmpty()) return;
    libNames_[i] = name.trimmed();
    setModified(true);
    emit libraryChanged();
}

QStringList Document::libraryCategories() const {
    QStringList out;
    for (const QString& s : libCats_)
        if (!s.isEmpty() && !out.contains(s)) out << s;
    std::sort(out.begin(), out.end());
    return out;
}

int Document::addToLibrary(const QString& name, const QImage& img) {
    libNames_.push_back(name);
    libCats_.push_back(QString());
    libImages_.push_back(img);
    setModified(true);
    emit libraryChanged();
    return libNames_.size() - 1;
}

void Document::removeFromLibrary(int i) {
    if (i >= 0 && i < libCats_.size()) libCats_.remove(i);
    if (i < 0 || i >= libNames_.size()) return;
    libNames_.removeAt(i);
    libImages_.remove(i);
    setModified(true);
    emit libraryChanged();
}

void Document::setAudioPath(const QString& p) {
    audioTracks_.clear();
    if (!p.isEmpty()) {
        AudioTrack t;
        t.path = p;
        t.name = QFileInfo(p).completeBaseName();
        audioTracks_.push_back(t);
    }
    setModified(true);
    emit audioChanged();
}

int Document::addAudioTrack(const QString& path, const QString& name) {
    AudioTrack t;
    t.path = path;
    t.name = name.isEmpty() ? QFileInfo(path).completeBaseName() : name;
    audioTracks_.push_back(t);
    setModified(true);
    emit audioChanged();
    return audioTracks_.size() - 1;
}

void Document::removeAudioTrack(int i) {
    if (i < 0 || i >= audioTracks_.size()) return;
    audioTracks_.removeAt(i);
    setModified(true);
    emit audioChanged();
}

int Document::duplicateAudioTrack(int i) {
    if (i < 0 || i >= audioTracks_.size()) return -1;
    AudioTrack t = audioTracks_[i];
    t.name += QStringLiteral(" 복사");
    audioTracks_.push_back(t);
    setModified(true);
    emit audioChanged();
    return audioTracks_.size() - 1;
}

void Document::moveAudioTrack(int i, int delta) {
    const int j = i + delta;
    if (i < 0 || j < 0 || i >= audioTracks_.size() || j >= audioTracks_.size()) return;
    audioTracks_.swapItemsAt(i, j);
    setModified(true);
    emit audioChanged();
}

void Document::updateAudioTrack(int i, const AudioTrack& t) {
    if (i < 0 || i >= audioTracks_.size()) return;
    audioTracks_[i] = t;
    setModified(true);
    emit audioChanged();
}

void Document::setMaster(double volume, bool muted) {
    masterVolume_ = volume;
    masterMuted_ = muted;
    setModified(true);
    emit audioChanged();
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
    m["camfollow"] = l.camFollow;
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
    if (l.vector) {
        m["vector"] = true;
        QVariantList vs;
        for (auto it = l.vstrokes.cbegin(); it != l.vstrokes.cend(); ++it) {
            QVariantList strokes;
            for (const VecStroke& s : it.value()) {
                QVariantList pts;
                for (int i = 0; i < s.pts.size(); ++i) pts << s.pts[i].x() << s.pts[i].y() << double(s.pressure.value(i, 1.0f));
                strokes << QVariant(QVariantMap{{"pts", pts}, {"color", s.color.name(QColor::HexArgb)}, {"w", s.width},
                                                {"eraser", s.eraser}, {"closed", s.closed}, {"filled", s.filled},
                                                {"fill", s.fill.name(QColor::HexArgb)}, {"visible", s.visible}});
            }
            vs << QVariant(QVariantMap{{"frame", it.key()}, {"strokes", strokes}});
        }
        m["vstrokes"] = vs;
    }
    if (l.puppet) {
        m["puppet"] = true;
        m["pid"] = l.puppetId;
        m["ppar"] = l.puppetParent;
        m["pivx"] = l.pivotX;
        m["pivy"] = l.pivotY;
        QVariantList pk;
        for (auto it = l.pkeys.cbegin(); it != l.pkeys.cend(); ++it)
            pk << QVariant(QVariantList{it.key(), it.value().x, it.value().y, it.value().scale, it.value().rotation, it.value().ease});
        m["pkeys"] = pk;
    }
    return m;
}

void layerExtLoad(Layer& l, const QVariantMap& m) {
    l.preBehavior = std::clamp(m.value("pre", 0).toInt(), 0, 3);
    l.postBehavior = std::clamp(m.value("post", 3).toInt(), 0, 3);
    l.endFrame = m.value("end", -1).toInt();
    l.camFollow = std::clamp(m.value("camfollow", 100).toInt(), 0, 400);
    l.blend = std::clamp(m.value("blend", 0).toInt(), 0, Blend::Count - 1);
    l.collapsed = m.value("collapsed", false).toBool();
    l.colorGroup = m.value("cg", -1).toInt();
    l.stencil = m.value("stencil", false).toBool();
    l.stencilInvert = m.value("stencilInv", false).toBool();
    l.preserveAlpha = m.value("pa", false).toBool();
    const QVariantMap names = m.value("names").toMap(), marks = m.value("marks").toMap();
    for (auto it = names.cbegin(); it != names.cend(); ++it) l.instanceNames[it.key().toInt()] = it.value().toString();
    for (auto it = marks.cbegin(); it != marks.cend(); ++it) l.imageMarks[it.key().toInt()] = it.value().toInt();
    l.vector = m.value("vector", false).toBool();
    l.vstrokes.clear();
    if (l.vector) {
        for (const QVariant& fv : m.value("vstrokes").toList()) {
            const QVariantMap fm = fv.toMap();
            QVector<VecStroke> list;
            for (const QVariant& sv : fm.value("strokes").toList()) {
                const QVariantMap sm = sv.toMap();
                VecStroke s;
                const QVariantList pts = sm.value("pts").toList();
                for (int i = 0; i + 2 < pts.size(); i += 3) {
                    s.pts.push_back(QPointF(pts[i].toDouble(), pts[i + 1].toDouble()));
                    s.pressure.push_back(pts[i + 2].toFloat());
                }
                s.color = QColor(sm.value("color", "#ff000000").toString());
                s.width = sm.value("w", 6).toDouble();
                s.eraser = sm.value("eraser").toBool();
                s.closed = sm.value("closed").toBool();
                s.filled = sm.value("filled").toBool();
                s.fill = QColor(sm.value("fill", "#00000000").toString());
                s.visible = sm.value("visible", true).toBool();
                if (!s.pts.isEmpty()) list.push_back(s);
            }
            if (!list.isEmpty()) l.vstrokes[fm.value("frame").toInt()] = list;
        }
    }
    l.puppet = m.value("puppet", false).toBool();
    l.puppetId = m.value("pid", 0).toInt();
    l.puppetParent = m.value("ppar", 0).toInt();
    l.pivotX = m.value("pivx", -1).toDouble();
    l.pivotY = m.value("pivy", -1).toDouble();
    l.pkeys.clear();
    for (const QVariant& v : m.value("pkeys").toList()) {
        const QVariantList e = v.toList();
        if (e.size() < 6) continue;
        CameraKey k;
        k.x = e[1].toDouble(); k.y = e[2].toDouble(); k.scale = e[3].toDouble(); k.rotation = e[4].toDouble(); k.ease = e[5].toInt();
        l.pkeys[e[0].toInt()] = k;
    }
}
} // namespace

QVariantMap Document::extraSave() const {
    QVariantMap m;
    QVariantList bm;
    for (int b : bookmarks_) bm << b;
    m["bookmarks"] = bm;
    m["bookmarksOn"] = bookmarksOn_;
    m["colorSpace"] = colorSpace_;
    QVariantList mc;
    for (const MarkColor& c : markColors_) mc << QVariant(QVariantList{c.name, c.color.name()});
    m["markColors"] = mc;
    QVariantList at;
    for (const AudioTrack& t : audioTracks_)
        at << QVariantMap{{"path", t.path}, {"name", t.name}, {"volume", t.volume}, {"offset", t.offsetFrames}, {"muted", t.muted}, {"locked", t.locked}};
    m["audioTracks"] = at;
    QVariantMap ease;
    for (auto it = cameraKeys_.cbegin(); it != cameraKeys_.cend(); ++it)
        if (it.value().ease != 0) ease[QString::number(it.key())] = it.value().ease;
    m["cameraEase"] = ease;
    QVariantList sl;
    for (const StrokeRec& s : strokes_) {
        QVariantList pts;
        for (int i = 0; i < s.pts.size(); ++i) pts << s.pts[i].x() << s.pts[i].y() << double(s.pressure.value(i, 1.0f)) << s.ms.value(i, 0);
        sl << QVariant(QVariantMap{{"layer", s.layer}, {"frame", s.frame}, {"size", s.brush.size}, {"op", s.brush.opacity},
                                   {"hard", s.brush.hardness}, {"press", s.brush.pressure}, {"preset", s.brush.preset},
                                   {"color", s.color.name(QColor::HexArgb)}, {"eraser", s.eraser}, {"pts", pts}});
    }
    QVariantList fp;
    for (const Folder& fo : folders_) fp << fo.parent;
    m["folderParents"] = fp;
    QVariantList lc;
    for (const QString& s : libCats_) lc << s;
    m["libraryCategories"] = lc;
    m["strokes"] = sl;
    m["renderCam"] = QVariantMap{{"on", renderCam_.on}, {"w", renderCam_.size.width()}, {"h", renderCam_.size.height()}, {"fit", renderCam_.fit}};
    m["masterVolume"] = masterVolume_;
    m["masterMuted"] = masterMuted_;
    return m;
}

void Document::extraLoad(const QVariantMap& m) {
    {
        const QVariantMap ease = m.value("cameraEase").toMap();
        for (auto it = ease.cbegin(); it != ease.cend(); ++it)
            if (cameraKeys_.contains(it.key().toInt())) cameraKeys_[it.key().toInt()].ease = it.value().toInt();
    }
    bookmarks_.clear();
    for (const QVariant& v : m.value("bookmarks").toList()) bookmarks_.insert(v.toInt());
    bookmarksOn_ = m.value("bookmarksOn", true).toBool();
    colorSpace_ = std::clamp(m.value("colorSpace", 0).toInt(), 0, colorSpaceCount() - 1);
    QVector<MarkColor> mc;
    for (const QVariant& v : m.value("markColors").toList()) {
        const QVariantList e = v.toList();
        if (e.size() == 2) mc.push_back({e[0].toString(), QColor(e[1].toString())});
    }
    if (m.contains("audioTracks")) { // 여러 트랙 (이전 파일에는 없다 → 위에서 읽은 단일 소리를 유지)
        audioTracks_.clear();
        for (const QVariant& v : m.value("audioTracks").toList()) {
            const QVariantMap e = v.toMap();
            AudioTrack t;
            t.path = e.value("path").toString();
            t.name = e.value("name").toString();
            t.volume = e.value("volume", 1.0).toDouble();
            t.offsetFrames = e.value("offset", 0).toInt();
            t.muted = e.value("muted", false).toBool();
            t.locked = e.value("locked", false).toBool();
            audioTracks_.push_back(t);
        }
    }
    {
        const QVariantMap rc = m.value("renderCam").toMap();
        renderCam_.on = rc.value("on", false).toBool();
        renderCam_.size = QSize(std::clamp(rc.value("w", 1920).toInt(), 16, 16384), std::clamp(rc.value("h", 1080).toInt(), 16, 16384));
        renderCam_.fit = std::clamp(rc.value("fit", 0).toInt(), 0, 2);
    }
    {
        libCats_.clear();
        for (const QVariant& v : m.value("libraryCategories").toList()) libCats_ << v.toString();
        while (libCats_.size() < libNames_.size()) libCats_ << QString();
        libCats_.resize(libNames_.size());
    }
    {
        const QVariantList fp = m.value("folderParents").toList();
        for (int i = 0; i < folders_.size() && i < fp.size(); ++i) {
            const int par = fp[i].toInt();
            folders_[i].parent = (par >= -1 && par < folders_.size() && par != i) ? par : -1;
        }
    }
    strokes_.clear();
    for (const QVariant& v : m.value("strokes").toList()) {
        const QVariantMap e = v.toMap();
        StrokeRec s;
        s.layer = e.value("layer").toInt();
        s.frame = e.value("frame").toInt();
        s.brush.size = e.value("size", 6).toInt();
        s.brush.opacity = e.value("op", 100).toInt();
        s.brush.hardness = e.value("hard", 85).toInt();
        s.brush.pressure = e.value("press").toBool();
        s.brush.preset = e.value("preset").toInt();
        s.color = QColor(e.value("color", "#ff000000").toString());
        s.eraser = e.value("eraser").toBool();
        const QVariantList pts = e.value("pts").toList();
        for (int i = 0; i + 3 < pts.size(); i += 4) {
            s.pts.push_back(QPointF(pts[i].toDouble(), pts[i + 1].toDouble()));
            s.pressure.push_back(pts[i + 2].toFloat());
            s.ms.push_back(pts[i + 3].toInt());
        }
        if (!s.pts.isEmpty()) strokes_.push_back(s);
    }
    masterVolume_ = m.value("masterVolume", 1.0).toDouble();
    masterMuted_ = m.value("masterMuted", false).toBool();
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
    ds << audioPath();
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
                if (t < 0 || t > int(Effects::Type::Last)) return false;
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
    renderCam_ = RenderCamera();
    strokes_.clear();
    nextPuppetId_ = 1;
    for (const Layer& pl : layers_) nextPuppetId_ = std::max(nextPuppetId_, pl.puppetId + 1);
    folders_ = std::move(loadedFolders);
    cameraEnabled_ = camOn;
    cameraKeys_ = std::move(camKeys);
    libNames_ = std::move(libNames);
    libCats_.clear();
    libCats_.resize(libNames_.size());
    libImages_ = std::move(libImages);
    audioTracks_.clear();
    if (!audio.isEmpty()) {
        AudioTrack t;
        t.path = audio;
        t.name = QFileInfo(audio).completeBaseName();
        audioTracks_.push_back(t);
    }
    notes_ = std::move(loadedNotes);
    extraLoad(pendingExt);
    curLayer_ = 0;
    curFrame_ = 0;
    path_ = path;
    relinkMissingAudio();
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

QImage Document::renderFrame(int frame, const QColor& bg, bool useCamera) const {
    Document* self = const_cast<Document*>(this);
    const int keep = proxyDiv_;
    self->proxyDiv_ = 1;
    QImage world(size_, QImage::Format_ARGB32_Premultiplied);
    world.fill(Qt::transparent);
    drawLayers(world, QTransform(), frame);
    self->proxyDiv_ = keep;
    QImage out(size_, QImage::Format_ARGB32_Premultiplied);
    out.fill(bg.alpha() == 0 ? QColor(Qt::transparent) : bg);
    QPainter p(&out);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    if (useCamera && cameraEnabled_) {
        p.setTransform(cameraTransform(cameraAt(frame)));
        if (bg.alpha() != 0) p.fillRect(QRect(QPoint(0, 0), size_), bg);
    }
    p.drawImage(0, 0, world);
    p.end();
    if (renderCam_.on && renderCam_.size.isValid() && renderCam_.size != size_) {
        const QSize rs = renderCam_.size;
        QImage fin(rs, QImage::Format_ARGB32_Premultiplied);
        fin.fill(bg.alpha() == 0 ? QColor(Qt::transparent) : bg);
        QPainter q(&fin);
        q.setRenderHint(QPainter::SmoothPixmapTransform, true);
        if (renderCam_.fit == 2) {
            q.drawImage(QRect(QPoint(0, 0), rs), out);
        } else {
            const double sx = double(rs.width()) / size_.width(), sy = double(rs.height()) / size_.height();
            const double s = renderCam_.fit == 1 ? std::max(sx, sy) : std::min(sx, sy);
            const QSizeF ds(size_.width() * s, size_.height() * s);
            q.drawImage(QRectF((rs.width() - ds.width()) / 2, (rs.height() - ds.height()) / 2, ds.width(), ds.height()), out);
        }
        return fin;
    }
    return out;
}

bool Document::exportFrame(int frame, const QString& path) const {
    Document* self = const_cast<Document*>(this);
    const int keep = proxyDiv_;
    self->proxyDiv_ = 1;
    QImage out = composite(frame, true, true);
    tagColorSpace(out);           // 내보내는 그림에 색 공간을 붙인다
    const bool ok = out.save(path, "PNG");
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

void Document::reverseKeys(int layer, int a, int b) {
    if (layer < 0 || layer >= layers_.size()) return;
    a = std::clamp(a, 0, frameCount_ - 1);
    b = std::clamp(b, 0, frameCount_ - 1);
    if (a >= b) return;
    auto& fr = layers_[layer].frames;
    // 구간 안의 (키 위치, 이미지) 를 모아 위치만 거울처럼 뒤집는다
    QVector<QPair<int, QImage>> keys;
    for (int f = a; f <= b; ++f)
        if (!fr[f].isNull()) keys.push_back({f, fr[f]});
    if (keys.size() < 2) return;
    // 각 키의 노출 길이를 유지한 채 순서만 뒤집는다
    QVector<int> lens;
    for (int i = 0; i < keys.size(); ++i) {
        const int next = i + 1 < keys.size() ? keys[i + 1].first : b + 1;
        lens.push_back(next - keys[i].first);
    }
    for (int f = a; f <= b; ++f) fr[f] = QImage();
    int pos = keys.first().first;
    for (int i = keys.size() - 1; i >= 0; --i) {
        fr[pos] = keys[i].second;
        pos += lens[i];
    }
    undo_->clear();
    notifyStructure();
    setModified(true);
}

void Document::setUniformExposure(int layer, int n) {
    if (layer < 0 || layer >= layers_.size() || n < 1) return;
    auto& fr = layers_[layer].frames;
    QVector<QImage> imgs;
    for (const QImage& im : fr)
        if (!im.isNull()) imgs.push_back(im);
    if (imgs.isEmpty()) return;
    const int start = [&] { for (int f = 0; f < fr.size(); ++f) if (!fr[f].isNull()) return f; return 0; }();
    for (QImage& im : fr) im = QImage();
    for (int i = 0, pos = start; i < imgs.size() && pos < frameCount_; ++i, pos += n) fr[pos] = imgs[i];
    undo_->clear();
    notifyStructure();
    setModified(true);
}

// 프로젝트 파일이 옮겨졌을 때, 소리 파일을 .ony 옆이나 _files 폴더에서 다시 찾아 준다.
int Document::relinkMissingAudio() {
    if (path_.isEmpty()) return 0;
    const QDir home = QFileInfo(path_).dir();
    const QString base = QFileInfo(path_).completeBaseName();
    int fixed = 0;
    for (AudioTrack& t : audioTracks_) {
        if (t.path.isEmpty() || QFile::exists(t.path)) continue;
        const QString fileName = QFileInfo(t.path).fileName();
        const QStringList tries = {home.filePath(fileName),
                                   home.filePath(base + "_files/" + fileName),
                                   home.filePath("소리/" + fileName),
                                   home.filePath("audio/" + fileName)};
        for (const QString& cand : tries) {
            if (!QFile::exists(cand)) continue;
            t.path = cand;
            ++fixed;
            break;
        }
    }
    if (fixed) emit audioChanged();
    return fixed;
}

// 프로젝트와 거기에 쓰인 소리 파일을 한 폴더에 모아서 저장한다 (다른 컴퓨터로 옮겨도 소리가 따라간다).
bool Document::packTo(const QString& onyPath, QString* report) {
    const QFileInfo fi(onyPath);
    QDir home = fi.dir();
    if (!home.exists() && !QDir().mkpath(home.absolutePath())) {
        if (report) *report = QStringLiteral("폴더를 만들지 못했습니다");
        return false;
    }
    const QString sub = fi.completeBaseName() + "_files";
    const QVector<AudioTrack> keep = audioTracks_;
    int copied = 0;
    QStringList missing;
    if (!audioTracks_.isEmpty() && !home.exists(sub) && !home.mkpath(sub)) {
        audioTracks_ = keep;
        if (report) *report = QStringLiteral("소리 폴더를 만들지 못했습니다");
        return false;
    }
    for (AudioTrack& t : audioTracks_) {
        if (t.path.isEmpty()) continue;
        if (!QFile::exists(t.path)) {
            missing << QFileInfo(t.path).fileName();
            continue;
        }
        const QString dest = home.filePath(sub + "/" + QFileInfo(t.path).fileName());
        if (QFileInfo(t.path).absoluteFilePath() == QFileInfo(dest).absoluteFilePath()) {
            t.path = dest;
            continue;
        }
        QFile::remove(dest);
        if (QFile::copy(t.path, dest)) {
            t.path = dest;
            ++copied;
        } else {
            missing << QFileInfo(t.path).fileName();
        }
    }
    const bool ok = saveCopy(onyPath);
    audioTracks_ = keep;   // 지금 열려 있는 프로젝트의 경로는 건드리지 않는다
    if (report) {
        if (!ok) *report = QStringLiteral("프로젝트를 저장하지 못했습니다");
        else {
            *report = QStringLiteral("소리 파일 %1개를 함께 넣었습니다").arg(copied);
            if (!missing.isEmpty())
                *report += QStringLiteral(". 찾지 못한 파일: ") + missing.join(", ");
        }
    }
    return ok;
}

bool Document::saveCopy(const QString& path) {
    const QString keepPath = path_;
    const bool keepMod = modified();
    const bool ok = save(path);
    path_ = keepPath;
    if (keepMod) setModified(true);
    return ok;
}

int Document::repeatKeys(int layer, int a, int b, int times, bool pingpong) {
    if (layer < 0 || layer >= layers_.size() || times < 1) return 0;
    a = std::clamp(a, 0, frameCount_ - 1);
    b = std::clamp(b, a, frameCount_ - 1);
    // 구간을 (이미지, 노출 길이) 목록으로 만든다. 구간 맨 앞이 홀드면 직전 키 이미지를 이어받는다.
    struct Seg { QImage img; int len; };
    QVector<Seg> segs;
    const auto& fr = layers_[layer].frames;
    int k0 = keyIndexAt(layer, a);
    if (k0 < 0) return 0;
    QVector<int> keyPos;
    keyPos << a;
    for (int f = a + 1; f <= b; ++f)
        if (!fr[f].isNull()) keyPos << f;
    for (int i = 0; i < keyPos.size(); ++i) {
        const int next = i + 1 < keyPos.size() ? keyPos[i + 1] : b + 1;
        segs.push_back({keyImage(layer, i == 0 ? k0 : keyPos[i]), next - keyPos[i]});
    }
    const int len = b - a + 1;
    const int needEnd = b + len * times;
    if (needEnd >= frameCount_) setFrameCount(needEnd + 1);
    auto& frames = layers_[layer].frames;
    int pos = b + 1;
    for (int t = 1; t <= times; ++t) {
        QVector<Seg> s = segs;
        if (pingpong && t % 2 == 1) std::reverse(s.begin(), s.end());
        // 첫 칸은 반드시 키 이미지여야 하므로 이전 구간의 끝과 같은 이미지라도 새 키로 둔다
        for (const Seg& g : s) {
            if (pos < frames.size()) frames[pos] = g.img;
            pos += g.len;
        }
    }
    undo_->clear();
    notifyStructure();
    setModified(true);
    return len * times;
}

void Document::stretchToFps(int newFps) {
    newFps = std::max(1, newFps);
    if (newFps == fps_) return;
    const double ratio = double(newFps) / fps_;
    auto mapF = [ratio](int f) { return int(f * ratio + 0.5); };
    const int newCount = std::max(1, mapF(frameCount_));
    for (auto& l : layers_) {
        QVector<QImage> nf(newCount);
        for (int f = 0; f < l.frames.size(); ++f) {
            if (l.frames[f].isNull()) continue;
            const int t = std::min(newCount - 1, mapF(f));
            if (nf[t].isNull()) nf[t] = l.frames[f]; // 같은 칸에 겹치면 앞의 이미지를 남긴다
        }
        // 이름/마크 위치도 함께 옮긴다
        QMap<int, QString> inst;
        for (auto it = l.instanceNames.cbegin(); it != l.instanceNames.cend(); ++it) inst[std::min(newCount - 1, mapF(it.key()))] = it.value();
        QMap<int, int> marks;
        for (auto it = l.imageMarks.cbegin(); it != l.imageMarks.cend(); ++it) marks[std::min(newCount - 1, mapF(it.key()))] = it.value();
        l.instanceNames = inst;
        l.imageMarks = marks;
        if (l.endFrame >= 0) l.endFrame = std::min(newCount, mapF(l.endFrame));
        l.frames = nf;
    }
    QMap<int, CameraKey> ck;
    for (auto it = cameraKeys_.cbegin(); it != cameraKeys_.cend(); ++it) ck[std::min(newCount - 1, mapF(it.key()))] = it.value();
    cameraKeys_ = ck;
    QSet<int> bm;
    for (int b : bookmarks_) bm.insert(std::min(newCount - 1, mapF(b)));
    bookmarks_ = bm;
    QMap<int, QString> nn;
    for (auto it = notes_.cbegin(); it != notes_.cend(); ++it) nn[std::min(newCount - 1, mapF(it.key()))] = it.value();
    notes_ = nn;
    for (AudioTrack& t : audioTracks_) t.offsetFrames = mapF(t.offsetFrames);
    frameCount_ = newCount;
    fps_ = newFps;
    curFrame_ = std::min(mapF(curFrame_), frameCount_ - 1);
    undo_->clear();
    setModified(true);
    notifyStructure();
    emit currentChanged();
}
