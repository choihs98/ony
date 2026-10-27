#include "Psd.h"

#include <QBuffer>
#include <QDataStream>
#include <QDebug>
#include <QHash>
#include <QFile>
#include <QPainter>
#include <algorithm>

namespace Psd {
namespace {

// ---------------- 읽기 도우미 ----------------
struct Reader {
    QDataStream ds;
    explicit Reader(QIODevice* d) : ds(d) { ds.setByteOrder(QDataStream::BigEndian); }
    quint8 u8() { quint8 v = 0; ds >> v; return v; }
    quint16 u16() { quint16 v = 0; ds >> v; return v; }
    qint16 i16() { qint16 v = 0; ds >> v; return v; }
    quint32 u32() { quint32 v = 0; ds >> v; return v; }
    qint32 i32() { qint32 v = 0; ds >> v; return v; }
    bool ok() const { return ds.status() == QDataStream::Ok; }
    void skip(qint64 n) { ds.skipRawData(int(n)); }
    QByteArray raw(int n) {
        QByteArray b(n, 0);
        const int got = ds.readRawData(b.data(), n);
        if (got != n) b.resize(std::max(0, got));
        return b;
    }
};

struct Writer {
    QDataStream ds;
    explicit Writer(QIODevice* d) : ds(d) { ds.setByteOrder(QDataStream::BigEndian); }
    void u8(quint8 v) { ds << v; }
    void u16(quint16 v) { ds << v; }
    void i16(qint16 v) { ds << v; }
    void u32(quint32 v) { ds << v; }
    void i32(qint32 v) { ds << v; }
    void raw(const QByteArray& b) { ds.writeRawData(b.constData(), b.size()); }
};

// PackBits 풀기
QByteArray rleDecode(const QByteArray& in, int expected) {
    QByteArray out;
    out.reserve(expected);
    int i = 0;
    while (i < in.size() && out.size() < expected) {
        const qint8 n = qint8(in[i++]);
        if (n >= 0) {
            const int cnt = n + 1;
            if (i + cnt > in.size()) break;
            out.append(in.constData() + i, cnt);
            i += cnt;
        } else if (n != -128) {
            const int cnt = 1 - n;
            if (i >= in.size()) break;
            out.append(QByteArray(cnt, in[i]));
            ++i;
        }
    }
    out.resize(expected);
    return out;
}

// PackBits 묶기 (한 줄씩)
QByteArray rleEncodeRow(const uchar* p, int n) {
    QByteArray out;
    int i = 0;
    while (i < n) {
        int run = 1;
        while (i + run < n && run < 128 && p[i + run] == p[i]) ++run;
        if (run >= 2) {
            out.append(char(qint8(1 - run)));
            out.append(char(p[i]));
            i += run;
        } else {
            int lit = 1;
            while (i + lit < n && lit < 128 && !(i + lit + 1 < n && p[i + lit] == p[i + lit + 1] && p[i + lit] == p[i + lit + 2]))
                ++lit;
            out.append(char(qint8(lit - 1)));
            out.append(reinterpret_cast<const char*>(p + i), lit);
            i += lit;
        }
    }
    return out;
}

// 한 채널을 바이트 배열로 뽑는다 (0 R, 1 G, 2 B, 3 A)
QByteArray channelBytes(const QImage& img, int ch) {
    const QImage s = img.convertToFormat(QImage::Format_ARGB32);
    QByteArray b(s.width() * s.height(), 0);
    uchar* d = reinterpret_cast<uchar*>(b.data());
    for (int y = 0; y < s.height(); ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(s.constScanLine(y));
        for (int x = 0; x < s.width(); ++x) {
            const QRgb c = row[x];
            *d++ = ch == 0 ? qRed(c) : ch == 1 ? qGreen(c) : ch == 2 ? qBlue(c) : qAlpha(c);
        }
    }
    return b;
}

QByteArray pascalPad4(const QString& s) {
    QByteArray n = s.toUtf8().left(255);
    QByteArray out;
    out.append(char(n.size()));
    out.append(n);
    while (out.size() % 4) out.append('\0');
    return out;
}

} // namespace

// ---------------- 블렌딩 모드 표 ----------------
// ONY 의 Blend::Mode 순서: Color, Behind, Erase, Shade, Light, Colorize, Tint, Saturate, Value, Add, Sub,
// Multiply, Screen, Replace, Substitute, Difference, Divide, Overlay1, Overlay2, Dodge, Burn, HardLight,
// SoftLight, GrainExtract, GrainMerge, Subtract, DarkenOnly, LightenOnly
namespace {
struct BlendPair { int ony; const char* key; };
const BlendPair kBlendTable[] = {
    {0, "norm"},   {1, "norm"},   {2, "norm"},   {3, "dark"},   {4, "lite"},   {5, "colr"},
    {6, "hue "},   {7, "sat "},   {8, "lum "},   {9, "lddg"},   {10, "fsub"},  {11, "mul "},
    {12, "scrn"},  {13, "norm"},  {14, "norm"},  {15, "diff"},  {16, "fdiv"},  {17, "over"},
    {18, "over"},  {19, "div "},  {20, "idiv"},  {21, "hLit"},  {22, "sLit"},  {23, "fsub"},
    {24, "lddg"},  {25, "fsub"},  {26, "dark"},  {27, "lite"},
};
}

QByteArray blendKeyFromOny(int onyMode) {
    for (const BlendPair& p : kBlendTable)
        if (p.ony == onyMode) return QByteArray(p.key);
    return QByteArray("norm");
}

int onyModeFromBlendKey(const QByteArray& key) {
    for (const BlendPair& p : kBlendTable)
        if (key == QByteArray(p.key)) return p.ony;
    return 0;
}

// ---------------------------------------------------------------- 읽기
QString read(const QString& path, File* out) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QStringLiteral("파일을 열 수 없습니다.");
    Reader r(&f);
    if (r.raw(4) != QByteArray("8BPS")) return QStringLiteral("PSD 파일이 아닙니다.");
    if (r.u16() != 1) return QStringLiteral("지원하지 않는 PSD 판입니다 (PSB 는 읽지 못합니다).");
    r.skip(6);
    const int channels = r.u16();
    const int h = int(r.u32()), w = int(r.u32());
    const int depth = r.u16();
    const int mode = r.u16();
    if (w <= 0 || h <= 0 || w > 30000 || h > 30000) return QStringLiteral("크기를 읽지 못했습니다.");
    if (depth != 8) return QString("8비트 PSD 만 읽을 수 있습니다 (이 파일은 %1비트).").arg(depth);
    if (mode != 3 && mode != 1) return QStringLiteral("RGB 또는 회색조 PSD 만 읽을 수 있습니다 (CMYK 등은 포토샵에서 RGB 로 바꿔 주세요).");
    out->size = QSize(w, h);

