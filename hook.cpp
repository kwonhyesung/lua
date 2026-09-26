// hook.cpp — xlua.dll의 luaL_loadbufferx를 후킹해 Lua 청크를 로그/덤프/치환한다.
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
#include <tlhelp32.h>
#include <mutex>
#include <fstream>
#include <cstdarg>
#include <unordered_map>
#include <unordered_set>
#include <atomic>
#include <filesystem>
#include <algorithm>
namespace fs = std::filesystem;

// Task 1 (tools/find_sig.py) 의 SIG: 줄 (luaL_loadbufferx). 스펙 10절 참고.
static const char* LOADBUFFERX_SIG = "4C 8B DC 53 48 83 EC 60 4D 89 43 C0 48 8D 05 ?? ?? ?? ?? 49 89 43 D8 4C 8D 05 ?? ?? ?? ?? 49 8D 43 B8";

// GameAssembly.dll의 xLua DoString(byte[] chunk, string chunkName, ...) 진입점 프롤로그.
// IDA로 실제 luaL_loadbufferx 호출자를 추적해 확정(RVA 0x2EC1B80). 함수 시작 그 자체가 HWBP 대상:
// rcx=this, rdx=byte[]배열, r8=chunkName(Il2CppString*), r9=context — 표준 fastcall.
static const char* BYTEARRAY_SIG = "48 89 5C 24 10 48 89 74 24 18 4C 89 4C 24 20 57 41 54 41 55 41 56 41 57";
static constexpr size_t BYTEARRAY_KNOWN_RVA = 0x2EC1B80;

typedef void lua_State;

static HMODULE        g_self;
static void*          g_target;   // luaL_loadbufferx 주소 (HWBP DR0 대상)
static void*          g_target2;  // byte[] 로더 주소 (HWBP DR2 대상, 없으면 nullptr=비활성)
static void*          g_arrayNewSpecific;  // GameAssembly.dll!il2cpp_array_new_specific
static Config         g_cfg;
static std::wstring   g_base;   // hook.dll 폴더
static std::mutex     g_mu;     // ponytail: 로그·덤프 전역 락 하나
static FILE*          g_log;
static void*          g_vehHandle;   // AddVectoredExceptionHandler 반환값 — 언로드 전에 반드시 해제

static std::atomic<size_t> g_chunkCount{0}, g_replacedCount{0}, g_missCount{0};

