# luahook — xLua 런타임 스크립트 교체 (학습용)

## 사용 순서
1. `build.bat` 실행 → `out\hook.dll`, `out\injector.exe` 생성
2. `out\rules.txt` 편집 (형식은 파일 안 주석 참고)
3. **게임을 켜기 전에** `out\injector.exe` 실행 (관리자 권한 권장)
4. 게임 실행 → `out\hook.log`에 `hook ready` 확인 (OTP 인증에 20~30초 걸려도 무관 — 스크립트는 월드 입장 때 로드됨)
5. `out\dump\` 에서 로드된 스크립트 확인 (.lua 평문 / .luac 바이트코드 → unluac 등으로 디컴파일) → 고친 평문 Lua를 파일로 저장 → rules.txt에 이름|@파일 규칙 → 게임 재시작 + 3번부터 반복

## 게임 업데이트 후 `SIG_FAIL`이 뜨면
`python tools\find_sig.py` 재실행 → 출력된 시그니처를 `hook.cpp`의 `LOADBUFFERX_SIG`에 붙이고 재빌드.

## 주의
넥슨 약관상 클라이언트 개조는 금지. 본인 학습/본인 월드 범위에서만.

## hook.log의 `=== process exit ===` 줄
ExitProcess로 끝날 때만 찍힌다. 이 게임은 정리 후 TerminateProcess로 종료하는 것으로 보여 정상 종료에도 안 찍힐 수 있다.
크래시 여부는 Unity `Player.log`(`%USERPROFILE%\AppData\LocalLow\Nexon\MapleStory Worlds\`) 끝에 `Input System ... Shutdown` + `Memory Statistics`가 있는지로 판단한다.
