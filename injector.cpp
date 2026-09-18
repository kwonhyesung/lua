// injector.cpp — 대상 exe가 뜨길 기다렸다가 hook.dll을 LoadLibraryW 원격 스레드로 주입한다.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <string>
#include <io.h>
#include <fcntl.h>
#include <conio.h>

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

    wprintf(L"%s 대기 중... (게임을 실행하세요)\n", exe);
    DWORD pid;
    while (!(pid = find_pid(exe))) Sleep(500);
    wprintf(L"pid %lu 발견. 2초 후 주입합니다.\n", pid);
    Sleep(2000);

    HANDLE h = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!h) return fail(L"OpenProcess 실패. 관리자 권한으로 실행하세요");

    SIZE_T bytes = (dll.size() + 1) * sizeof(wchar_t);
    void* mem = VirtualAllocEx(h, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!mem || !WriteProcessMemory(h, mem, dll.c_str(), bytes, nullptr)) return fail(L"메모리 쓰기 실패");

    auto loadlib = (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    HANDLE t = CreateRemoteThread(h, nullptr, 0, loadlib, mem, 0, nullptr);
    if (!t) return fail(L"CreateRemoteThread 실패");
    WaitForSingleObject(t, INFINITE);
    DWORD rc = 0; GetExitCodeThread(t, &rc);
    VirtualFreeEx(h, mem, 0, MEM_RELEASE);
    CloseHandle(t); CloseHandle(h);

    if (!rc) return fail(L"대상 안에서 LoadLibrary 실패 (hook.dll 비트/의존성 확인)");
    wprintf(L"주입 완료. hook.log를 확인하세요.\n아무 키나 누르면 닫힙니다.");
    _getwch();
    return 0;
}