    r.skip(r.u32());                 // 색상 모드 데이터
    r.skip(r.u32());                 // 이미지 리소스

    const quint32 layerMaskLen = r.u32();
    const qint64 layerMaskEnd = f.pos() + layerMaskLen;
    if (layerMaskLen > 0) {
        const quint32 layerInfoLen = r.u32();
        const qint64 layerInfoEnd = f.pos() + layerInfoLen;
        if (layerInfoLen > 0) {
            qint16 count = r.i16();
            const bool firstAlpha = count < 0;
            Q_UNUSED(firstAlpha);
            int n = std::abs(int(count));
            if (n > 1000) n = 1000;
            struct Rec {
                int top = 0, left = 0, bottom = 0, right = 0;
                QVector<QPair<int, qint64>> chans;   // (채널 id, 길이)
                int opacity = 255;
                bool visible = true;
                QString name;
                QByteArray blend = "norm";
            };
            QVector<Rec> recs;
            for (int i = 0; i < n && r.ok(); ++i) {
                Rec rec;
                rec.top = r.i32();
                rec.left = r.i32();
                rec.bottom = r.i32();
                rec.right = r.i32();
                const int nch = r.u16();
                if (nch > 64) return QStringLiteral("레이어 정보를 읽지 못했습니다.");
                for (int c = 0; c < nch; ++c) {
                    const qint16 id = r.i16();
                    const quint32 len = r.u32();
                    rec.chans.push_back({int(id), qint64(len)});
                }
                r.skip(4);                      // 블렌드 서명
                rec.blend = r.raw(4);           // 블렌드 키
                rec.opacity = r.u8();
                r.skip(1);                      // clipping
                const quint8 flags = r.u8();
                rec.visible = !(flags & 0x02);
                r.skip(1);                      // filler
                const quint32 extraLen = r.u32();
                const qint64 extraEnd = f.pos() + extraLen;
                r.skip(r.u32());                // 레이어 마스크
                r.skip(r.u32());                // 블렌딩 범위
                const int nameLen = r.u8();
                QByteArray nb = r.raw(nameLen);
                int pad = (nameLen + 1) % 4;
                if (pad) r.skip(4 - pad);
                rec.name = QString::fromUtf8(nb);
                // 유니코드 이름(luni)을 찾아본다
                while (f.pos() + 12 <= extraEnd && r.ok()) {
                    const QByteArray sig = r.raw(4);
                    if (sig != QByteArray("8BIM") && sig != QByteArray("8B64")) break;
                    const QByteArray key = r.raw(4);
                    const quint32 len = r.u32();
                    const qint64 next = f.pos() + len + (len % 2);
                    if (key == QByteArray("luni")) {
                        const quint32 cnt = r.u32();
                        QString s;
                        for (quint32 k = 0; k < cnt && k < 1024; ++k) s.append(QChar(r.u16()));
                        if (!s.isEmpty()) rec.name = s;
                    }
                    f.seek(std::min(next, extraEnd));
                }
                f.seek(extraEnd);
                recs.push_back(rec);
            }
            // 채널 데이터
            for (const Rec& rec : recs) {
                const int lw = rec.right - rec.left, lh = rec.bottom - rec.top;
                QImage li(out->size, QImage::Format_ARGB32);
                li.fill(Qt::transparent);
                if (lw > 0 && lh > 0 && lw <= 30000 && lh <= 30000) {
                    QHash<int, QByteArray> planes;
                    for (const auto& ch : rec.chans) {
                        const qint64 end = f.pos() + ch.second;
                        const quint16 comp = r.u16();
                        QByteArray data;
                        if (comp == 0) {
                            data = r.raw(lw * lh);
                        } else if (comp == 1) {
                            QVector<int> lens(lh);
                            for (int y = 0; y < lh; ++y) lens[y] = r.u16();
                            for (int y = 0; y < lh; ++y) data += rleDecode(r.raw(lens[y]), lw);
                        }
                        if (data.size() == lw * lh) planes[ch.first] = data;
                        f.seek(end);
                    }
                    const QByteArray R = planes.value(0);
                    // 회색조 파일은 채널이 하나뿐이라 R 을 그대로 쓴다
                    const QByteArray G = planes.contains(1) ? planes.value(1) : planes.value(0);
                    const QByteArray B = planes.contains(2) ? planes.value(2) : planes.value(0);
                    if (qEnvironmentVariableIsSet("ONY_PSD_DEBUG"))
                        qDebug() << "layer" << rec.name << lw << lh << "planes" << planes.keys() << R.size() << G.size() << B.size();
                    const QByteArray A = planes.value(-1);
                    if (R.size() == lw * lh) {
                        for (int y = 0; y < lh; ++y) {
                            const int iy = rec.top + y;
                            if (iy < 0 || iy >= out->size.height()) continue;
                            QRgb* dst = reinterpret_cast<QRgb*>(li.scanLine(iy));
                            for (int x = 0; x < lw; ++x) {
                                const int ix = rec.left + x;
                                if (ix < 0 || ix >= out->size.width()) continue;
                                const int o = y * lw + x;
                                const int a = A.size() == lw * lh ? uchar(A[o]) : 255;
                                dst[ix] = qRgba(uchar(R[o]), G.size() == lw * lh ? uchar(G[o]) : uchar(R[o]),
                                                B.size() == lw * lh ? uchar(B[o]) : uchar(R[o]), a);
                            }
                        }
                    }
                } else {
                    for (const auto& ch : rec.chans) r.skip(ch.second);
                }
                LayerData ld;
                ld.name = rec.name.isEmpty() ? QStringLiteral("레이어") : rec.name;
                ld.opacity = rec.opacity;
                ld.visible = rec.visible;
                ld.blend = rec.blend.isEmpty() ? QByteArray("norm") : rec.blend;
                ld.image = li.convertToFormat(QImage::Format_ARGB32_Premultiplied);
                out->layers.push_back(ld);
            }
        }
        f.seek(layerInfoEnd > f.pos() ? layerInfoEnd : f.pos());
    }
    f.seek(std::max(layerMaskEnd, f.pos()));

