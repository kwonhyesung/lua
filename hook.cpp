// hook.cpp — xlua.dll의 lua_load를 후킹해 Lua 청크를 치환한다.
// /DSELFTEST 로 빌드하면 순수 로직만 콘솔 테스트한다.
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cassert>

struct Rule { std::string name, find, replace; };
struct Config { bool dump = true; bool log = true; std::vector<Rule> rules; };

Config parse_rules(const std::string& text);
size_t replace_all(std::string& s, const std::string& find, const std::string& rep);
std::string sanitize(const char* chunkname);
std::vector<int> parse_sig(const char* sig);
std::vector<size_t> find_sig(const unsigned char* hay, size_t n, const std::vector<int>& pat);

static std::string unescape(const std::string& in) {
    std::string out;
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '\\' && i + 1 < in.size()) {
            char n = in[++i];
            out += n == 'n' ? '\n' : n == 't' ? '\t' : n == '\\' ? '\\' : n;
        } else out += in[i];
    }
    return out;
}

Config parse_rules(const std::string& text) {
    Config c;
    std::string t = text;
    if (t.rfind("\xEF\xBB\xBF", 0) == 0) t.erase(0, 3);  // UTF-8 BOM
    size_t pos = 0;
    while (pos <= t.size()) {
        size_t nl = t.find('\n', pos);
        std::string line = t.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = nl == std::string::npos ? t.size() + 1 : nl + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        size_t p1 = line.find('|');
        if (p1 == std::string::npos) {                       // key=value
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            if (k == "dump") c.dump = v == "1";
            else if (k == "log") c.log = v == "1";
            continue;
        }
        size_t p2 = line.find('|', p1 + 1);
        if (p2 == std::string::npos || line.find('|', p2 + 1) != std::string::npos) continue;  // | 정확히 2개
        c.rules.push_back({line.substr(0, p1), unescape(line.substr(p1 + 1, p2 - p1 - 1)), unescape(line.substr(p2 + 1))});
    }
    return c;
}

size_t replace_all(std::string& s, const std::string& find, const std::string& rep) {
    if (find.empty()) return 0;
    size_t n = 0, pos = 0;
    while ((pos = s.find(find, pos)) != std::string::npos) {
        s.replace(pos, find.size(), rep);
        pos += rep.size();
        ++n;
    }
    return n;
}

std::string sanitize(const char* chunkname) {
    if (!chunkname) return "(null)";
    std::string s = chunkname;
    if (!s.empty() && (s[0] == '@' || s[0] == '=')) s.erase(0, 1);
    for (char& ch : s)
        if (strchr("\\/:*?\"<>|", ch)) ch = '_';
    return s.empty() ? "(empty)" : s;
}

std::vector<int> parse_sig(const char* sig) {
    std::vector<int> out;
    for (const char* p = sig; *p; ) {
        while (*p == ' ') ++p;
        if (!*p) break;
        if (!p[1]) break;  // truncated token, avoid reading past NUL
        if (p[0] == '?') out.push_back(-1);
        else out.push_back((int)strtol(std::string(p, 2).c_str(), nullptr, 16));
        p += 2;
    }
    return out;
}

std::vector<size_t> find_sig(const unsigned char* hay, size_t n, const std::vector<int>& pat) {
    std::vector<size_t> hits;
    if (pat.empty() || n < pat.size()) return hits;
    for (size_t i = 0; i + pat.size() <= n; ++i) {
        size_t k = 0;
        for (; k < pat.size(); ++k)
            if (pat[k] != -1 && hay[i + k] != (unsigned char)pat[k]) break;
        if (k == pat.size()) hits.push_back(i);
    }
    return hits;  // ponytail: O(n*m) 단순 스캔. .text 수백KB × 32바이트면 충분히 빠름
}

#ifdef SELFTEST
int main() {
    // rules 파서
    Config c = parse_rules("\xEF\xBB\xBF# c\r\n\r\ndump=0\nlog=1\nmain.lua|a\\nb|x\\\\y\nbad line\n");
    assert(!c.dump && c.log);
    assert(c.rules.size() == 1);
    assert(c.rules[0].name == "main.lua");
    assert(c.rules[0].find == "a\nb");
    assert(c.rules[0].replace == "x\\y");
    assert(parse_rules("").dump == true);  // 기본값

    // 치환
    std::string s = "aXbXc";
    assert(replace_all(s, "X", "YY") == 2 && s == "aYYbYYc");
    assert(replace_all(s, "Z", "q") == 0 && s == "aYYbYYc");
    s = "aa"; assert(replace_all(s, "a", "") == 2 && s.empty());

    // sanitize
    assert(sanitize("@ui/shop:1.lua") == "ui_shop_1.lua");
    assert(sanitize("=[C]") == "[C]");
    assert(sanitize(nullptr) == "(null)");

    // 시그니처
    std::vector<int> p = parse_sig("48 8B ?? 24");
    assert(p.size() == 4 && p[2] == -1 && p[3] == 0x24);
    assert(parse_sig("48 8").size() == 1);
    assert(parse_sig("48 ").size() == 1);
    unsigned char hay[] = {0x00, 0x48, 0x8B, 0xFF, 0x24, 0x48, 0x8B, 0x00, 0x24, 0x48};
    std::vector<size_t> h = find_sig(hay, sizeof hay, p);
    assert(h.size() == 2 && h[0] == 1 && h[1] == 5);
    assert(find_sig(hay, 3, p).empty());

    puts("SELFTEST OK");
    return 0;
}
#else  // ===================== DLL =====================
#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mutex>
#include <fstream>
#include <cstdarg>
#include "MinHook.h"

