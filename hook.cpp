// hook.cpp — xlua.dll의 lua_load를 후킹해 Lua 청크를 치환한다.
// /DSELFTEST 로 빌드하면 순수 로직만 콘솔 테스트한다.
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
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
    unsigned char hay[] = {0x00, 0x48, 0x8B, 0xFF, 0x24, 0x48, 0x8B, 0x00, 0x24, 0x48};
    std::vector<size_t> h = find_sig(hay, sizeof hay, p);
    assert(h.size() == 2 && h[0] == 1 && h[1] == 5);
    assert(find_sig(hay, 3, p).empty());

    puts("SELFTEST OK");
    return 0;
}
#endif
