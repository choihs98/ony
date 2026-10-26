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

## 동영상 내보내기
파일 > "동영상으로 내보내기" (Ctrl+Shift+E): MP4(H.264) / MOV(ProRes) / WebM(VP9) / GIF, 범위·화질·크기·소리 선택.
인코딩은 ffmpeg 를 사용합니다. 설치 파일(GitHub Actions 빌드)에는 ffmpeg.exe 가 들어 있고,
직접 빌드한 경우 ffmpeg.exe 를 ony.exe 와 같은 폴더에 두거나 PATH 에 추가하세요.
(포함되는 ffmpeg 빌드는 GPL 라이선스입니다. 외부 배포 시 라이선스를 확인하세요.)


## 단축키
편집 > 단축키 설정 (Ctrl+Alt+K) 에서 모든 명령의 키를 바꿀 수 있고, 설정은 자동 저장됩니다.
기본값: B 브러시, E 지우개, P 연필, L 선, R 사각형, O 원, S 스플라인, G 페인트통, M 선택/이동, W 리깅,
[ ] 브러시 크기, X A/B 색 바꾸기, Shift+P 색상 패널, Shift+B 브러시 패널, Ctrl+L 라이트 테이블, Ctrl+Ins 프레임 추가, F11 전체 화면.

## 로고
`resources/` 폴더에 `ony.png`(창/정보 아이콘)와 `ony.ico`(exe·설치 프로그램 아이콘)를 넣고 다시 빌드하면 적용됩니다. 자세한 내용은 `resources/README.txt`.

## 데모2
- 내보내기 통합 대화상자(영상/이미지 시퀀스), 프로젝트 설정(프레임 속도 변경 시 길이 유지)
- 재생: 왕복·속도, 카메라 속도 곡선, 라이트 테이블 고급, 플립 패널
- 환경설정: 저장(자동 백업·보관 수), 사운드, 시작(마지막 프로젝트·시작 룸)
- 단축키 대화상자 분류별 정렬

## 데모3
- FX 카테고리: 블러·색·왜곡·키잉·모션·렌더링·스타일화 (효과 메뉴 + FX 스택), 전환(디졸브·와이프·밀기)
- 브러시: 파선, 점선(촘촘/사각), 컷 브러시, 컷(사각)
- 드로잉 레코더(F9) + 획 애니메이션 (이미지 → 드로잉 레코더)
- 타임라인 확대/축소(Ctrl+휠, Ctrl+Alt+= / -), 레이어 접기(Ctrl+Alt+K)
- 퍼펫 레이어: 레이어 메뉴 → 퍼펫 레이어 (키·피벗·부모 연결)
- 렌더 카메라(프로젝트 메뉴), 인쇄/PDF(Ctrl+P), 퍼블리시(파일 메뉴)

## 데모3 이후
- 시작 로고 화면(스르륵 나타남 → 화면이 켜지며 로고가 사라짐). 끄려면 설정 `start/splash=false`.
- 획 그리기 최적화: 마우스를 움직일 때 바뀐 영역만 다시 합성 (결과는 이전과 픽셀 단위로 동일).
