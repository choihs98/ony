#pragma once
#include <QColor>
#include <QIcon>
#include <QString>

class QStyle;

// TVPaint 12 의 어두운 회색 화면을 닮은 테마와, 코드로 그리는 아이콘 모음.
namespace Style {
struct Scheme {
    QColor window, dark, base, text, muted, button, accent, canvasBg;
};
const Scheme& scheme(int index);
int schemeCount();
QString schemeName(int index);
void apply(int index);          // 팔레트 + 스타일시트를 앱 전체에 적용
QString styleSheet(const Scheme& s);

// 눌림/호버가 부드럽게 전환되는 버튼, 부드러운 휠 스크롤, 대화상자·메뉴 페이드를 가진 스타일
QStyle* createStyle();
void installMotion(); // QApplication 생성 뒤 한 번 호출

// 이름으로 아이콘을 만든다 (회색 선 아이콘). 없는 이름이면 빈 아이콘.
QIcon icon(const QString& name, int px = 24, const QColor& color = QColor(0xd2, 0xd2, 0xd2));
}