// Task 1 (tools/find_sig.py) 의 SIG: 줄 (luaL_loadbufferx). 스펙 10절 참고.
static const char* LOADBUFFERX_SIG = "4C 8B DC 53 48 83 EC 60 4D 89 43 C0 48 8D 05 ?? ?? ?? ?? 49 89 43 D8 4C 8D 05 ?? ?? ?? ?? 49 8D 43 B8";

typedef void lua_State;
typedef int (*loadbufferx_t)(lua_State*, const char* buff, size_t sz, const char* name, const char* mode);

static HMODULE        g_self;
static loadbufferx_t  g_orig;
static Config         g_cfg;
static std::wstring   g_base;   // hook.dll 폴더
static std::mutex     g_mu;     // ponytail: 로그·덤프 전역 락 하나
static FILE*          g_log;

static void logf(const char* fmt, ...) {
    if (!g_log) return;
    std::lock_guard<std::mutex> lk(g_mu);
    SYSTEMTIME t; GetLocalTime(&t);
    fprintf(g_log, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list ap; va_start(ap, fmt); vfprintf(g_log, fmt, ap); va_end(ap);
    fputc('\n', g_log); fflush(g_log);
}

static std::wstring widen(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n ? n - 1 : 0, L'\0');
    if (n) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}

static void dump_chunk(const std::string& chunk, const std::string& fname) {
    std::wstring dir = g_base + L"\\dump";
    CreateDirectoryW(dir.c_str(), nullptr);
    std::ofstream f(dir + L"\\" + widen(fname) + L".lua", std::ios::binary);
    f.write(chunk.data(), (std::streamsize)chunk.size());
    logf("DUMP dump/%s.lua", fname.c_str());
}

// 수집된 청크에 로그/덤프/치환을 적용한다. chunk는 제자리 수정.
static void process(std::string& chunk, const char* name) {
    if (!chunk.empty() && chunk[0] == '\x1b') { logf("BYTECODE skip name=%s", name ? name : "(null)"); return; }
    if (g_cfg.log) logf("LOAD name=%s size=%zu", name ? name : "(null)", chunk.size());
    if (g_cfg.dump) dump_chunk(chunk, sanitize(name));
    std::string nm = name ? name : "";
    for (size_t i = 0; i < g_cfg.rules.size(); ++i) {
        const Rule& r = g_cfg.rules[i];
        if (nm.find(r.name) == std::string::npos) continue;
        size_t n = replace_all(chunk, r.find, r.replace);
        if (n) logf("REPLACED rule=%zu n=%zu", i + 1, n);
        else if (g_cfg.log) logf("RULE_MISS rule=%zu", i + 1);
    }
}

static int hooked_loadbufferx(lua_State* L, const char* buff, size_t sz, const char* name, const char* mode) {
    std::string chunk(buff ? buff : "", buff ? sz : 0);
    try { process(chunk, name); }
    catch (...) { logf("ERROR exception in process name=%s", name ? name : "(null)"); }
    int rc = g_orig(L, chunk.data(), chunk.size(), name, mode);
    if (g_cfg.log || rc) logf("RESULT rc=%d name=%s", rc, name ? name : "(null)");
    return rc;
}

static std::string read_file(const std::wstring& path) {
    std::ifstream f(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

static DWORD WINAPI worker(LPVOID) {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(g_self, path, MAX_PATH);
    g_base = path; g_base.erase(g_base.find_last_of(L"\\/"));
    g_log = _wfopen((g_base + L"\\hook.log").c_str(), L"a");
    logf("=== attached pid=%lu ===", GetCurrentProcessId());

    g_cfg = parse_rules(read_file(g_base + L"\\rules.txt"));
    logf("rules: %zu, dump=%d, log=%d", g_cfg.rules.size(), g_cfg.dump, g_cfg.log);

    HMODULE x;
    while (!(x = GetModuleHandleW(L"xlua.dll"))) Sleep(100);

    auto* nt = (IMAGE_NT_HEADERS*)((BYTE*)x + ((IMAGE_DOS_HEADER*)x)->e_lfanew);
    auto* sec = IMAGE_FIRST_SECTION(nt);
    BYTE* text = nullptr; DWORD tsize = 0;
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec)
        if (memcmp(sec->Name, ".text", 5) == 0) { text = (BYTE*)x + sec->VirtualAddress; tsize = sec->Misc.VirtualSize; break; }
    if (!text) { logf("SIG_FAIL no .text"); return 0; }
    logf("xlua.dll found at %p text=%p+0x%lX", x, text, tsize);

    std::vector<size_t> hits = find_sig(text, tsize, parse_sig(LOADBUFFERX_SIG));
    if (hits.size() != 1) { logf("SIG_FAIL count=%zu", hits.size()); return 0; }
    void* target = text + hits[0];
    logf("luaL_loadbufferx @ %p (sig match 1)", target);

    MH_STATUS st = MH_Initialize();
    if (st != MH_OK) { logf("MH_Initialize failed %d", st); return 0; }
    st = MH_CreateHook(target, (void*)hooked_loadbufferx, (void**)&g_orig);
    if (st != MH_OK) { logf("MH_CreateHook failed %d", st); return 0; }
    st = MH_EnableHook(target);
    if (st != MH_OK) { logf("MH_EnableHook failed %d", st); return 0; }
    logf("hook ready");
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = h;
        DisableThreadLibraryCalls(h);
        CreateThread(nullptr, 0, worker, nullptr, 0, nullptr);
    }
    return TRUE;
}
#endif
