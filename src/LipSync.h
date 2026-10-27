#pragma once
#include <QString>
#include <QVector>

// 입 모양(음소) 맞추기. Preston Blair 가 정리한 여덟 가지 입 모양을 쓴다.
// 대사를 글로 적으면 음소 열로 바꿔 주고, 각 음소에 이미지를 묶어 프레임에 깔아 준다.
namespace LipSync {

enum Phoneme { A = 0, B, C, D, E, F, G, X, Count };

// 입 모양의 이름 ("A", "B", …)
QString code(int p);
// 어떤 소리에 쓰는 입인지 (예: "A  ㅁ·ㅂ·ㅍ / M·B·P — 입을 다문다")
QString label(int p);

// 대사를 음소 열로 바꾼다. 한글은 자모를 풀어서 보고, 영문은 글자를 본다.
// 띄어쓰기·문장부호는 X(쉼) 가 된다.
QVector<int> fromText(const QString& text);

// 음소 열을 "ADBX…" 꼴의 글자로 (미리 보기용)
QString toCodes(const QVector<int>& seq);

}   // namespace LipSync
