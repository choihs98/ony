@echo off
REM tvp-parity Windows 빌드 스크립트
REM 필요: Visual Studio 2022 Build Tools(C++), CMake, Python 3
REM 실행: "x64 Native Tools Command Prompt for VS 2022" 에서 이 파일을 실행
setlocal
set QTVER=6.8.1
if not exist Qt\%QTVER% (
  python -m pip install aqtinstall || goto :err
  python -m aqt install-qt windows desktop %QTVER% win64_msvc2022_64 -m qtmultimedia -O Qt || goto :err
)
set QTDIR=%CD%\Qt\%QTVER%\msvc2022_64
cmake -S . -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=%QTDIR% || goto :err
cmake --build build || goto :err
if not exist dist mkdir dist
copy build\ony.exe dist\ >nul
%QTDIR%\bin\windeployqt --release --no-translations dist\ony.exe || goto :err
REM 선택: Inno Setup 6/7 이 설치돼 있으면 설치 파일도 만든다
set ISCC=
for %%V in (7 6) do (
  if exist "%ProgramFiles(x86)%\Inno Setup %%V\ISCC.exe" if not defined ISCC set "ISCC=%ProgramFiles(x86)%\Inno Setup %%V\ISCC.exe"
  if exist "%ProgramFiles%\Inno Setup %%V\ISCC.exe" if not defined ISCC set "ISCC=%ProgramFiles%\Inno Setup %%V\ISCC.exe"
  if exist "%LocalAppData%\Programs\Inno Setup %%V\ISCC.exe" if not defined ISCC set "ISCC=%LocalAppData%\Programs\Inno Setup %%V\ISCC.exe"
)
if not defined ISCC for /f "delims=" %%I in ('where iscc 2^>nul') do if not defined ISCC set "ISCC=%%I"
if defined ISCC (
  "%ISCC%" installer\ony.iss && echo 설치 파일: release\Ony-Setup.exe
) else (
  echo Inno Setup 을 찾지 못했습니다. 설치 후 iscc installer\ony.iss 를 실행하세요.
)
echo.
echo 완료: dist\ony.exe 를 실행하세요.
exit /b 0
:err
echo 빌드 실패
exit /b 1
