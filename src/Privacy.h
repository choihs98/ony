#pragma once
class QWidget;

// 개인정보(익명 정보 전송) 동의. 첫 실행에 한 번 묻고, 도움말 > 개인정보 설정 에서 언제든 바꿀 수 있다.
namespace Privacy {
bool errorReportsAllowed(); // 오류 개선에 협조
bool usageStatsAllowed();   // 품질 향상(사용 현황)에 협조
void askIfNeeded(QWidget* parent); // 아직 선택한 적이 없을 때만 대화상자를 띄운다
void showDialog(QWidget* parent);
}