    // 합쳐진 그림
    {
        const quint16 comp = r.u16();
        const int total = w * h;
        QVector<QByteArray> planes;
        if (comp == 0) {
            for (int c = 0; c < channels; ++c) planes.push_back(r.raw(total));
        } else if (comp == 1) {
            QVector<int> lens(h * channels);
            for (int i = 0; i < h * channels; ++i) lens[i] = r.u16();
            for (int c = 0; c < channels; ++c) {
                QByteArray plane;
                for (int y = 0; y < h; ++y) plane += rleDecode(r.raw(lens[c * h + y]), w);
                planes.push_back(plane);
            }
        }
        if (planes.size() >= 1 && planes[0].size() == total) {
            QImage m(w, h, QImage::Format_ARGB32);
            const bool gray = planes.size() < 3 || planes[1].size() != total || planes[2].size() != total;
            for (int y = 0; y < h; ++y) {
                QRgb* d = reinterpret_cast<QRgb*>(m.scanLine(y));
                for (int x = 0; x < w; ++x) {
                    const int o = y * w + x;
                    const int R = uchar(planes[0][o]);
                    const int G = gray ? R : uchar(planes[1][o]);
                    const int B = gray ? R : uchar(planes[2][o]);
                    const int A = (!gray && planes.size() >= 4 && planes[3].size() == total) ? uchar(planes[3][o]) : 255;
                    d[x] = qRgba(R, G, B, A);
                }
            }
            out->merged = m.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        }
    }
    if (out->layers.isEmpty() && out->merged.isNull()) return QStringLiteral("그림을 읽지 못했습니다.");
    return {};
}

