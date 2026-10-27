#include "LipSync.h"
#include <QChar>

namespace LipSync {

QString code(int p) {
    static const char* c[Count] = {"A", "B", "C", "D", "E", "F", "G", "X"};
    return (p >= 0 && p < Count) ? QString::fromLatin1(c[p]) : QStringLiteral("X");
}

QString label(int p) {
    switch (p) {
    case A: return QStringLiteral("A   ㅁ·ㅂ·ㅍ / M·B·P — 입을 다문다");
    case B: return QStringLiteral("B   ㅅ·ㅈ·ㄷ·ㄴ·ㄹ·ㅣ / S·T·D·N·L·I — 살짝 벌린다");
    case C: return QStringLiteral("C   ㅔ·ㅐ·ㅓ / E·EH — 옆으로 벌린다");
    case D: return QStringLiteral("D   ㅏ·ㅑ / A·AH — 크게 벌린다");
    case E: return QStringLiteral("E   ㅗ·ㅛ / O — 동그랗게");
    case F: return QStringLiteral("F   ㅜ·ㅠ·ㅡ / U·W·OO — 오므린다");
    case G: return QStringLiteral("G   ㅍ·ㅎ·ㅸ / F·V — 아랫입술을 문다");
    default: return QStringLiteral("X   쉼 — 다문 기본 입");
    }
}

QString toCodes(const QVector<int>& seq) {
    QString s;
    for (int p : seq) s += code(p);
    return s;
}

namespace {

// 한글 낱자(초성/중성/종성) 를 입 모양으로
int fromJung(int jung) {
    // 0 ㅏ 1 ㅐ 2 ㅑ 3 ㅒ 4 ㅓ 5 ㅔ 6 ㅕ 7 ㅖ 8 ㅗ 9 ㅘ 10 ㅙ 11 ㅚ 12 ㅛ
    // 13 ㅜ 14 ㅝ 15 ㅞ 16 ㅟ 17 ㅠ 18 ㅡ 19 ㅢ 20 ㅣ
    switch (jung) {
    case 0: case 2: return D;                       // ㅏ ㅑ
    case 1: case 3: case 4: case 5: case 6: case 7: return C;   // ㅐ ㅒ ㅓ ㅔ ㅕ ㅖ
    case 8: case 9: case 10: case 11: case 12: return E;        // ㅗ ㅘ ㅙ ㅚ ㅛ
    case 13: case 14: case 15: case 16: case 17: case 18: return F;  // ㅜ ㅝ ㅞ ㅟ ㅠ ㅡ
    case 19: case 20: return B;                     // ㅢ ㅣ
    default: return C;
    }
}

int fromCho(int cho) {
    // 0 ㄱ 1 ㄲ 2 ㄴ 3 ㄷ 4 ㄸ 5 ㄹ 6 ㅁ 7 ㅂ 8 ㅃ 9 ㅅ 10 ㅆ 11 ㅇ
    // 12 ㅈ 13 ㅉ 14 ㅊ 15 ㅋ 16 ㅌ 17 ㅍ 18 ㅎ
    switch (cho) {
    case 6: case 7: case 8: return A;     // ㅁ ㅂ ㅃ — 다문다
    case 17: case 18: return G;           // ㅍ ㅎ — 입술/이
    case 11: return -1;                   // ㅇ 은 소리가 없으니 모음에 맡긴다
    default: return B;                    // 나머지 자음
    }
}

// 자모만 따로 쓴 경우 (ㄱ~ㅎ, ㅏ~ㅣ)
int fromJamo(ushort u) {
    if (u >= 0x3131 && u <= 0x314E) {   // ㄱ…ㅎ
        switch (u) {
        case 0x3141: case 0x3142: case 0x3143: return A;   // ㅁ ㅂ ㅃ
        case 0x314D: case 0x314E: return G;                // ㅍ ㅎ
        case 0x3147: return X;                             // ㅇ
        default: return B;
        }
    }
    if (u >= 0x314F && u <= 0x3163) return fromJung(u - 0x314F);
    return -1;
}

int fromLatin(QChar ch) {
    switch (ch.toUpper().unicode()) {
    case 'M': case 'B': case 'P': return A;
    case 'S': case 'T': case 'D': case 'N': case 'L': case 'R':
    case 'C': case 'G': case 'K': case 'J': case 'X': case 'Z':
    case 'I': case 'Y': case 'H': return B;
    case 'E': return C;
    case 'A': return D;
    case 'O': return E;
    case 'U': case 'W': case 'Q': return F;
    case 'F': case 'V': return G;
    default: return -1;
    }
}

}   // namespace

QVector<int> fromText(const QString& text) {
    QVector<int> out;
    auto push = [&out](int p) {
        if (p < 0 || p >= Count) return;
        // 같은 입 모양이 세 번 넘게 이어지면 지루하니 두 번까지만 둔다
        if (out.size() >= 2 && out[out.size() - 1] == p && out[out.size() - 2] == p) return;
        out.push_back(p);
    };
    for (QChar ch : text) {
        const ushort u = ch.unicode();
        if (ch.isSpace() || ch.isPunct()) {
            if (!out.isEmpty() && out.last() != X) out.push_back(X);
            continue;
        }
        if (u >= 0xAC00 && u <= 0xD7A3) {           // 완성형 한글
            const int s = u - 0xAC00;
            const int cho = s / (21 * 28), jung = (s % (21 * 28)) / 28, jong = s % 28;
            const int c = fromCho(cho);
            if (c >= 0) push(c);
            push(fromJung(jung));
            // 받침 ㅁ·ㅂ·ㅍ 는 입을 다물게 된다
            if (jong == 16 || jong == 17 || jong == 26) push(A);
            continue;
        }
        const int j = fromJamo(u);
        if (j >= 0) { push(j); continue; }
        if (ch.isLetter()) {
            const int l = fromLatin(ch);
            if (l >= 0) push(l);
            continue;
        }
        if (ch.isDigit()) push(C);
    }
    while (!out.isEmpty() && out.last() == X) out.removeLast();
    return out;
}

}   // namespace LipSync
