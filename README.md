# Ony

키 이미지(원화) 중심의 2D 프레임 애니메이션 도구 (C++20 / Qt 6 / CMake). 프로젝트 파일 확장자: `.ony`

## 실행
- Windows: `build_windows.bat` (VS 2022 x64 Native Tools 프롬프트) → `dist\ony.exe`
- Linux:   `sudo apt install qt6-base-dev qt6-multimedia-dev cmake g++` →
           `cmake -S . -B build && cmake --build build -j && ./build/ony`
- 자체 테스트(Linux): `QT_QPA_PLATFORM=offscreen ./build/selftest`

## 처음 써보기
1. 파일 > 새 프로젝트 → 왼쪽 메인 패널에서 획 도구 → 캔버스에 그리기
2. 타임라인에서 프레임 클릭 → 다른 프레임에 그리기 (이미지가 없으면 직전 이미지를 홀드)
3. Enter 로 재생, L 로 라이트 테이블, 스페이스 누른 채 드래그로 화면 이동

이전 이름(`.tvpx`)으로 저장한 파일도 열 수 있습니다.

## 다른 PC에 설치 파일로 배포 (서명 없음)
- GitHub 에 올린 뒤 Actions > "Windows 설치 파일 빌드" > Run workflow → 끝나면 Artifacts 의 `Ony-Setup` 다운로드
  (안에 `Ony-Setup.exe`). 또는 PC 에 Inno Setup 6 이 있으면 `build_windows.bat` 이 `release\Ony-Setup.exe` 도 만듭니다.
- 서명이 없어서 처음 실행 때 Windows SmartScreen 이 경고합니다: "추가 정보" → "실행".