// ---------------------------------------------------------------- 쓰기
QString write(const QString& path, const QSize& size, const QVector<LayerData>& layers, const QImage& merged) {
    if (size.width() < 1 || size.height() < 1) return QStringLiteral("크기가 올바르지 않습니다.");
    if (size.width() > 30000 || size.height() > 30000) return QStringLiteral("PSD 는 30000 픽셀까지만 됩니다.");
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return QStringLiteral("파일을 만들 수 없습니다.");
    Writer wr(&f);
    const int w = size.width(), h = size.height();

    wr.raw(QByteArray("8BPS"));
    wr.u16(1);
    wr.raw(QByteArray(6, '\0'));
    wr.u16(4);          // 채널 수 (RGBA)
    wr.u32(h);
    wr.u32(w);
    wr.u16(8);          // 8비트
    wr.u16(3);          // RGB
    wr.u32(0);          // 색상 모드 데이터 없음
    wr.u32(0);          // 이미지 리소스 없음

    // ---- 레이어 구역을 메모리에 먼저 만든다 ----
    QByteArray layerInfo;
    if (!layers.isEmpty()) {
        QBuffer buf(&layerInfo);
        buf.open(QIODevice::WriteOnly);
        Writer lw(&buf);
        lw.i16(qint16(layers.size()));
        QVector<QVector<QByteArray>> encoded;   // [레이어][채널] RLE 데이터
        for (const LayerData& ld : layers) {
            QVector<QByteArray> chans;
            for (int ch : {3, 0, 1, 2}) {       // A, R, G, B
                const QByteArray plain = channelBytes(ld.image, ch);
                QByteArray rows, body;
                for (int y = 0; y < h; ++y) {
                    const QByteArray e = rleEncodeRow(reinterpret_cast<const uchar*>(plain.constData()) + y * w, w);
                    QByteArray len(2, 0);
                    len[0] = char((e.size() >> 8) & 0xff);
                    len[1] = char(e.size() & 0xff);
                    rows += len;
                    body += e;
                }
                QByteArray out;
                out.append(char(0));
                out.append(char(1));            // 압축 = RLE
                out += rows;
                out += body;
                chans.push_back(out);
            }
            encoded.push_back(chans);
        }
        for (int i = 0; i < layers.size(); ++i) {
            const LayerData& ld = layers[i];
            lw.i32(0);
            lw.i32(0);
            lw.i32(h);
            lw.i32(w);
            lw.u16(4);
            const qint16 ids[4] = {-1, 0, 1, 2};
            for (int c = 0; c < 4; ++c) {
                lw.i16(ids[c]);
                lw.u32(quint32(encoded[i][c].size()));
            }
            lw.raw(QByteArray("8BIM"));
            lw.raw(ld.blend.size() == 4 ? ld.blend : QByteArray("norm"));
            lw.u8(quint8(std::clamp(ld.opacity, 0, 255)));
            lw.u8(0);                                   // clipping
            lw.u8(ld.visible ? 0 : 0x02);               // flags
            lw.u8(0);                                   // filler
            // extra: 마스크(0) + 블렌딩 범위(0) + 이름 + 유니코드 이름(luni)
            const QByteArray nm = pascalPad4(ld.name);
            QByteArray luni;
            {
                QBuffer lb(&luni);
                lb.open(QIODevice::WriteOnly);
                Writer uw(&lb);
                const QString s = ld.name.left(255);
                uw.u32(quint32(s.size()));
                for (QChar ch : s) uw.u16(ch.unicode());
                lb.close();
            }
            QByteArray luniBlock;
            {
                QBuffer lb(&luniBlock);
                lb.open(QIODevice::WriteOnly);
                Writer uw(&lb);
                uw.raw(QByteArray("8BIM"));
                uw.raw(QByteArray("luni"));
                uw.u32(quint32(luni.size()));
                uw.raw(luni);
                if (luni.size() % 2) uw.u8(0);
                lb.close();
            }
            lw.u32(quint32(4 + 4 + nm.size() + luniBlock.size()));
            lw.u32(0);
            lw.u32(0);
            lw.raw(nm);
            lw.raw(luniBlock);
        }
        for (int i = 0; i < layers.size(); ++i)
            for (int c = 0; c < 4; ++c) lw.raw(encoded[i][c]);
        buf.close();
        if (layerInfo.size() % 2) layerInfo.append('\0');
    }
    const quint32 layerInfoLen = quint32(layerInfo.size());
    const quint32 layerMaskLen = layerInfo.isEmpty() ? 0 : quint32(4 + layerInfoLen + 4);
    wr.u32(layerMaskLen);
    if (!layerInfo.isEmpty()) {
        wr.u32(layerInfoLen);
        wr.raw(layerInfo);
        wr.u32(0);   // 전역 레이어 마스크 없음
    }

    // ---- 합쳐진 그림 (RLE) ----
    QImage flat = merged;
    if (flat.isNull()) {
        flat = QImage(size, QImage::Format_ARGB32_Premultiplied);
        flat.fill(Qt::transparent);
        QPainter p(&flat);
        for (const LayerData& ld : layers) {
            if (!ld.visible) continue;
            p.setOpacity(std::clamp(ld.opacity, 0, 255) / 255.0);
            p.drawImage(0, 0, ld.image);
        }
    }
    wr.u16(1);   // RLE
    QByteArray rows, body;
    for (int ch : {0, 1, 2, 3}) {
        const QByteArray plain = channelBytes(flat, ch);
        for (int y = 0; y < h; ++y) {
            const QByteArray e = rleEncodeRow(reinterpret_cast<const uchar*>(plain.constData()) + y * w, w);
            QByteArray len(2, 0);
            len[0] = char((e.size() >> 8) & 0xff);
            len[1] = char(e.size() & 0xff);
            rows += len;
            body += e;
        }
    }
    wr.raw(rows);
    wr.raw(body);
    f.close();
    return {};
}

} // namespace Psd
