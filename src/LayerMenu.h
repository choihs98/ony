#pragma once
#include <functional>

class QMenu;
class Document;

// 레이어 속성 메뉴 (프리/포스트 비헤이비어, 블렌딩 모드, 컬러 그룹, 스텐실, 접기 …).
// 레이어 메뉴와 타임라인 우클릭 메뉴가 같은 항목을 쓴다.
namespace LayerMenu {
void populate(QMenu* menu, Document* doc, const std::function<int()>& layerFn, const std::function<int()>& frameFn);
}
