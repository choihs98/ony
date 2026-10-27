#pragma once
#include <QByteArray>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QVector>

// 포토샵 PSD 파일 읽기/쓰기 (8비트 RGB/RGBA, 압축 없음 또는 RLE).
// 16/32비트, CMYK 같은 형식은 읽지 않고 이유를 돌려준다.
namespace Psd {

struct LayerData {
    QString name;
    QImage image;      // 캔버스 크기, ARGB32_Premultiplied
    int opacity = 255; // 0~255
    bool visible = true;
    QByteArray blend = "norm";   // PSD 블렌딩 키 (4글자)
};

// ONY 블렌딩 모드 번호 <-> PSD 블렌딩 키
QByteArray blendKeyFromOny(int onyMode);
int onyModeFromBlendKey(const QByteArray& key);

struct File {
    QSize size;
    QVector<LayerData> layers;   // 아래에서 위 순서
    QImage merged;               // 합쳐진 그림 (레이어가 없을 때 쓴다)
};

// 성공하면 빈 문자열, 실패하면 오류 메시지
QString read(const QString& path, File* out);
QString write(const QString& path, const QSize& size, const QVector<LayerData>& layers, const QImage& merged);

}
