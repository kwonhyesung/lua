// injector.cpp — 대상 exe가 뜨길 기다렸다가 hook.dll을 LoadLibraryW 원격 스레드로 주입한다.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <string>
#include <io.h>
#include <fcntl.h>
#include <conio.h>
#include <cstdarg>

static DWORD find_pid(const wchar_t* name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W e{ sizeof e };
    DWORD pid = 0;
    if (Process32FirstW(snap, &e))
        do { if (_wcsicmp(e.szExeFile, name) == 0) { pid = e.th32ProcessID; break; } } while (Process32NextW(snap, &e));
    CloseHandle(snap);
    return pid;
}

static int fail(const wchar_t* msg) {
    wprintf(L"%s (오류 %lu)\n아무 키나 누르면 닫힙니다.", msg, GetLastError());
    _getwch();
    return 1;
}

// injector.exe는 콘솔창이 닫히면 사라지므로, 실패 원인을 파일에도 남겨 나중에 확인할 수 있게 한다.
static std::wstring g_logPath;
static void ilog(const wchar_t* fmt, ...) {
    FILE* f = _wfopen(g_logPath.c_str(), L"a");
    if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t);
    fwprintf(f, L"[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list ap; va_start(ap, fmt); vfwprintf(f, fmt, ap); va_end(ap);
    fputwc(L'\n', f);
    fclose(f);
}

int wmain(int argc, wchar_t** argv) {
    _setmode(_fileno(stdout), _O_U16TEXT);

    const wchar_t* exe = argc > 1 ? argv[1] : L"msw.exe";
    std::wstring dll;
    if (argc > 2) {
        dll = argv[2];
        wchar_t buf[MAX_PATH];
        if (GetFullPathNameW(dll.c_str(), MAX_PATH, buf, nullptr)) dll = buf;
    }
    else {
        wchar_t p[MAX_PATH]; GetModuleFileNameW(nullptr, p, MAX_PATH);
        dll = p; dll.erase(dll.find_last_of(L"\\/") + 1); dll += L"hook.dll";
    }
    if (GetFileAttributesW(dll.c_str()) == INVALID_FILE_ATTRIBUTES) return fail(L"hook.dll을 찾을 수 없습니다");
    { std::wstring d = dll; d.erase(d.find_last_of(L"\\/") + 1); g_logPath = d + L"injector.log"; }
    ilog(L"=== injector start, dll=%s ===", dll.c_str());

    // hook.dll을 %TEMP%에 8자리 랜덤 이름으로 복사 → 원본은 게임 실행 중에도 재빌드 가능.
    wchar_t tmpdir[MAX_PATH]; GetTempPathW(MAX_PATH, tmpdir);
    srand((unsigned)(GetTickCount64() ^ GetCurrentProcessId()));
    wchar_t rnd[9];
    for (int i = 0; i < 8; ++i) rnd[i] = L"0123456789abcdef"[rand() & 0xF];
    rnd[8] = 0;
    std::wstring copy = std::wstring(tmpdir) + rnd + L".dll";
    if (!CopyFileW(dll.c_str(), copy.c_str(), FALSE)) { ilog(L"COPY_FAIL err=%lu", GetLastError()); return fail(L"임시 DLL 복사 실패"); }

    // hook.dll은 이 사본(%TEMP%) 경로로 로드되므로 자기 모듈 경로로는 원래 프로젝트 폴더(rules.txt,
    // out\ 등이 있는 곳)를 알 수 없다. 사본 옆에 "<사본이름>.dir" 파일로 원래 폴더를 적어두면
    // hook.cpp가 그걸 읽어서 어느 PC/폴더에 있든 항상 실제 프로젝트 폴더를 찾아간다.
    {
        std::wstring origDir = dll; origDir.erase(origDir.find_last_of(L"\\/") + 1);
        if (!origDir.empty() && (origDir.back() == L'\\' || origDir.back() == L'/')) origDir.pop_back();
        FILE* mf = _wfopen((copy + L".dir").c_str(), L"wb");
        if (mf) {
            int n = WideCharToMultiByte(CP_UTF8, 0, origDir.c_str(), -1, nullptr, 0, nullptr, nullptr);
            std::string narrow(n ? n - 1 : 0, '\0');
            if (n) WideCharToMultiByte(CP_UTF8, 0, origDir.c_str(), -1, &narrow[0], n, nullptr, nullptr);
            fwrite(narrow.data(), 1, narrow.size(), mf);
            fclose(mf);
        }
    }

    dll = copy;   // 게임엔 사본을 주입
    wprintf(L"임시 DLL: %s\n", dll.c_str());

    wprintf(L"%s 대기 중... (게임을 실행하세요)\n", exe);
    DWORD pid;
    while (!(pid = find_pid(exe))) Sleep(500);
    wprintf(L"pid %lu 발견. 2초 후 주입합니다.\n", pid);
    Sleep(2000);

    HANDLE h = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!h) { ilog(L"OPENPROCESS_FAIL err=%lu", GetLastError()); return fail(L"OpenProcess 실패. 관리자 권한으로 실행하세요"); }

    SIZE_T bytes = (dll.size() + 1) * sizeof(wchar_t);
    void* mem = VirtualAllocEx(h, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!mem || !WriteProcessMemory(h, mem, dll.c_str(), bytes, nullptr)) { ilog(L"VALLOC_OR_WPM_FAIL err=%lu", GetLastError()); return fail(L"메모리 쓰기 실패"); }

    auto loadlib = (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    HANDLE t = CreateRemoteThread(h, nullptr, 0, loadlib, mem, 0, nullptr);
    if (!t) { ilog(L"CRT_FAIL err=%lu", GetLastError()); return fail(L"CreateRemoteThread 실패"); }
    WaitForSingleObject(t, INFINITE);
    DWORD rc = 0; GetExitCodeThread(t, &rc);
    VirtualFreeEx(h, mem, 0, MEM_RELEASE);
    CloseHandle(t);

    if (!rc) { ilog(L"LOADLIBRARY_FAIL exitcode=0"); CloseHandle(h); DeleteFileW(dll.c_str()); DeleteFileW((dll + L".dir").c_str()); return fail(L"대상 안에서 LoadLibrary 실패 (hook.dll 비트/의존성 확인)"); }
    ilog(L"INJECT_OK %s", dll.c_str());
    wprintf(L"주입 완료. out\\hook.log를 확인하세요.\n30초 후 이 창은 자동으로 닫힙니다 (훅은 게임 프로세스 안에서 계속 동작).\n");
    // hook.dll은 게임에 로드된 채로 계속 살아있으므로(자가언로드 안 함) 지금은 못 지운다 —
    // 시도만 해보고 실패(파일 사용 중)해도 그냥 넘어간다. 게임 종료 후 %TEMP%에 파일이 남아있어도 무해.
    // .dir 마커는 worker 스레드가 비동기로 읽으므로 곧바로 지우면 레이스가 생길 수 있어
    // 30초 대기 뒤(이미 다 읽었을 시점)에 같이 정리한다.
    Sleep(30000);
    CloseHandle(h);
    DeleteFileW(dll.c_str());
    DeleteFileW((dll + L".dir").c_str());
    return 0;
}
