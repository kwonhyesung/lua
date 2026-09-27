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
#pragma comment(lib, "user32.lib")

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

// hook.dll을 %TEMP%에 랜덤 이름으로 복사하고, 원본 폴더를 적은 .dir 마커를 같이 남긴 뒤
// pid에 LoadLibraryW 원격 스레드로 주입한다. 실패해도 false만 반환하고 종료하지 않는다
// (Insert 재주입 루프에서 한 번 실패했다고 프로그램 전체가 죽으면 안 되므로).
static bool inject_into(DWORD pid, const std::wstring& origDll) {
    wchar_t tmpdir[MAX_PATH]; GetTempPathW(MAX_PATH, tmpdir);
    srand((unsigned)(GetTickCount64() ^ GetCurrentProcessId() ^ pid));
    wchar_t rnd[9];
    for (int i = 0; i < 8; ++i) rnd[i] = L"0123456789abcdef"[rand() & 0xF];
    rnd[8] = 0;
    std::wstring copy = std::wstring(tmpdir) + rnd + L".dll";
    if (!CopyFileW(origDll.c_str(), copy.c_str(), FALSE)) { ilog(L"COPY_FAIL err=%lu", GetLastError()); wprintf(L"임시 DLL 복사 실패 (오류 %lu)\n", GetLastError()); return false; }

    {
        std::wstring origDir = origDll; origDir.erase(origDir.find_last_of(L"\\/") + 1);
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

    wprintf(L"임시 DLL: %s\n", copy.c_str());

    HANDLE h = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!h) { ilog(L"OPENPROCESS_FAIL err=%lu", GetLastError()); wprintf(L"OpenProcess 실패 (오류 %lu). 관리자 권한으로 실행하세요\n", GetLastError()); return false; }

    SIZE_T bytes = (copy.size() + 1) * sizeof(wchar_t);
    void* mem = VirtualAllocEx(h, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!mem || !WriteProcessMemory(h, mem, copy.c_str(), bytes, nullptr)) { ilog(L"VALLOC_OR_WPM_FAIL err=%lu", GetLastError()); wprintf(L"메모리 쓰기 실패\n"); CloseHandle(h); return false; }

    auto loadlib = (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    HANDLE t = CreateRemoteThread(h, nullptr, 0, loadlib, mem, 0, nullptr);
    if (!t) { ilog(L"CRT_FAIL err=%lu", GetLastError()); wprintf(L"CreateRemoteThread 실패\n"); VirtualFreeEx(h, mem, 0, MEM_RELEASE); CloseHandle(h); return false; }
    WaitForSingleObject(t, INFINITE);
    DWORD rc = 0; GetExitCodeThread(t, &rc);
    VirtualFreeEx(h, mem, 0, MEM_RELEASE);
    CloseHandle(t);
    CloseHandle(h);

    if (!rc) {
        ilog(L"LOADLIBRARY_FAIL exitcode=0");
        wprintf(L"대상 안에서 LoadLibrary 실패 (hook.dll 비트/의존성 확인)\n");
        DeleteFileW(copy.c_str()); DeleteFileW((copy + L".dir").c_str());
        return false;
    }
    ilog(L"INJECT_OK %s", copy.c_str());
    wprintf(L"주입 완료. out\\hook.log를 확인하세요.\n");
    // hook.dll은 규칙 적용 후 자기 스스로 언로드하므로(hook.cpp의 self-unload) 여기서 굳이
    // 대기 후 삭제하지 않는다 — %TEMP%에 임시 파일이 남아도 무해하니 그냥 둔다.
    return true;
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

    // 채널이동 등으로 게임이 Lua를 재로드하면 이전에 넣은 패치가 날아간다. 그때마다 이 창을
    // 다시 실행할 필요 없이, Insert 키를 누르면 같은 프로세스에 재주입한다(원본 제작자도 이 방식).
    wprintf(L"%s 대기 중... (게임을 실행하세요)\n", exe);
    GetAsyncKeyState(VK_INSERT); // 시작 전에 눌려있던 상태를 흘려보내 첫 루프에서 오탐하지 않게 한다
    for (;;) {
        DWORD pid;
        while (!(pid = find_pid(exe))) Sleep(500);
        wprintf(L"pid %lu 발견. 2초 후 주입합니다.\n", pid);
        Sleep(2000);
        inject_into(pid, dll);
        wprintf(L"Insert 키: 재주입 (채널이동 등으로 훅이 빠졌을 때 누르세요). 게임 종료 시 자동으로 재대기합니다.\n");

        for (;;) {
            Sleep(100);
            if (!find_pid(exe)) { wprintf(L"게임 종료 감지. 재시작을 기다립니다.\n"); break; }
            if (GetAsyncKeyState(VK_INSERT) & 1) {
                DWORD curPid = find_pid(exe);
                if (curPid) {
                    wprintf(L"Insert 감지 -> 재주입\n");
                    inject_into(curPid, dll);
                }
            }
        }
    }
}