static constexpr int KEEP_LOGS = 10;   // hook.log 외에 보관할 이전 로그 개수
// 기존 hook.log를 타임스탬프 이름으로 보존하고, 오래된 보존본은 KEEP_LOGS개만 남기고 지운다.
static void rotate_log(const std::wstring& base) {
    fs::path cur = fs::path(base) / L"hook.log";
    std::error_code ec;
    if (fs::exists(cur, ec)) {
        SYSTEMTIME t; GetLocalTime(&t);
        wchar_t stamp[32];
        swprintf(stamp, 32, L"hook_%04d%02d%02d_%02d%02d%02d.log", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
        fs::rename(cur, fs::path(base) / stamp, ec);
    }
    std::vector<fs::path> archived;
    for (auto& e : fs::directory_iterator(fs::path(base), ec)) {
        std::wstring name = e.path().filename().wstring();
        if (name.rfind(L"hook_", 0) == 0 && name.size() > 4 && name.substr(name.size() - 4) == L".log")
            archived.push_back(e.path());
    }
    std::sort(archived.begin(), archived.end());   // 이름에 타임스탬프가 들어있어 사전순 = 시간순
    while ((int)archived.size() > KEEP_LOGS) { fs::remove(archived.front(), ec); archived.erase(archived.begin()); }
}

// 진짜 원샷: 대상 whole 규칙이 전부 REPLACED되면(보통 1초 이내) HWBP를 완전히 풀고
// 그 뒤로는 세션 끝까지 DR 레지스터를 다시 안 건드린다 (커널 안티치트 노출 시간 최소화).
// DLL 자체는 언로드하지 않는다 — 반복 재주입이 아니라 이번엔 반복 arm/disarm 자체를 없앤다.
static std::mutex g_doneMu;
static std::unordered_set<size_t> g_doneRules;
static size_t g_totalWholeRules;
static void mark_rule_done(size_t idx) {
    std::lock_guard<std::mutex> lk(g_doneMu);
    g_doneRules.insert(idx);
}
static bool all_rules_done() {
    std::lock_guard<std::mutex> lk(g_doneMu);
    return g_totalWholeRules > 0 && g_doneRules.size() >= g_totalWholeRules;
}

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

// Il2CppString 레이아웃(sub_18000D210 역분석으로 확인): length(int32)@+0x10, UTF-16 문자열@+0x14.
static std::string il2cpp_string_to_utf8(const void* p) {
    if (!p) return {};
    const BYTE* s = (const BYTE*)p;
    int32_t len = *(const int32_t*)(s + 0x10);
    if (len <= 0 || len > 0x10000) return {};   // ponytail: 비정상 길이 방어
    const wchar_t* wtext = (const wchar_t*)(s + 0x14);
    int n = WideCharToMultiByte(CP_UTF8, 0, wtext, len, nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    if (n) WideCharToMultiByte(CP_UTF8, 0, wtext, len, &out[0], n, nullptr, nullptr);
    return out;
}

// 같은 이름·같은 내용이면 다시 쓰지 않고, 같은 이름·다른 내용이면 <name>_<hash>.ext 로 저장한다.
static void dump_chunk(const std::string& chunk, std::string fname, const char* ext) {
    uint32_t h = fnv1a(chunk);  // ponytail: 32-bit FNV, ~0.5% chance one pair collides among 6k chunks; switch to 64-bit if a dump ever goes missing
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
    ++g_chunkCount;
    if (g_cfg.log) logf("%s name=%s size=%zu h=%08X", bytecode ? "BYTECODE" : "LOAD", nm.c_str(), chunk.size(), fnv1a(chunk));
    if (g_cfg.dump) dump_chunk(chunk, sanitize(name), bytecode ? ".luac" : ".lua");
    for (size_t i = 0; i < g_cfg.rules.size(); ++i) {
        const Rule& r = g_cfg.rules[i];
        if (r.whole) {
            if (nm != r.name || r.replace.empty()) continue;   // 파일 못 읽었으면 원본 유지 (worker가 이미 ERROR 로그)
            chunk = r.replace;
            logf("REPLACED rule=%zu whole size=%zu", i + 1, chunk.size());
            mark_rule_done(i);
            ++g_replacedCount;
            return true;                                       // 통째 교체 뒤 문자열 규칙은 의미 없음
        }
        if (bytecode || nm.find(r.name) == std::string::npos) continue;
        size_t n = replace_all(chunk, r.find, r.replace);
        if (n) { logf("REPLACED rule=%zu n=%zu", i + 1, n); ++g_replacedCount; }
        else { if (g_cfg.log) logf("RULE_MISS rule=%zu", i + 1); ++g_missCount; }
    }
    return false;
}

// HWBP(DR0)가 luaL_loadbufferx 진입점에서 멈춘 순간엔 그 함수의 프롤로그가 아직
// 실행되기 전이라, MS x64 호출 규약대로 [rsp+0x00]=리턴주소, [rsp+0x20]=5번째 인자(mode)다.
// (함수 자신의 프롤로그 이후에 쓰이는 "[rsp+0x90]"과는 다른 오프셋 — 이건 raw entry 기준.)
// ponytail: 이 오프셋은 ABI 문서 기준 값. 실제 게임에서 dump=1로 한 번 돌려 REPLACED 뒤
// RESULT rc=0이 나오는지로 검증한다. rc!=0이면 여기부터 의심.
static constexpr ptrdiff_t MODE_ARG_OFFSET = 0x20;

// 청크 버퍼는 재개된 원본 함수가 즉시 읽으므로, 다음에 이 스레드가 다시 걸릴 때까지만
// 살아있으면 된다 → 스레드별 스크래치 하나로 충분 (재귀 호출은 덮어써짐, ponytail: 필요해지면 스택으로)
static thread_local std::string t_scratch;

// luaL_loadbufferx를 실제로 부르는 곳(리턴 주소) 목록을 중복 없이 기록한다.
// 호출 지점이 몇 종류 안 되므로(래퍼 함수 하나 정도) 금방 다 모인다.
static std::mutex g_callerMu;
static std::unordered_set<ULONG64> g_callerSeen;
static void log_caller_once(ULONG64 ret) {
    std::lock_guard<std::mutex> lk(g_callerMu);
    if (g_callerSeen.insert(ret).second) logf("CALLER ret=%p", (void*)ret);
}

static LONG CALLBACK veh_handler(EXCEPTION_POINTERS* info) {
    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
    PCONTEXT ctx = info->ContextRecord;

    if (ctx->Dr6 & 0x1) {                       // DR0: luaL_loadbufferx 진입
        ctx->Dr6 &= ~(DWORD64)0x1;
        const char* buff = (const char*)ctx->Rdx;
        size_t sz = (size_t)ctx->R8;
        const char* name = (const char*)ctx->R9;
        try {
            std::string chunk(buff ? buff : "", buff ? sz : 0);
            bool whole = process(chunk, name);
            t_scratch = std::move(chunk);
            ctx->Rdx = (DWORD64)t_scratch.data();
            ctx->R8 = (DWORD64)t_scratch.size();
            if (whole) *(const char**)(ctx->Rsp + MODE_ARG_OFFSET) = nullptr;
        } catch (...) {
            logf("ERROR exception in process name=%s", name ? name : "(null)");
        }
        // 리턴 주소에 DR1을 걸어 반환값을 잡는다 (RESULT 로그용, 1회성)
        ULONG64 ret = *(ULONG64*)ctx->Rsp;
        log_caller_once(ret);   // 진짜 호출자 찾기: 이 주소를 부른 곳(=return addr) 최초 1회만 기록
        ctx->Dr1 = ret;
        ctx->Dr7 = (ctx->Dr7 & ~((DWORD64)0xF << 20)) | ((DWORD64)1 << 2);
        ctx->EFlags |= 0x10000;   // RF: 이 명령어 재개 시 DR0가 즉시 재발동하는 것 방지 (안 하면 같은 주소에서 무한루프)
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (ctx->Dr6 & 0x2) {                       // DR1: 위에서 건 리턴 주소
        ctx->Dr6 &= ~(DWORD64)0x2;
        ctx->Dr7 &= ~((DWORD64)1 << 2);          // 1회성이므로 바로 해제
        if (g_cfg.log) logf("RESULT rc=%d", (int)ctx->Rax);
        ctx->EFlags |= 0x10000;   // RF: 이 반환 지점 재개 시 즉시 재발동 방지
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (ctx->Dr6 & 0x4) {                       // DR2: DoString(byte[] chunk, string chunkName, ...) 진입
        ctx->Dr6 &= ~(DWORD64)0x4;
        try {
            BYTE* arr = (BYTE*)ctx->Rdx;         // byte[] 배열 (rdx)
            const void* nameObj = (const void*)ctx->R8;  // Il2CppString* (r8)
            std::string nm = il2cpp_string_to_utf8(nameObj);
            if (g_cfg.log && arr) {
                int32_t len = *(const int32_t*)(arr + 0x18);
                uint32_t h = (len > 0 && len < 0x1000000) ? fnv1a(std::string((const char*)(arr + 0x20), (size_t)len)) : 0;
                logf("BYTEARRAY_HIT arr=%p name=%s size=%d h=%08X newspecific=%p", arr, nm.c_str(), len, h, g_arrayNewSpecific);
            }
            if (arr && g_arrayNewSpecific && !nm.empty()) {
                for (size_t i = 0; i < g_cfg.rules.size(); ++i) {
                    const Rule& r = g_cfg.rules[i];
                    if (!r.whole || nm != r.name || r.replace.empty()) continue;
                    void* klass = *(void**)arr;   // il2cpp 배열 레이아웃: klass(0x00) 재사용
                    using NewSpecific = void* (*)(void*, size_t);
                    void* newArr = ((NewSpecific)g_arrayNewSpecific)(klass, r.replace.size());
                    if (newArr) {
                        memcpy((BYTE*)newArr + 0x20, r.replace.data(), r.replace.size());  // data는 +0x20부터
                        ctx->Rdx = (DWORD64)newArr;
                        logf("REPLACE_BYTEARRAY rule=%zu name=%s size=%zu", i + 1, r.name.c_str(), r.replace.size());
                        mark_rule_done(i);
                    } else {
                        logf("ERROR il2cpp_array_new_specific failed rule=%zu", i + 1);
                    }
                    break;
                }
            }
        } catch (...) {
            logf("ERROR exception in bytearray hook");
        }
        ctx->EFlags |= 0x10000;   // RF: 동일 주소 즉시 재발동 방지
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;            // 우리 DR이 아님 (다른 디버거/스레드 몫)
}

// DR0=luaL_loadbufferx, DR2=byte[] 로더(있으면). 둘 다 execute(RW=00)+1byte(LEN=00). 스레드 하나에 적용.
static void arm_thread(DWORD tid) {
    HANDLE h = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, tid);
    if (!h) { logf("ARM_FAIL tid=%lu OpenThread err=%lu", tid, GetLastError()); return; }
    if (SuspendThread(h) != (DWORD)-1) {
        CONTEXT ctx{}; ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        if (GetThreadContext(h, &ctx)) {
            ctx.Dr0 = (DWORD64)g_target;
            DWORD64 dr7 = (ctx.Dr7 & ~((DWORD64)0xF << 16)) | (DWORD64)1;   // L0=bit0
            if (g_target2) {
                ctx.Dr2 = (DWORD64)g_target2;
                dr7 = (dr7 & ~((DWORD64)0xF << 24)) | ((DWORD64)1 << 4);   // L2=bit4
            }
            ctx.Dr7 = dr7;
            BOOL setOk = SetThreadContext(h, &ctx);
            CONTEXT verify{}; verify.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            GetThreadContext(h, &verify);
            logf("ARM tid=%lu set=%d dr0=%p dr2=%p dr7=0x%llX (readback dr0=%p dr2=%p dr7=0x%llX)",
                 tid, setOk, (void*)ctx.Dr0, (void*)ctx.Dr2, dr7,
                 (void*)verify.Dr0, (void*)verify.Dr2, (unsigned long long)verify.Dr7);
        } else {
            logf("ARM_FAIL tid=%lu GetThreadContext err=%lu", tid, GetLastError());
        }
        ResumeThread(h);
    }
    CloseHandle(h);
}

// DR 끄기만 하면 됨 (RW/LEN은 이미 execute로 세팅돼있어 안 건드려도 무해).
static void disarm_thread(DWORD tid) {
    HANDLE h = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, tid);
    if (!h) return;
    if (SuspendThread(h) != (DWORD)-1) {
        CONTEXT ctx{}; ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        if (GetThreadContext(h, &ctx)) {
            ctx.Dr7 &= ~(DWORD64)0x11;   // L0(bit0) + L2(bit4) 끔
            SetThreadContext(h, &ctx);
        }
        ResumeThread(h);
    }
    CloseHandle(h);
}

// 대상 whole 규칙이 전부 잡히거나(보통 1초 이내) DISCOVERY_WINDOW_MS가 지나면 armed 스레드를 모두
// disarm한다. disarm_thread의 SuspendThread→SetThreadContext→ResumeThread가, 하필 그 스레드가
// g_target 명령어를 막 실행하려는 순간(#DB가 이미 래치됐지만 유저모드 전달 전)과 겹치면 예외 전달이
// 꼬여서 크래시로 이어지는 레이스가 실제로 재현됐다(crashpad dmp: exception=SINGLE_STEP,
// faulting_addr=g_target). 반대로 VEH+DLL을 세션 내내 살려두면(레이스는 안전하지만) 디버그 레지스터가
// 계속 켜져 있는 걸 안티치트가 감지해 TerminateProcess로 죽이는 것도 실제로 재현됐다(크래시 다이얼로그도
// crashpad dmp도 없이 프로세스만 사라짐 — DLL_PROCESS_DETACH도 안 불림, TerminateProcess의 전형적 증상).
// 그래서 절충: disarm 직후 VEH는 짧은 유예 시간만 더 살려서(레이스로 놓친 스레드의 낙오 히트를 안전망으로
// 잡고) 그 다음엔 원래 설계대로 VEH 제거+DLL 언로드해서 노출 시간을 최소화한다.
static constexpr DWORD DISARM_GRACE_MS = 2000;
static constexpr DWORD DISCOVERY_WINDOW_MS = 90000;   // 로그인/로딩 화면이 10초보다 훨씬 오래 걸려서 늘림
static DWORD WINAPI watcher(LPVOID) {
    std::unordered_set<DWORD> armed;
    DWORD self = GetCurrentThreadId();
    ULONGLONG deadline = GetTickCount64() + DISCOVERY_WINDOW_MS;
    for (;;) {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snap != INVALID_HANDLE_VALUE) {
            THREADENTRY32 te{ sizeof te };
            DWORD pid = GetCurrentProcessId();
            if (Thread32First(snap, &te)) do {
                if (te.th32OwnerProcessID == pid && te.th32ThreadID != self && !armed.count(te.th32ThreadID)) {
                    arm_thread(te.th32ThreadID);
                    armed.insert(te.th32ThreadID);
                    Sleep(20);   // 스레드가 몰려 생길 때 SuspendThread를 연달아 때리지 않도록 완화
                }
            } while (Thread32Next(snap, &te));
            CloseHandle(snap);
        }
        if (all_rules_done()) { logf("dormant: all %zu rule(s) confirmed", g_totalWholeRules); break; }
        if (GetTickCount64() >= deadline) { logf("dormant: %lums window expired", DISCOVERY_WINDOW_MS); break; }
        Sleep(250);
    }
    for (DWORD tid : armed) disarm_thread(tid);
    logf("dormant: disarmed %zu thread(s)", armed.size());
    {
        std::lock_guard<std::mutex> lk(g_doneMu);
        logf("SUMMARY chunks=%zu replaced=%zu miss=%zu whole_rules_done=%zu/%zu armed=%zu",
             g_chunkCount.load(), g_replacedCount.load(), g_missCount.load(), g_doneRules.size(), g_totalWholeRules, armed.size());
        for (size_t i = 0; i < g_cfg.rules.size(); ++i)
            if (g_cfg.rules[i].whole && !g_doneRules.count(i))
                logf("SUMMARY whole rule=%zu name=%s NEVER FIRED", i + 1, g_cfg.rules[i].name.c_str());
    }
    // 유예 시간: disarm 레이스로 놓친 스레드가 있어도 이 창 안에서 히트하면 VEH가 안전하게 처리한다.
    Sleep(DISARM_GRACE_MS);
    if (g_vehHandle) { RemoveVectoredExceptionHandler(g_vehHandle); g_vehHandle = nullptr; }
    logf("self-unloading (single injection this session, no reapply)");
    FreeLibraryAndExitThread(g_self, 0);   // 이 스레드는 여기서 끝남 (반환 안 함)
}

static std::string read_file(const std::wstring& path) {
    std::ifstream f(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

static DWORD WINAPI worker(LPVOID) {
  try {
    g_base = L"C:\\Users\\kwon\\Desktop\\luahook\\out";   // 고정: DLL 사본이 Temp에서 돌아도 rules/log/dump는 항상 여기
    rotate_log(g_base);   // 이전 hook.log를 타임스탬프 이름으로 보존 (최근 KEEP_LOGS개만)
    g_log = _wfopen((g_base + L"\\hook.log").c_str(), L"w");
    logf("=== attached pid=%lu === base=%ls", GetCurrentProcessId(), g_base.c_str());

    g_cfg = parse_rules(read_file(g_base + L"\\rules.txt"));
    for (size_t i = 0; i < g_cfg.rules.size(); ++i) {           // 이름|@파일 규칙: 파일 내용을 미리 읽어둔다
        Rule& r = g_cfg.rules[i];
        if (!r.whole) continue;
        std::wstring p = widen(r.replace);
        if (p.size() < 2 || p[1] != L':') p = g_base + L"\\" + p;   // 상대경로는 hook.dll 폴더 기준
        std::string body = read_file(p);
        if (body.empty()) logf("ERROR rule=%zu file empty or missing: %s", i + 1, r.replace.c_str());
        r.replace = body;
        ++g_totalWholeRules;
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
    g_target = text + hits[0];
    logf("luaL_loadbufferx @ %p (sig match 1)", g_target);

    // byte[] 훅(DR2)은 선택 사항 — 실패해도 luaL_loadbufferx 훅만으로 계속 동작한다.
    if (HMODULE ga = GetModuleHandleW(L"GameAssembly.dll")) {
        BYTE* base = (BYTE*)ga;
        std::vector<int> sig2 = parse_sig(BYTEARRAY_SIG);
        BYTE* known = base + BYTEARRAY_KNOWN_RVA;
        bool ok = true;
        for (size_t i = 0; i < sig2.size(); ++i)
            if (sig2[i] != -1 && known[i] != (unsigned char)sig2[i]) { ok = false; break; }
        if (ok) {
            g_target2 = known;
            logf("bytearray loader @ %p (known RVA 0x%zX)", g_target2, BYTEARRAY_KNOWN_RVA);
        } else {
            auto* nt2 = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
            DWORD imgSize = nt2->OptionalHeader.SizeOfImage;
            std::vector<size_t> hits2 = find_sig(base, imgSize, sig2);
            if (hits2.size() == 1) {
                g_target2 = base + hits2[0];
                logf("bytearray loader @ %p (rescanned, RVA moved from 0x%zX)", g_target2, BYTEARRAY_KNOWN_RVA);
            } else {
                logf("BYTEARRAY_SIG_FAIL count=%zu (byte[] 교체 비활성, luaL_loadbufferx 훅은 정상 동작)", hits2.size());
            }
        }
        if (g_target2) {
            g_arrayNewSpecific = (void*)GetProcAddress(ga, "il2cpp_array_new_specific");
            if (!g_arrayNewSpecific) { logf("il2cpp_array_new_specific not found, byte[] 교체 비활성"); g_target2 = nullptr; }
        }
    } else {
        logf("GameAssembly.dll not loaded, byte[] 훅 비활성");
    }

    g_vehHandle = AddVectoredExceptionHandler(1, veh_handler);
    if (!g_vehHandle) { logf("AddVectoredExceptionHandler failed"); return 0; }
    if (!CreateThread(nullptr, 0, watcher, nullptr, 0, nullptr)) { logf("watcher thread failed"); return 0; }
    logf("hook ready (HWBP+VEH), one-shot window=%lums, %zu rule(s) to catch", DISCOVERY_WINDOW_MS, g_totalWholeRules);
    return 0;
  } catch (...) {
    logf("ERROR worker exception");
    return 0;
  }
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = h;
        DisableThreadLibraryCalls(h);
        CreateThread(nullptr, 0, worker, nullptr, 0, nullptr);
    } else if (reason == DLL_PROCESS_DETACH && g_log) {
        // 정상 종료(ExitProcess)면 여기가 불리고, 크래시면 안 불린다 → 이 줄의 유무로 튕김/정상종료 구분.
        // 다른 스레드는 이미 정리된 상태라 뮤텍스를 잡지 않고 바로 쓴다.
        SYSTEMTIME t; GetLocalTime(&t);
        fprintf(g_log, "[%02d:%02d:%02d.%03d] === process exit (%s) ===\n",
                t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, reserved ? "terminating" : "dll unloaded");
        fflush(g_log);
    }
    return TRUE;
}
#endif
