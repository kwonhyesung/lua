// hook.cpp — xlua.dll의 lua_load를 후킹해 Lua 청크를 치환한다.
// /DSELFTEST 로 빌드하면 순수 로직만 콘솔 테스트한다.
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <cassert>

struct Rule { std::string name, find, replace; bool whole = false; };   // whole: name 정확일치 → replace(파일 내용)로 청크 전체 교체
struct Config { bool dump = true; bool log = true; std::vector<Rule> rules; };

Config parse_rules(const std::string& text);
size_t replace_all(std::string& s, const std::string& find, const std::string& rep);
std::string sanitize(const char* chunkname);
std::vector<int> parse_sig(const char* sig);
std::vector<size_t> find_sig(const unsigned char* hay, size_t n, const std::vector<int>& pat);
uint32_t fnv1a(const std::string& s);

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
        if (p1 + 1 < line.size() && line[p1 + 1] == '@' && line.find('|', p1 + 1) == std::string::npos) {  // 이름|@파일
            Rule r; r.name = line.substr(0, p1); r.replace = line.substr(p1 + 2); r.whole = true;
            c.rules.push_back(r);
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

uint32_t fnv1a(const std::string& s) {
    uint32_t h = 0x811C9DC5u;
    for (unsigned char c : s) { h ^= c; h *= 0x01000193u; }
    return h;
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

    // whole-replace 규칙
    Config w = parse_rules("Foo.Bar|@my.lua\nBaz|x|y\n");
    assert(w.rules.size() == 2);
    assert(w.rules[0].whole && w.rules[0].name == "Foo.Bar" && w.rules[0].replace == "my.lua" && w.rules[0].find.empty());
    assert(!w.rules[1].whole);
    // fnv1a
    assert(fnv1a("") == 0x811C9DC5u);
    assert(fnv1a("a") == 0xE40C292Cu);
    assert(fnv1a("a") != fnv1a("b"));

    puts("SELFTEST OK");
    return 0;
}
#else  // ===================== DLL =====================
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mutex>
#include <fstream>
#include <cstdarg>
#include <unordered_map>
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

static std::unordered_map<std::string, uint32_t> g_dumped;  // fname -> 내용 해시 (g_mu로 보호)

// 같은 이름·같은 내용이면 다시 쓰지 않고, 같은 이름·다른 내용이면 <name>_<hash>.ext 로 저장한다.
static void dump_chunk(const std::string& chunk, std::string fname, const char* ext) {
    uint32_t h = fnv1a(chunk);
    {
        std::lock_guard<std::mutex> lk(g_mu);
        auto it = g_dumped.find(fname);
        if (it != g_dumped.end()) {
            if (it->second == h) return;                       // 이미 저장한 동일 내용
            char suf[16]; snprintf(suf, sizeof suf, "_%08X", h);
            fname += suf;
            if (g_dumped.count(fname)) return;
        }
        g_dumped[fname] = h;
    }
    std::wstring dir = g_base + L"\\dump";
    CreateDirectoryW(dir.c_str(), nullptr);
    std::ofstream f(dir + L"\\" + widen(fname) + widen(ext), std::ios::binary);
    if (!f) { logf("ERROR dump open failed %s", fname.c_str()); return; }
    f.write(chunk.data(), (std::streamsize)chunk.size());
    if (g_cfg.log) logf("DUMP dump/%s%s", fname.c_str(), ext);
}

// 청크에 로그/덤프/치환을 적용한다. chunk는 제자리 수정. 반환값: 청크 전체가 교체되었는가.
static bool process(std::string& chunk, const char* name) {
    bool bytecode = !chunk.empty() && chunk[0] == '\x1b';
    std::string nm = name ? name : "(null)";
    if (g_cfg.log) logf("%s name=%s size=%zu", bytecode ? "BYTECODE" : "LOAD", nm.c_str(), chunk.size());
    if (g_cfg.dump) dump_chunk(chunk, sanitize(name), bytecode ? ".luac" : ".lua");
    for (size_t i = 0; i < g_cfg.rules.size(); ++i) {
        const Rule& r = g_cfg.rules[i];
        if (r.whole) {
            if (nm != r.name) continue;
            chunk = r.replace;
            logf("REPLACED rule=%zu whole size=%zu", i + 1, chunk.size());
            return true;                                       // 통째 교체 뒤 문자열 규칙은 의미 없음
        }
        if (bytecode || nm.find(r.name) == std::string::npos) continue;
        size_t n = replace_all(chunk, r.find, r.replace);
        if (n) logf("REPLACED rule=%zu n=%zu", i + 1, n);
        else if (g_cfg.log) logf("RULE_MISS rule=%zu", i + 1);
    }
    return false;
}

static int hooked_loadbufferx(lua_State* L, const char* buff, size_t sz, const char* name, const char* mode) {
    try {
        std::string chunk(buff ? buff : "", buff ? sz : 0);
        bool whole = process(chunk, name);
        int rc = g_orig(L, chunk.data(), chunk.size(), name, whole ? nullptr : mode);
        if (g_cfg.log || rc) logf("RESULT rc=%d name=%s", rc, name ? name : "(null)");
        return rc;
    } catch (...) {
        logf("ERROR exception in process name=%s", name ? name : "(null)");
        return g_orig(L, buff, sz, name, mode);
    }
}

static std::string read_file(const std::wstring& path) {
    std::ifstream f(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

static DWORD WINAPI worker(LPVOID) {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(g_self, path, MAX_PATH);
    g_base = path;
    size_t slash = g_base.find_last_of(L"\\/");
    if (slash != std::wstring::npos) g_base.erase(slash);
    g_log = _wfopen((g_base + L"\\hook.log").c_str(), L"a");
    logf("=== attached pid=%lu ===", GetCurrentProcessId());

    g_cfg = parse_rules(read_file(g_base + L"\\rules.txt"));
    for (size_t i = 0; i < g_cfg.rules.size(); ++i) {           // 이름|@파일 규칙: 파일 내용을 미리 읽어둔다
        Rule& r = g_cfg.rules[i];
        if (!r.whole) continue;
        std::wstring p = widen(r.replace);
        if (p.size() < 2 || p[1] != L':') p = g_base + L"\\" + p;   // 상대경로는 hook.dll 폴더 기준
        std::string body = read_file(p);
        if (body.empty()) logf("ERROR rule=%zu file empty or missing: %s", i + 1, r.replace.c_str());
        r.replace = body;
    }
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
