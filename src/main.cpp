#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include "resource.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <mutex>
#include <regex>
#include <sstream>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

struct Caption {
    int startMs;
    int endMs;
    std::wstring text;
    std::wstring rawHtml; // Original HTML from SMI source (empty if not from SMI)
    std::string lang;
    std::wstring smiClass; // Raw SMI <P Class=...> value (empty if not from SMI)
};

struct ConvertResult {
    bool ok;
    std::wstring message;
};

enum class TargetFormat {
    ToSmi,
    ToSrt,
    ToAss
};

std::wstring Trim(const std::wstring& value) {
    size_t begin = 0;
    while (begin < value.size() && std::iswspace(value[begin])) {
        ++begin;
    }
    size_t end = value.size();
    while (end > begin && std::iswspace(value[end - 1])) {
        --end;
    }
    return value.substr(begin, end - begin);
}

// Prefix long/UNC paths with the \\?\ extended-length marker so file I/O
// stays reliable near or beyond MAX_PATH and on \\server\share paths.
std::wstring ToLongPath(const std::wstring& rawPath) {
    std::wstring ioPath = rawPath;
    if (ioPath.rfind(L"\\\\?\\", 0) != 0) {
        if (ioPath.rfind(L"\\\\", 0) == 0) {
            ioPath = L"\\\\?\\UNC\\" + ioPath.substr(2);
        } else if (ioPath.size() >= 248) {
            ioPath = L"\\\\?\\" + ioPath;
        }
    }
    return ioPath;
}

std::wstring ToLowerW(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

std::string ToLowerA(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::wstring MultiByteToWide(const std::string& input, UINT codePage) {
    if (input.empty()) {
        return L"";
    }

    int needed = MultiByteToWideChar(codePage, 0, input.c_str(), static_cast<int>(input.size()), nullptr, 0);
    if (needed <= 0) {
        return L"";
    }

    std::wstring result(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(codePage, 0, input.c_str(), static_cast<int>(input.size()), result.data(), needed);
    return result;
}

bool IsValidUtf8(const std::string& bytes) {
    int continuation = 0;
    for (unsigned char ch : bytes) {
        if (continuation == 0) {
            if ((ch >> 7) == 0b0) {
                continue;
            }
            if ((ch >> 5) == 0b110) {
                continuation = 1;
                continue;
            }
            if ((ch >> 4) == 0b1110) {
                continuation = 2;
                continue;
            }
            if ((ch >> 3) == 0b11110) {
                continuation = 3;
                continue;
            }
            return false;
        }

        if ((ch >> 6) != 0b10) {
            return false;
        }
        --continuation;
    }
    return continuation == 0;
}

std::wstring DecodeToWide(const std::string& bytes) {
    if (bytes.size() >= 3 &&
        static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB &&
        static_cast<unsigned char>(bytes[2]) == 0xBF) {
        return MultiByteToWide(bytes.substr(3), CP_UTF8);
    }

    if (bytes.size() >= 2 &&
        static_cast<unsigned char>(bytes[0]) == 0xFF &&
        static_cast<unsigned char>(bytes[1]) == 0xFE) {
        std::wstring out;
        for (size_t i = 2; i + 1 < bytes.size(); i += 2) {
            wchar_t ch = static_cast<wchar_t>(
                static_cast<unsigned char>(bytes[i]) |
                (static_cast<unsigned char>(bytes[i + 1]) << 8));
            out.push_back(ch);
        }
        return out;
    }

    if (bytes.size() >= 2 &&
        static_cast<unsigned char>(bytes[0]) == 0xFE &&
        static_cast<unsigned char>(bytes[1]) == 0xFF) {
        std::wstring out;
        for (size_t i = 2; i + 1 < bytes.size(); i += 2) {
            wchar_t ch = static_cast<wchar_t>(
                (static_cast<unsigned char>(bytes[i]) << 8) |
                static_cast<unsigned char>(bytes[i + 1]));
            out.push_back(ch);
        }
        return out;
    }

    if (IsValidUtf8(bytes)) {
        std::wstring utf8 = MultiByteToWide(bytes, CP_UTF8);
        if (!utf8.empty()) {
            return utf8;
        }
    }

    std::wstring cp949 = MultiByteToWide(bytes, 949);
    if (!cp949.empty()) {
        return cp949;
    }

    return MultiByteToWide(bytes, CP_ACP);
}

std::wstring DecodeHtmlEntities(std::wstring input) {
    static const std::vector<std::pair<std::wstring, std::wstring>> entities = {
        {L"&nbsp;", L" "},
        {L"&amp;", L"&"},
        {L"&lt;", L"<"},
        {L"&gt;", L">"},
        {L"&quot;", L"\""},
        {L"&#39;", L"'"}
    };

    for (const auto& pair : entities) {
        size_t pos = 0;
        while ((pos = input.find(pair.first, pos)) != std::wstring::npos) {
            input.replace(pos, pair.first.size(), pair.second);
            pos += pair.second.size();
        }
    }

    std::wregex numericEntity(LR"(&#(\d+);)");
    std::wsmatch match;
    std::wstring result;
    std::wstring::const_iterator searchStart(input.cbegin());

    while (std::regex_search(searchStart, input.cend(), match, numericEntity)) {
        result.append(searchStart, match[0].first);
        int code = std::stoi(match[1].str());
        result.push_back(static_cast<wchar_t>(code));
        searchStart = match[0].second;
    }
    result.append(searchStart, input.cend());
    return result;
}

std::wstring NormalizeSubtitleText(std::wstring html) {
    html = std::regex_replace(html, std::wregex(LR"(<br\s*/?>)", std::regex_constants::icase), L"\n");
    html = std::regex_replace(html, std::wregex(LR"(<[^>]+>)"), L"");
    html = DecodeHtmlEntities(html);

    std::wstringstream in(html);
    std::wstring line;
    std::vector<std::wstring> lines;

    while (std::getline(in, line)) {
        std::wstring trimmed = Trim(line);
        if (!trimmed.empty()) {
            lines.push_back(trimmed);
        }
    }

    std::wstring out;
    for (size_t i = 0; i < lines.size(); ++i) {
        out += lines[i];
        if (i + 1 < lines.size()) {
            out += L"\n";
        }
    }
    return out;
}

// Parse a CSS/HTML color value (#RRGGBB, #RGB, or named color) into ASS &HBBGGRR& format.
std::wstring ParseColorToAss(const std::wstring& colorStr) {
    std::wstring s = Trim(colorStr);
    if (s.size() >= 2 &&
        (s.front() == L'"' || s.front() == L'\'') &&
        s.back() == s.front()) {
        s = s.substr(1, s.size() - 2);
    }
    s = Trim(s);
    std::wstring lower = ToLowerW(s);

    // Real-world SMI files frequently omit the leading '#' on a hex triplet
    // (e.g. <font color=CCCCFF>) even though it's technically required CSS —
    // treat a bare 3/6-digit hex run the same as one prefixed with '#' so
    // that color isn't silently dropped.
    std::wstring hexCandidate;
    bool isHex = false;
    if (!lower.empty() && lower[0] == L'#') {
        hexCandidate = lower.substr(1);
        isHex = true;
    } else if (lower.size() == 3 || lower.size() == 6) {
        isHex = std::all_of(lower.begin(), lower.end(), [](wchar_t ch) {
            return std::iswxdigit(ch) != 0;
        });
        if (isHex) {
            hexCandidate = lower;
        }
    }

    if (isHex) {
        const std::wstring& hex = hexCandidate;
        int r = 0, g = 0, b = 0;
        try {
            if (hex.size() == 6) {
                r = std::stoi(hex.substr(0, 2), nullptr, 16);
                g = std::stoi(hex.substr(2, 2), nullptr, 16);
                b = std::stoi(hex.substr(4, 2), nullptr, 16);
            } else if (hex.size() == 3) {
                int r1 = std::stoi(std::wstring(1, hex[0]), nullptr, 16);
                int g1 = std::stoi(std::wstring(1, hex[1]), nullptr, 16);
                int b1 = std::stoi(std::wstring(1, hex[2]), nullptr, 16);
                r = r1 * 17; g = g1 * 17; b = b1 * 17;
            } else {
                return L"";
            }
        } catch (...) { return L""; }
        wchar_t buf[24];
        swprintf_s(buf, L"&H%02X%02X%02X&", b, g, r);
        return buf;
    }

    static const std::map<std::wstring, std::tuple<int, int, int>> namedColors = {
        {L"white",          {255, 255, 255}}, {L"yellow",        {255, 255,   0}},
        {L"red",            {255,   0,   0}}, {L"lime",          {  0, 255,   0}},
        {L"green",          {  0, 128,   0}}, {L"blue",          {  0,   0, 255}},
        {L"cyan",           {  0, 255, 255}}, {L"aqua",          {  0, 255, 255}},
        {L"magenta",        {255,   0, 255}}, {L"fuchsia",       {255,   0, 255}},
        {L"black",          {  0,   0,   0}}, {L"gray",          {128, 128, 128}},
        {L"grey",           {128, 128, 128}}, {L"silver",        {192, 192, 192}},
        {L"orange",         {255, 165,   0}}, {L"pink",          {255, 192, 203}},
        {L"purple",         {128,   0, 128}}, {L"maroon",        {128,   0,   0}},
        {L"olive",          {128, 128,   0}}, {L"navy",          {  0,   0, 128}},
        {L"teal",           {  0, 128, 128}},
        // Extended CSS named colors commonly used in SMI files
        {L"skyblue",        {135, 206, 235}}, {L"lightblue",     {173, 216, 230}},
        {L"deepskyblue",    {  0, 191, 255}}, {L"dodgerblue",    { 30, 144, 255}},
        {L"cornflowerblue", {100, 149, 237}}, {L"royalblue",     { 65, 105, 225}},
        {L"steelblue",      { 70, 130, 180}}, {L"cadetblue",     { 95, 158, 160}},
        {L"darkblue",       {  0,   0, 139}}, {L"mediumblue",    {  0,   0, 205}},
        {L"lightgreen",     {144, 238, 144}}, {L"limegreen",     { 50, 205,  50}},
        {L"forestgreen",    { 34, 139,  34}}, {L"darkgreen",     {  0, 100,   0}},
        {L"seagreen",       { 46, 139,  87}}, {L"mediumseagreen",{ 60, 179, 113}},
        {L"springgreen",    {  0, 255, 127}}, {L"palegreen",     {152, 251, 152}},
        {L"darkseagreen",   {143, 188, 143}},
        {L"lightyellow",    {255, 255, 224}}, {L"gold",          {255, 215,   0}},
        {L"khaki",          {240, 230, 140}}, {L"darkkhaki",     {189, 183, 107}},
        {L"lightpink",      {255, 182, 193}}, {L"hotpink",       {255, 105, 180}},
        {L"deeppink",       {255,  20, 147}}, {L"coral",         {255, 127,  80}},
        {L"salmon",         {250, 128, 114}}, {L"tomato",        {255,  99,  71}},
        {L"orangered",      {255,  69,   0}}, {L"darkorange",    {255, 140,   0}},
        {L"crimson",        {220,  20,  60}}, {L"darkred",       {139,   0,   0}},
        {L"violet",         {238, 130, 238}}, {L"orchid",        {218, 112, 214}},
        {L"plum",           {221, 160, 221}}, {L"indigo",        { 75,   0, 130}},
        {L"mediumpurple",   {147, 112, 219}}, {L"blueviolet",    {138,  43, 226}},
        {L"darkorchid",     {153,  50, 204}}, {L"darkviolet",    {148,   0, 211}},
        {L"turquoise",      { 64, 224, 208}}, {L"mediumturquoise",{72, 209, 204}},
        {L"darkturquoise",  {  0, 206, 209}}, {L"lightcyan",     {224, 255, 255}},
        {L"lightgray",      {211, 211, 211}}, {L"lightgrey",     {211, 211, 211}},
        {L"darkgray",       {169, 169, 169}}, {L"darkgrey",      {169, 169, 169}},
        {L"dimgray",        {105, 105, 105}}, {L"dimgrey",       {105, 105, 105}},
        {L"gainsboro",      {220, 220, 220}}, {L"whitesmoke",    {245, 245, 245}},
        {L"brown",          {165,  42,  42}}, {L"sienna",        {160,  82,  45}},
        {L"chocolate",      {210, 105,  30}}, {L"tan",           {210, 180, 140}},
        {L"wheat",          {245, 222, 179}}, {L"beige",         {245, 245, 220}},
        {L"lavender",       {230, 230, 250}}, {L"thistle",       {216, 191, 216}},
        {L"ivory",          {255, 255, 240}}, {L"snow",          {255, 250, 250}},
    };
    auto it = namedColors.find(lower);
    if (it != namedColors.end()) {
        auto& [r, g, b] = it->second;
        wchar_t buf[24];
        swprintf_s(buf, L"&H%02X%02X%02X&", b, g, r);
        return buf;
    }
    return L"";
}

// Extract the value of a named attribute from a raw HTML tag content string.
std::wstring ExtractTagAttr(const std::wstring& tagContent, const std::wstring& attrName) {
    std::wregex re(attrName + LR"ATTR(\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s>]*)))ATTR",
                   std::regex_constants::icase);
    std::wsmatch m;
    if (std::regex_search(tagContent, m, re)) {
        if (m[1].matched) return m[1].str();
        if (m[2].matched) return m[2].str();
        return m[3].str();
    }
    return L"";
}

// Convert SMI inner-HTML (inside a <p> block) to ASS dialogue text,
// mapping <b>, <i>, <u>, <s>/<strike>, <font color/size>, and <br> to
// the corresponding ASS override codes.
std::wstring SmiHtmlToAssText(const std::wstring& html, int baseFontSize) {
    std::wstring out;
    out.reserve(html.size());
    size_t pos = 0;

    // Track font-tag nesting so we can restore previous state on </font>.
    struct FontFrame { bool hadColor; bool hadSize; };
    std::vector<FontFrame> fontStack;

    // Count open decoration tags so we can close any unclosed ones at the end.
    int boldOpen = 0, italicOpen = 0, underOpen = 0, strikeOpen = 0;
    int colorOpen = 0; // counts open font tags that set a color

    while (pos < html.size()) {
        if (html[pos] != L'<') {
            out.push_back(html[pos]);
            ++pos;
            continue;
        }

        size_t closePos = html.find(L'>', pos);
        if (closePos == std::wstring::npos) {
            // Unclosed '<', emit as-is and stop
            out += html.substr(pos);
            break;
        }

        std::wstring tagContent = html.substr(pos + 1, closePos - pos - 1);
        // Strip optional trailing slash (self-closing)
        std::wstring tagLower = ToLowerW(Trim(tagContent));
        if (!tagLower.empty() && tagLower.back() == L'/') {
            tagLower.pop_back();
            tagLower = Trim(tagLower);
        }
        pos = closePos + 1;

        // <br> / <br /> / <br/> → line-break
        if (tagLower == L"br" ||
            tagLower.rfind(L"br ", 0) == 0 ||
            tagLower.rfind(L"br\t", 0) == 0) {
            out += L"\\N";
            continue;
        }

        // <b>  </b>
        if (tagLower == L"b")  { out += L"{\\b1}"; ++boldOpen;   continue; }
        if (tagLower == L"/b") { if (boldOpen   > 0) { out += L"{\\b0}"; --boldOpen;   } continue; }

        // <i>  </i>
        if (tagLower == L"i")  { out += L"{\\i1}"; ++italicOpen; continue; }
        if (tagLower == L"/i") { if (italicOpen > 0) { out += L"{\\i0}"; --italicOpen; } continue; }

        // <u>  </u>
        if (tagLower == L"u")  { out += L"{\\u1}"; ++underOpen;  continue; }
        if (tagLower == L"/u") { if (underOpen  > 0) { out += L"{\\u0}"; --underOpen;  } continue; }

        // <s> <strike>  </s> </strike>
        if (tagLower == L"s" || tagLower == L"strike")   { out += L"{\\s1}"; ++strikeOpen; continue; }
        if (tagLower == L"/s" || tagLower == L"/strike") { if (strikeOpen > 0) { out += L"{\\s0}"; --strikeOpen; } continue; }

        // <font ...>
        if (tagLower.rfind(L"font", 0) == 0 &&
            (tagLower.size() == 4 || std::iswspace(tagLower[4]))) {
            FontFrame frame{false, false};

            std::wstring colorAttr = ExtractTagAttr(tagContent, L"color");
            if (!colorAttr.empty()) {
                std::wstring assColor = ParseColorToAss(colorAttr);
                if (!assColor.empty()) {
                    out += L"{\\c" + assColor + L"}";
                    frame.hadColor = true;
                    ++colorOpen;
                }
            }

            std::wstring sizeAttr = ExtractTagAttr(tagContent, L"size");
            if (!sizeAttr.empty()) {
                try {
                    int sz = std::stoi(sizeAttr);
                    // HTML font size 1-7 → approximate pixel sizes
                    static const int htmlSizePx[] = {8, 10, 12, 14, 18, 24, 24};
                    if (sz >= 1 && sz <= 7) sz = htmlSizePx[sz - 1];
                    if (sz > 0) {
                        wchar_t buf[24];
                        swprintf_s(buf, L"{\\fs%d}", sz);
                        out += buf;
                        frame.hadSize = true;
                    }
                } catch (...) {}
            }

            fontStack.push_back(frame);
            continue;
        }

        // </font>
        if (tagLower == L"/font") {
            if (!fontStack.empty()) {
                FontFrame& frame = fontStack.back();
                if (frame.hadColor && colorOpen > 0) {
                    out += L"{\\c&H00FFFFFF&}"; // reset to default white
                    --colorOpen;
                }
                if (frame.hadSize) {
                    out += L"{\\fs" + std::to_wstring(baseFontSize) + L"}"; // reset to default size
                }
                fontStack.pop_back();
            }
            continue;
        }

        // All other tags (e.g. <p>, <span>, <SYNC>, etc.) are silently dropped.
    }

    // Close any unclosed formatting tags
    for (int i = 0; i < boldOpen;   ++i) out += L"{\\b0}";
    for (int i = 0; i < italicOpen; ++i) out += L"{\\i0}";
    for (int i = 0; i < underOpen;  ++i) out += L"{\\u0}";
    for (int i = 0; i < strikeOpen; ++i) out += L"{\\s0}";
    for (int i = 0; i < colorOpen;  ++i) out += L"{\\c&H00FFFFFF&}";

    // Decode HTML entities
    out = DecodeHtmlEntities(out);

    // Replace literal CR/LF with \N and normalise lines
    std::wstring withLineBreaks;
    withLineBreaks.reserve(out.size());
    for (size_t i = 0; i < out.size(); ++i) {
        if (out[i] == L'\r') {
            withLineBreaks += L"\\N";
            if (i + 1 < out.size() && out[i + 1] == L'\n') ++i;
        } else if (out[i] == L'\n') {
            withLineBreaks += L"\\N";
        } else {
            withLineBreaks.push_back(out[i]);
        }
    }

    // Split on \N, trim each segment, drop empties, rejoin
    std::vector<std::wstring> segments;
    size_t start = 0;
    size_t found = 0;
    while ((found = withLineBreaks.find(L"\\N", start)) != std::wstring::npos) {
        segments.push_back(Trim(withLineBreaks.substr(start, found - start)));
        start = found + 2;
    }
    segments.push_back(Trim(withLineBreaks.substr(start)));

    std::wstring result;
    for (const auto& seg : segments) {
        if (seg.empty()) continue;
        if (!result.empty()) result += L"\\N";
        result += seg;
    }
    return result;
}

std::wstring NormalizeForCreditDetection(const std::wstring& text) {
    std::wstring lowered = ToLowerW(text);
    lowered = std::regex_replace(lowered, std::wregex(LR"([^0-9a-z가-힣]+)"), L" ");
    return Trim(lowered);
}

bool IsCreatorCreditCaption(const std::wstring& text) {
    std::wstring normalized = NormalizeForCreditDetection(text);
    if (normalized.empty()) {
        return false;
    }

    // Email addresses are almost always a feedback contact left by the
    // subtitle author, never real dialogue. Check the lightly-trimmed
    // original text so punctuation (@, .) survives.
    std::wstring rawLower = ToLowerW(Trim(text));
    static const std::wregex emailPattern(LR"([a-z0-9._%+-]+@[a-z0-9.-]+\.[a-z]{2,})", std::regex_constants::icase);
    if (std::regex_search(rawLower, emailPattern)) {
        return true;
    }

    static const std::vector<std::wstring> creditTokens = {
        L"smi by",
        L"sub by",
        L"subtitle by",
        L"sync by",
        L"sync correction by",
        L"sync corrections by",
        L"correction by",
        L"corrections by",
        L"provided by",
        L"modify by",
        L"modified by",
        L"converted by",
        L"conversion by",
        L"encoded by",
        L"edited by",
        L"ripped by",
        L"downloaded from",
        L"download from",
        L"ripped from",
        L"번역 by",
        L"자막 by",
        L"제작 by",
        L"싱크 by",
        L"수정 by",
        L"변환 by",
        L"제공 by",
        L"자막제작",
        L"한글자막"
    };

    for (const auto& token : creditTokens) {
        if (normalized.find(token) != std::wstring::npos) {
            return true;
        }
    }

    static const std::vector<std::wregex> creditPatterns = {
        std::wregex(LR"(\b(sync|timing|correction|corrections|modify|modified|provided|converted|conversion|subtitle|encode|encoded|edit|edited|rip|ripped)\s+by\b)"),
        std::wregex(LR"(\b(제작|수정|변환|싱크|제공|번역)\s*by\b)")
    };

    for (const auto& pattern : creditPatterns) {
        if (std::regex_search(normalized, pattern)) {
            return true;
        }
    }

    // Lines that open with a "자막:"/"번역:"/"subtitle:" style label are
    // almost always a standalone credit line, not dialogue.
    static const std::wregex labelColonPattern(
        LR"(^\s*(자막|번역|제작|싱크|수정|변환|제공|sub|smi|srt|ass|subtitle|translat\w*|encode\w*|sync|rip\w*)\s*[:：])",
        std::regex_constants::icase);
    if (std::regex_search(rawLower, labelColonPattern)) {
        return true;
    }

    // Short lines combining two credit-action words without "by" or a colon
    // (e.g. "SUB 변환 Jone Dow") are also very likely to be a credit line.
    if (normalized.size() <= 40) {
        static const std::vector<std::wstring> actionTokens = {
            L"변환", L"제작", L"수정", L"싱크", L"번역", L"자막", L"제공", L"전달",
            L"sub", L"smi", L"srt", L"ass", L"subtitle", L"convert", L"conversion",
            L"encode", L"encoded", L"sync", L"translat", L"rip"
        };
        int hits = 0;
        for (const auto& token : actionTokens) {
            if (normalized.find(token) != std::wstring::npos) {
                ++hits;
            }
        }
        if (hits >= 2) {
            return true;
        }
    }

    return false;
}

void RemoveCreditCaptions(std::vector<Caption>& captions) {
    if (captions.empty()) {
        return;
    }

    // Remove credits from the front
    size_t begin = 0;
    while (begin < captions.size() && IsCreatorCreditCaption(captions[begin].text)) {
        ++begin;
    }

    // Remove credits from the back
    size_t end = captions.size();
    while (end > begin && IsCreatorCreditCaption(captions[end - 1].text)) {
        --end;
    }

    // Keep only non-credit captions (front to back boundaries)
    if (begin > 0 || end < captions.size()) {
        captions = std::vector<Caption>(captions.begin() + begin, captions.begin() + end);
    }
}

bool IsSubtitleExtension(const std::wstring& extension) {
    std::wstring ext = ToLowerW(extension);
    return ext == L".smi" || ext == L".srt" || ext == L".ass";
}

int ParseSrtTimestamp(const std::wstring& value) {
    std::wsmatch match;
    if (!std::regex_match(value, match, std::wregex(LR"((\d{2}):(\d{2}):(\d{2}),(\d{3}))"))) {
        return -1;
    }

    int hh = std::stoi(match[1].str());
    int mm = std::stoi(match[2].str());
    int ss = std::stoi(match[3].str());
    int ms = std::stoi(match[4].str());
    return (((hh * 60) + mm) * 60 + ss) * 1000 + ms;
}

int ParseAssTimestamp(const std::wstring& value) {
    std::wsmatch match;
    if (!std::regex_match(value, match, std::wregex(LR"((\d+):(\d{2}):(\d{2})\.(\d{2}))"))) {
        return -1;
    }

    int hh = std::stoi(match[1].str());
    int mm = std::stoi(match[2].str());
    int ss = std::stoi(match[3].str());
    int cs = std::stoi(match[4].str());
    return (((hh * 60) + mm) * 60 + ss) * 1000 + (cs * 10);
}

std::wstring FormatTimestampAss(int totalMs) {
    if (totalMs < 0) {
        totalMs = 0;
    }

    int hours = totalMs / 2400000;
    totalMs %= 2400000;
    int minutes = totalMs / 60000;
    totalMs %= 60000;
    int seconds = totalMs / 1000;
    int centis = (totalMs % 1000) / 10;

    wchar_t buffer[32] = {};
    swprintf_s(buffer, L"%d:%02d:%02d.%02d", hours, minutes, seconds, centis);
    return buffer;
}

std::wstring NormalizePlainSubtitleText(const std::wstring& value) {
    std::wstringstream in(value);
    std::wstring line;
    std::vector<std::wstring> lines;

    while (std::getline(in, line)) {
        std::wstring trimmed = Trim(line);
        if (!trimmed.empty()) {
            lines.push_back(trimmed);
        }
    }

    std::wstring out;
    for (size_t i = 0; i < lines.size(); ++i) {
        out += lines[i];
        if (i + 1 < lines.size()) {
            out += L"\n";
        }
    }
    return out;
}

std::wstring StripAssOverrides(const std::wstring& text) {
    std::wstring out = std::regex_replace(text, std::wregex(LR"(\{[^}]*\})"), L"");
    size_t pos = 0;
    while ((pos = out.find(L"\\N", pos)) != std::wstring::npos) {
        out.replace(pos, 2, L"\n");
    }
    pos = 0;
    while ((pos = out.find(L"\\n", pos)) != std::wstring::npos) {
        out.replace(pos, 2, L"\n");
    }
    return NormalizePlainSubtitleText(out);
}

std::wstring EscapeHtml(const std::wstring& input) {
    std::wstring out;
    out.reserve(input.size() + 16);
    for (wchar_t ch : input) {
        if (ch == L'&') {
            out += L"&amp;";
        } else if (ch == L'<') {
            out += L"&lt;";
        } else if (ch == L'>') {
            out += L"&gt;";
        } else if (ch == L'\"') {
            out += L"&quot;";
        } else {
            out.push_back(ch);
        }
    }
    return out;
}

std::wstring ToSmiText(const std::wstring& plainText) {
    std::wstring escaped = EscapeHtml(plainText);
    size_t pos = 0;
    while ((pos = escaped.find(L"\n", pos)) != std::wstring::npos) {
        escaped.replace(pos, 1, L"<br>");
        pos += 4;
    }
    return escaped;
}

std::vector<std::wstring> SplitAssCsv(const std::wstring& text, size_t expectedFields) {
    std::vector<std::wstring> fields;
    fields.reserve(expectedFields);

    std::wstring current;
    for (size_t i = 0; i < text.size(); ++i) {
        wchar_t ch = text[i];
        if (ch == L',' && fields.size() + 1 < expectedFields) {
            fields.push_back(current);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    fields.push_back(current);
    return fields;
}

int ResolveCaptionEndMs(const std::vector<Caption>& items, size_t index) {
    int startMs = std::max(0, items[index].startMs);
    if (items[index].endMs > startMs) {
        return items[index].endMs;
    }

    int endMs = startMs + 2000;
    if (index + 1 < items.size()) {
        endMs = std::max(startMs + 300, items[index + 1].startMs - 1);
    }
    return endMs;
}

bool IsVideoExtension(const std::wstring& extension) {
    static const std::set<std::wstring> videoExts = {
        L".mp4", L".mkv", L".avi", L".mov", L".wmv", L".m4v", L".ts", L".m2ts", L".webm", L".mpg", L".mpeg"
    };
    return videoExts.find(ToLowerW(extension)) != videoExts.end();
}

std::wstring NormalizeNameForMatch(const std::wstring& value) {
    std::wstring lowered = ToLowerW(value);
    return std::regex_replace(lowered, std::wregex(LR"([^0-9a-z]+)"), L" ");
}

std::vector<std::wstring> ExtractMeaningfulTokens(const std::wstring& stem) {
    static const std::set<std::wstring> noise = {
        L"1080p", L"2160p", L"720p", L"480p", L"x264", L"x265", L"h264", L"h265", L"hevc", L"av1",
        L"webrip", L"web", L"webdl", L"bluray", L"brrip", L"dvdrip", L"hdr", L"uhd", L"10bit", L"8bit",
        L"aac", L"dts", L"truehd", L"atmos", L"proper", L"repack", L"remux", L"yts", L"rarbg"
    };

    std::wstring normalized = NormalizeNameForMatch(stem);
    std::wstringstream in(normalized);
    std::wstring token;
    std::vector<std::wstring> out;

    while (in >> token) {
        if (token.size() <= 1) {
            continue;
        }
        if (noise.find(token) != noise.end()) {
            continue;
        }
        out.push_back(token);
    }
    return out;
}

int ExtractYear(const std::wstring& stem) {
    std::wsmatch match;
    std::wstring normalized = NormalizeNameForMatch(stem);
    if (std::regex_search(normalized, match, std::wregex(LR"((19\d{2}|20\d{2}))"))) {
        return std::stoi(match[1].str());
    }
    return -1;
}

int ExtractEpisode(const std::wstring& stem) {
    std::wstring normalized = NormalizeNameForMatch(stem);
    std::wsmatch match;

    if (std::regex_search(normalized, match, std::wregex(LR"(\bs\d{1,2}\s*e\s*(\d{1,3})\b)"))) {
        return std::stoi(match[1].str());
    }
    if (std::regex_search(normalized, match, std::wregex(LR"(\bep\s*(\d{1,3})\b)"))) {
        return std::stoi(match[1].str());
    }
    if (std::regex_search(normalized, match, std::wregex(LR"(\be\s*(\d{1,3})\b)"))) {
        return std::stoi(match[1].str());
    }
    return -1;
}

// Every run of ASCII digits in the normalized stem, left to right, as
// integers (e.g. "Show.05.1080p" -> {5, 1080}). Used to infer episode
// numbers from filenames that carry a bare digit with no S/E/EP marker.
std::vector<int> ExtractNumberSequence(const std::wstring& stem) {
    std::wstring normalized = NormalizeNameForMatch(stem);
    std::vector<int> numbers;
    static const std::wregex digitsRe(LR"(\d+)");
    std::wsregex_iterator it(normalized.begin(), normalized.end(), digitsRe);
    std::wsregex_iterator end;
    for (; it != end; ++it) {
        try {
            numbers.push_back(std::stoi(it->str()));
        } catch (...) {
            // Digit run too long to fit an int (essentially never happens
            // for filenames) — treat as unusable rather than throwing.
        }
    }
    return numbers;
}

// Case-insensitive, separator-normalized key so paths gathered from two
// different directory scans (or an argv path vs. a directory_iterator path)
// compare equal even if their casing or exact string form differs.
std::wstring PathKey(const fs::path& p) {
    std::error_code ec;
    fs::path canon = fs::weakly_canonical(p, ec);
    return ToLowerW((ec ? p : canon).wstring());
}

// Given a complete, aligned batch of files that share a common naming
// template (e.g. every subtitle in one folder, or every video in one
// folder), tries to find the one numeric "column" in their filenames that
// encodes the episode number, and returns each file's inferred number.
//
// The columns are aligned by position: the k-th digit-run in each filename
// is compared against the k-th digit-run of every other file. A column
// qualifies as "the episode number" only if, across every file in the
// batch, its values are pairwise distinct and form a run of exactly |m|
// consecutive integers (in any order, at any starting value — episodes 05
// through 10 qualify just as well as 01 through 06). This intentionally
// does not require the run to start at 1 or stay within [1, m]: a partial
// season selected from the middle (e.g. 05~10 out of a 20-episode show)
// still produces a valid consecutive run, just not one anchored at 1.
// Resolution/year/bitrate columns are excluded because they hold the same
// value in every filename (constant, not consecutive) — not because their
// value is "too large". The left-most qualifying column wins. Returns an
// empty map if the filenames don't share the same digit-run "shape" or no
// column qualifies.
std::map<std::wstring, int> InferBatchEpisodeNumbers(const std::vector<fs::path>& files) {
    std::map<std::wstring, int> result;
    size_t m = files.size();
    if (m < 2) {
        return result; // nothing to disambiguate against with a single file
    }

    std::vector<std::vector<int>> perFileNumbers;
    perFileNumbers.reserve(m);
    size_t columnCount = SIZE_MAX;
    for (const auto& f : files) {
        std::vector<int> nums = ExtractNumberSequence(f.stem().wstring());
        if (columnCount == SIZE_MAX) {
            columnCount = nums.size();
        } else if (nums.size() != columnCount) {
            return result; // filenames don't share a common numeric "shape"
        }
        perFileNumbers.push_back(std::move(nums));
    }
    if (columnCount == 0 || columnCount == SIZE_MAX) {
        return result;
    }

    for (size_t col = 0; col < columnCount; ++col) {
        std::vector<int> values;
        values.reserve(m);
        for (const auto& nums : perFileNumbers) {
            values.push_back(nums[col]);
        }

        std::vector<int> sorted = values;
        std::sort(sorted.begin(), sorted.end());
        bool isConsecutiveRun = true;
        for (size_t i = 1; i < sorted.size(); ++i) {
            if (sorted[i] != sorted[i - 1] + 1) { isConsecutiveRun = false; break; }
        }
        if (!isConsecutiveRun) continue;

        for (size_t i = 0; i < m; ++i) {
            result[PathKey(files[i])] = values[i];
        }
        return result; // first qualifying column wins (left-to-right)
    }

    return result; // no column looked like an episode-number sequence
}

// Explicit S/E, EP, or E markers are trusted first; a bare digit run is only
// used when no marker exists, and only if the batch-wide inference above
// already resolved this exact file (see BuildBareEpisodeNumberMap).
int ResolveEpisodeNumber(const fs::path& path, const std::map<std::wstring, int>& bareEpisodes) {
    int explicitEp = ExtractEpisode(path.stem().wstring());
    if (explicitEp > 0) {
        return explicitEp;
    }
    auto it = bareEpisodes.find(PathKey(path));
    return (it != bareEpisodes.end()) ? it->second : -1;
}

int ComputeNameScore(const fs::path& subtitlePath, const fs::path& videoPath,
                      const std::map<std::wstring, int>& bareEpisodes) {
    std::wstring subStem = ToLowerW(subtitlePath.stem().wstring());
    std::wstring vidStem = ToLowerW(videoPath.stem().wstring());
    if (subStem == vidStem) {
        return 1000;
    }

    int score = 0;

    int subYear = ExtractYear(subtitlePath.stem().wstring());
    int vidYear = ExtractYear(videoPath.stem().wstring());
    if (subYear > 0 && vidYear > 0) {
        score += (subYear == vidYear) ? 120 : -80;
    }

    int subEp = ResolveEpisodeNumber(subtitlePath, bareEpisodes);
    int vidEp = ResolveEpisodeNumber(videoPath, bareEpisodes);
    if (subEp > 0 && vidEp > 0) {
        score += (subEp == vidEp) ? 180 : -120;
    }

    std::set<std::wstring> subTokens;
    for (const auto& token : ExtractMeaningfulTokens(subtitlePath.stem().wstring())) {
        subTokens.insert(token);
    }

    std::set<std::wstring> vidTokens;
    for (const auto& token : ExtractMeaningfulTokens(videoPath.stem().wstring())) {
        vidTokens.insert(token);
    }

    int overlap = 0;
    for (const auto& token : subTokens) {
        if (vidTokens.find(token) != vidTokens.end()) {
            ++overlap;
        }
    }
    score += overlap * 12;

    if (overlap == 0 && subYear < 0 && subEp < 0) {
        score -= 30;
    }

    return score;
}

// Returns the path of the video file in the same folder that best matches
// |subtitlePath|'s name, or an empty path if none/no confident match exists.
// |bareEpisodes| is the batch-wide marker-less episode map (see
// BuildBareEpisodeNumberMap); pass an empty map when it isn't available.
fs::path ResolveMatchedVideoPath(const fs::path& subtitlePath,
                                  const std::map<std::wstring, int>& bareEpisodes = {}) {
    std::vector<fs::path> videos;
    try {
        for (const auto& entry : fs::directory_iterator(subtitlePath.parent_path())) {
            if (!entry.is_regular_file()) {
                continue;
            }
            fs::path candidate = entry.path();
            if (IsVideoExtension(candidate.extension())) {
                videos.push_back(candidate);
            }
        }
    } catch (...) {
        return fs::path();
    }

    if (videos.empty()) {
        return fs::path();
    }
    if (videos.size() == 1) {
        return videos[0];
    }

    int bestScore = -999999;
    int secondScore = -999999;
    fs::path bestPath;

    for (const auto& video : videos) {
        int score = ComputeNameScore(subtitlePath, video, bareEpisodes);
        if (score > bestScore) {
            secondScore = bestScore;
            bestScore = score;
            bestPath = video;
        } else if (score > secondScore) {
            secondScore = score;
        }
    }

    if (bestScore >= 24 && (bestScore - secondScore >= 12 || secondScore < 0)) {
        return bestPath;
    }

    return fs::path();
}

std::wstring ResolveMatchedVideoStem(const fs::path& subtitlePath) {
    fs::path matched = ResolveMatchedVideoPath(subtitlePath);
    return matched.empty() ? subtitlePath.stem().wstring() : matched.stem().wstring();
}

// Builds the batch-wide marker-less episode map used as a ComputeNameScore()
// fallback for filenames with a bare episode digit and no S/E/EP marker
// (e.g. "쇼 - 05.mkv" matched against "쇼.05.smi").
//
// |batchFiles| is the whole multi-select batch (already aggregated by the
// collector), grouped here by parent directory. For each directory:
//  - Subtitles: only inferred when the batch accounts for *every* subtitle
//    in that directory — inferring on a partial selection would make the
//    "numbers form one consecutive run" assumption misfire (e.g. selecting
//    episodes 3 and 4 out of 10 looks like a run of length 2, not 10).
//  - Videos: inferred unconditionally from whatever videos exist there,
//    independent of the subtitle batch size (n:n / n:m are both fine).
std::map<std::wstring, int> BuildBareEpisodeNumberMap(const std::vector<fs::path>& batchFiles) {
    std::map<std::wstring, int> result;

    std::map<fs::path, std::vector<fs::path>> selectedSubsByDir;
    for (const auto& f : batchFiles) {
        selectedSubsByDir[f.parent_path()].push_back(f);
    }

    for (const auto& [dir, selectedSubs] : selectedSubsByDir) {
        std::vector<fs::path> allSubsInDir;
        std::vector<fs::path> allVideosInDir;
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file()) continue;
            fs::path p = entry.path();
            if (IsSubtitleExtension(p.extension().wstring())) {
                allSubsInDir.push_back(p);
            } else if (IsVideoExtension(p.extension())) {
                allVideosInDir.push_back(p);
            }
        }
        if (ec) continue;

        if (selectedSubs.size() == allSubsInDir.size()) {
            auto subEpisodes = InferBatchEpisodeNumbers(allSubsInDir);
            result.insert(subEpisodes.begin(), subEpisodes.end());
        }

        auto videoEpisodes = InferBatchEpisodeNumbers(allVideosInDir);
        result.insert(videoEpisodes.begin(), videoEpisodes.end());
    }

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Video pixel-dimension detection (no external libraries).
// Supports ISO-BMFF (mp4/mov/m4v) via the 'tkhd' box and Matroska/WebM
// (mkv/webm) via EBML PixelWidth/PixelHeight, read directly from the file
// header without decoding video data.
// ─────────────────────────────────────────────────────────────────────────────

bool ReadExactBytes(HANDLE h, void* buffer, DWORD length) {
    DWORD readBytes = 0;
    return ReadFile(h, buffer, length, &readBytes, nullptr) && readBytes == length;
}

bool SeekAbsolute(HANDLE h, uint64_t pos) {
    LARGE_INTEGER li;
    li.QuadPart = static_cast<LONGLONG>(pos);
    return SetFilePointerEx(h, li, nullptr, FILE_BEGIN) != 0;
}

uint32_t ReadBigEndian32(const unsigned char* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

// Recursively walks ISO-BMFF boxes looking for the first video track's
// pixel dimensions, taken from that track's 'tkhd' box (width/height are
// 16.16 fixed-point; audio tracks report 0x0 so they're skipped naturally).
bool FindMp4VideoSize(HANDLE h, uint64_t start, uint64_t end, int depth, int& outW, int& outH) {
    if (depth > 8) {
        return false;
    }

    uint64_t pos = start;
    while (pos + 8 <= end) {
        if (!SeekAbsolute(h, pos)) return false;
        unsigned char hdr[8];
        if (!ReadExactBytes(h, hdr, 8)) return false;

        uint64_t boxSize = ReadBigEndian32(hdr);
        char type[5] = {0};
        std::memcpy(type, hdr + 4, 4);
        uint64_t headerLen = 8;

        if (boxSize == 1) {
            unsigned char ext[8];
            if (!ReadExactBytes(h, ext, 8)) return false;
            boxSize = (static_cast<uint64_t>(ReadBigEndian32(ext)) << 32) | ReadBigEndian32(ext + 4);
            headerLen = 16;
        } else if (boxSize == 0) {
            boxSize = end - pos;
        }

        if (boxSize < headerLen || pos + boxSize > end) {
            break;
        }

        uint64_t contentStart = pos + headerLen;
        uint64_t contentEnd = pos + boxSize;
        std::string boxType(type);

        if (boxType == "moov" || boxType == "trak" || boxType == "mdia" ||
            boxType == "minf" || boxType == "stbl") {
            if (FindMp4VideoSize(h, contentStart, contentEnd, depth + 1, outW, outH)) {
                return true;
            }
        } else if (boxType == "tkhd") {
            unsigned char versionFlags[4];
            if (SeekAbsolute(h, contentStart) && ReadExactBytes(h, versionFlags, 4)) {
                int version = versionFlags[0];
                uint64_t widthOffset = contentStart +
                    (version == 1 ? (4 + 32 + 8 + 8 + 36) : (4 + 20 + 8 + 8 + 36));
                unsigned char whBytes[8];
                if (SeekAbsolute(h, widthOffset) && ReadExactBytes(h, whBytes, 8)) {
                    int w = static_cast<int>(ReadBigEndian32(whBytes) >> 16);
                    int trackH = static_cast<int>(ReadBigEndian32(whBytes + 4) >> 16);
                    if (w > 0 && trackH > 0) {
                        outW = w;
                        outH = trackH;
                        return true;
                    }
                }
            }
        }

        pos += boxSize;
    }

    return false;
}

// Reads an EBML variable-length integer at |pos|, advancing it past the
// element. |keepMarker| controls whether the length-marker bit stays in the
// returned value (required for element IDs, stripped for size fields).
bool ReadEbmlVint(HANDLE h, uint64_t& pos, uint64_t end, bool keepMarker, uint64_t& value, int& lenOut) {
    if (pos >= end || !SeekAbsolute(h, pos)) {
        return false;
    }
    unsigned char first;
    if (!ReadExactBytes(h, &first, 1)) return false;

    int len = 0;
    for (int i = 0; i < 8; ++i) {
        if (first & (0x80 >> i)) {
            len = i + 1;
            break;
        }
    }
    if (len == 0 || pos + len > end) {
        return false;
    }

    unsigned char buf[8] = {0};
    buf[0] = first;
    if (len > 1 && !ReadExactBytes(h, buf + 1, static_cast<DWORD>(len - 1))) {
        return false;
    }

    uint64_t v;
    if (keepMarker) {
        v = 0;
        for (int i = 0; i < len; ++i) v = (v << 8) | buf[i];
    } else {
        v = buf[0] & static_cast<unsigned char>(0xFF >> len);
        for (int i = 1; i < len; ++i) v = (v << 8) | buf[i];
    }

    value = v;
    lenOut = len;
    pos += len;
    return true;
}

// Resolves an EBML element's content end offset, treating the reserved
// "all value bits set to 1" size encoding as unknown/unbounded (extends to
// the parent's end, per the Matroska spec).
uint64_t ResolveEbmlContentEnd(uint64_t contentStart, uint64_t size, int sizeLen, uint64_t parentEnd) {
    uint64_t maxVal = (sizeLen >= 8) ? ~0ULL : ((1ULL << (7 * sizeLen)) - 1);
    if (size == maxVal) {
        return parentEnd;
    }
    uint64_t contentEnd = contentStart + size;
    return std::min(contentEnd, parentEnd);
}

constexpr uint64_t kEbmlIdSegment = 0x18538067;
constexpr uint64_t kEbmlIdTracks = 0x1654AE6B;
constexpr uint64_t kEbmlIdTrackEntry = 0xAE;
constexpr uint64_t kEbmlIdTrackType = 0x83;
constexpr uint64_t kEbmlIdVideo = 0xE0;
constexpr uint64_t kEbmlIdPixelWidth = 0xB0;
constexpr uint64_t kEbmlIdPixelHeight = 0xBA;

bool FindMkvVideoSizeInTrackEntry(HANDLE h, uint64_t start, uint64_t end, int& outW, int& outH) {
    uint64_t pos = start;
    bool isVideoTrack = false;
    int width = 0;
    int height = 0;

    while (pos < end) {
        uint64_t id, size;
        int idLen, sizeLen;
        if (!ReadEbmlVint(h, pos, end, true, id, idLen)) break;
        if (!ReadEbmlVint(h, pos, end, false, size, sizeLen)) break;

        uint64_t contentStart = pos;
        uint64_t contentEnd = ResolveEbmlContentEnd(contentStart, size, sizeLen, end);

        if (id == kEbmlIdTrackType) {
            unsigned char v;
            if (SeekAbsolute(h, contentStart) && ReadExactBytes(h, &v, 1)) {
                isVideoTrack = (v == 1);
            }
        } else if (id == kEbmlIdVideo) {
            uint64_t vp = contentStart;
            while (vp < contentEnd) {
                uint64_t vid, vsize;
                int vidLen, vsizeLen;
                if (!ReadEbmlVint(h, vp, contentEnd, true, vid, vidLen)) break;
                if (!ReadEbmlVint(h, vp, contentEnd, false, vsize, vsizeLen)) break;

                if ((vid == kEbmlIdPixelWidth || vid == kEbmlIdPixelHeight) && vsize <= 8) {
                    unsigned char buf[8] = {0};
                    if (SeekAbsolute(h, vp) && ReadExactBytes(h, buf, static_cast<DWORD>(vsize))) {
                        uint64_t val = 0;
                        for (uint64_t i = 0; i < vsize; ++i) val = (val << 8) | buf[i];
                        if (vid == kEbmlIdPixelWidth) width = static_cast<int>(val);
                        else height = static_cast<int>(val);
                    }
                }
                vp += vsize;
            }
        }

        pos = contentEnd;
    }

    if (isVideoTrack && width > 0 && height > 0) {
        outW = width;
        outH = height;
        return true;
    }
    return false;
}

bool FindMkvVideoSize(HANDLE h, uint64_t start, uint64_t end, int& outW, int& outH) {
    uint64_t pos = start;
    while (pos < end) {
        uint64_t id, size;
        int idLen, sizeLen;
        if (!ReadEbmlVint(h, pos, end, true, id, idLen)) break;
        if (!ReadEbmlVint(h, pos, end, false, size, sizeLen)) break;

        uint64_t contentStart = pos;
        uint64_t contentEnd = ResolveEbmlContentEnd(contentStart, size, sizeLen, end);

        if (id == kEbmlIdSegment) {
            if (FindMkvVideoSize(h, contentStart, contentEnd, outW, outH)) {
                return true;
            }
        } else if (id == kEbmlIdTracks) {
            uint64_t tp = contentStart;
            while (tp < contentEnd) {
                uint64_t tid, tsize;
                int tidLen, tsizeLen;
                if (!ReadEbmlVint(h, tp, contentEnd, true, tid, tidLen)) break;
                if (!ReadEbmlVint(h, tp, contentEnd, false, tsize, tsizeLen)) break;

                uint64_t entryEnd = ResolveEbmlContentEnd(tp, tsize, tsizeLen, contentEnd);
                if (tid == kEbmlIdTrackEntry) {
                    if (FindMkvVideoSizeInTrackEntry(h, tp, entryEnd, outW, outH)) {
                        return true;
                    }
                }
                tp = entryEnd;
            }
        }

        pos = contentEnd;
    }
    return false;
}

// Detects the pixel width/height of a video file by reading its container
// header directly (mp4/mov/m4v via ISO-BMFF, mkv/webm via EBML). Returns
// false if the format isn't supported or dimensions couldn't be found.
bool GetVideoDimensions(const fs::path& videoPath, int& width, int& height) {
    std::wstring ext = ToLowerW(videoPath.extension().wstring());
    bool isMp4Family = (ext == L".mp4" || ext == L".mov" || ext == L".m4v");
    bool isMkvFamily = (ext == L".mkv" || ext == L".webm");
    if (!isMp4Family && !isMkvFamily) {
        return false;
    }

    std::wstring ioPath = ToLongPath(videoPath.wstring());
    HANDLE h = CreateFileW(
        ioPath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }

    LARGE_INTEGER fileSize{};
    if (!GetFileSizeEx(h, &fileSize) || fileSize.QuadPart <= 0) {
        CloseHandle(h);
        return false;
    }

    bool ok;
    if (isMp4Family) {
        ok = FindMp4VideoSize(h, 0, static_cast<uint64_t>(fileSize.QuadPart), 0, width, height);
    } else {
        ok = FindMkvVideoSize(h, 0, static_cast<uint64_t>(fileSize.QuadPart), width, height);
    }

    CloseHandle(h);
    return ok;
}

// Marker prefix used for a caption's `lang` field until ResolveTextBasedLanguages
// resolves it from actual text content. SMI captions get one marker per raw
// class value (so distinct tracks stay distinct groups even though the class
// name itself is never trusted — some SMI files mislabel a track, e.g. a
// Korean-only file whose only track is still named ENCC); SRT/ASS captions,
// which have no per-line class concept, all share a single marker (one
// group per file). Resolving per group instead of per line also avoids
// fragmenting a single-language file into spurious one-line "en"/"ko"
// output files whenever an odd line's script mix would tip a per-line guess.
const char* const kPendingLangMarker = "?";

std::string GuessLangFromText(const std::wstring& text) {
    int ko = 0;
    int en = 0;
    int jp = 0;

    for (wchar_t ch : text) {
        if (ch >= 0xAC00 && ch <= 0xD7A3) {
            ++ko;
        } else if ((ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z')) {
            ++en;
        } else if ((ch >= 0x3040 && ch <= 0x30FF) || (ch >= 0x31F0 && ch <= 0x31FF)) {
            ++jp;
        }
    }

    if (ko >= en && ko >= jp && ko > 0) {
        return "ko";
    }
    if (en >= ko && en >= jp && en > 0) {
        return "en";
    }
    if (jp >= ko && jp >= en && jp > 0) {
        return "jp";
    }
    return "und";
}

// Resolves each group of captions still carrying a kPendingLangMarker-based
// `lang` (every SRT/ASS caption, and every SMI caption grouped by its raw
// class value) into a single concrete language code derived from the actual
// text. Rather than guessing per line, this samples a handful of captions starting around the
// middle of the group — skipping any short/atypical opening lines —
// and picks the language with the highest character share across that
// sample, then applies it to every caption in the group. This keeps a
// single-language source from being fragmented into stray one-line output
// files whenever an odd line's script mix would tip a per-line guess.
void ResolveTextBasedLanguages(std::vector<Caption>& captions) {
    std::map<std::string, std::vector<size_t>> pendingGroups;
    for (size_t i = 0; i < captions.size(); ++i) {
        if (captions[i].lang.rfind(kPendingLangMarker, 0) == 0) {
            pendingGroups[captions[i].lang].push_back(i);
        }
    }

    for (auto& [groupKey, indices] : pendingGroups) {
        size_t sampleStart = indices.size() / 2;

        std::wstring sample;
        for (size_t k = sampleStart; k < indices.size() && sample.size() < 200; ++k) {
            sample += captions[indices[k]].text;
        }
        if (sample.size() < 20) {
            // Not enough sampled text to be confident; fall back to the
            // whole group's text.
            sample.clear();
            for (size_t idx : indices) {
                sample += captions[idx].text;
            }
        }

        std::string resolved = GuessLangFromText(sample);
        for (size_t idx : indices) {
            captions[idx].lang = resolved;
        }
    }
}

std::wstring FormatTimestamp(int totalMs) {
    if (totalMs < 0) {
        totalMs = 0;
    }

    int hours = totalMs / 2400000;
    totalMs %= 2400000;
    int minutes = totalMs / 60000;
    totalMs %= 60000;
    int seconds = totalMs / 1000;
    int millis = totalMs % 1000;

    wchar_t buffer[32] = {};
    swprintf_s(buffer, L"%02d:%02d:%02d,%03d", hours, minutes, seconds, millis);
    return buffer;
}

std::vector<Caption> ParseSmiCaptions(const std::wstring& content) {
    std::vector<Caption> parsed;

    std::wregex syncRegex(LR"(<sync[^>]*start\s*=\s*(\d+)[^>]*>)", std::regex_constants::icase);
    std::wsregex_iterator begin(content.begin(), content.end(), syncRegex);
    std::wsregex_iterator end;

    std::vector<std::pair<size_t, int>> syncPoints;
    for (auto it = begin; it != end; ++it) {
        int startMs = std::stoi((*it)[1].str());
        size_t tagPos = static_cast<size_t>((*it).position()) + (*it).length();
        syncPoints.push_back({tagPos, startMs});
    }

    for (size_t i = 0; i < syncPoints.size(); ++i) {
        size_t startPos = syncPoints[i].first;
        size_t endPos = (i + 1 < syncPoints.size()) ? syncPoints[i + 1].first : content.size();
        std::wstring block = content.substr(startPos, endPos - startPos);
        int startMs = syncPoints[i].second;
        int inferredEndMs = -1;
        if (i + 1 < syncPoints.size()) {
            int nextStartMs = syncPoints[i + 1].second;
            inferredEndMs = (nextStartMs > startMs) ? (nextStartMs - 1) : (startMs + 300);
        }

        std::wregex pRegex(LR"(<p[^>]*class\s*=\s*["']?([^"'\s>]+)[^>]*>([\s\S]*?)(?=(<p[^>]*>|$)))", std::regex_constants::icase);
        std::wsregex_iterator pBegin(block.begin(), block.end(), pRegex);
        std::wsregex_iterator pEnd;

        bool addedAny = false;
        for (auto pit = pBegin; pit != pEnd; ++pit) {
            std::wstring cls = (*pit)[1].str();
            std::wstring innerHtml = (*pit)[2].str();
            std::wstring text = NormalizeSubtitleText(innerHtml);
            if (text.empty()) {
                continue;
            }
            // The class name (e.g. KRCC/ENCC) only tells us which nominal
            // track a line belongs to — some SMI files mislabel a track
            // (e.g. a Korean-only file whose only track is still named
            // ENCC), so the actual language is always resolved from the
            // real text content later, never trusted from the class name.
            std::wstring normalizedClass = ToLowerW(cls);
            std::string lang = kPendingLangMarker + std::string(normalizedClass.begin(), normalizedClass.end());

            parsed.push_back({startMs, inferredEndMs, text, innerHtml, lang, normalizedClass});
            addedAny = true;
        }

        if (!addedAny) {
            std::wstring text = NormalizeSubtitleText(block);
            if (!text.empty()) {
                parsed.push_back({startMs, inferredEndMs, text, block, kPendingLangMarker, L""});
            }
        }
    }

    std::sort(parsed.begin(), parsed.end(), [](const Caption& left, const Caption& right) {
        return left.startMs < right.startMs;
    });

    return parsed;
}

std::vector<Caption> ParseSrtCaptions(const std::wstring& content) {
    std::vector<Caption> parsed;

    std::wregex blockRegex(
        LR"((?:^|\r?\n)\s*\d+\s*\r?\n\s*(\d{2}:\d{2}:\d{2},\d{3})\s*-->\s*(\d{2}:\d{2}:\d{2},\d{3})\s*\r?\n([\s\S]*?)(?=\r?\n\s*\r?\n|$))",
        std::regex_constants::icase);

    std::wsregex_iterator begin(content.begin(), content.end(), blockRegex);
    std::wsregex_iterator end;

    for (auto it = begin; it != end; ++it) {
        int startMs = ParseSrtTimestamp(Trim((*it)[1].str()));
        int endMs = ParseSrtTimestamp(Trim((*it)[2].str()));
        std::wstring text = NormalizePlainSubtitleText((*it)[3].str());
        if (startMs < 0 || endMs <= startMs || text.empty()) {
            continue;
        }
        parsed.push_back({startMs, endMs, text, L"", kPendingLangMarker, L""});
    }

    std::sort(parsed.begin(), parsed.end(), [](const Caption& left, const Caption& right) {
        return left.startMs < right.startMs;
    });
    return parsed;
}

std::vector<Caption> ParseAssCaptions(const std::wstring& content) {
    std::vector<Caption> parsed;
    std::wstringstream lines(content);
    std::wstring line;

    bool inEvents = false;
    std::vector<std::wstring> formatFields;

    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == L'\r') {
            line.pop_back();
        }

        std::wstring lowered = ToLowerW(Trim(line));
        if (lowered == L"[events]") {
            inEvents = true;
            continue;
        }
        if (!inEvents) {
            continue;
        }
        if (!lowered.empty() && lowered.front() == L'[') {
            break;
        }

        if (lowered.rfind(L"format:", 0) == 0) {
            std::wstring fieldSpec = Trim(line.substr(7));
            formatFields = SplitAssCsv(fieldSpec, 64);
            for (auto& field : formatFields) {
                field = ToLowerW(Trim(field));
            }
            continue;
        }

        if (lowered.rfind(L"dialogue:", 0) != 0) {
            continue;
        }

        std::wstring payload = line.substr(9);
        size_t expected = formatFields.empty() ? 10 : formatFields.size();
        std::vector<std::wstring> fields = SplitAssCsv(payload, expected);
        if (fields.size() < expected || expected == 0) {
            continue;
        }

        int startIdx = -1;
        int endIdx = -1;
        int textIdx = -1;

        if (formatFields.empty()) {
            startIdx = 1;
            endIdx = 2;
            textIdx = 9;
        } else {
            for (size_t i = 0; i < formatFields.size(); ++i) {
                if (formatFields[i] == L"start") startIdx = static_cast<int>(i);
                if (formatFields[i] == L"end") endIdx = static_cast<int>(i);
                if (formatFields[i] == L"text") textIdx = static_cast<int>(i);
            }
        }

        if (startIdx < 0 || endIdx < 0 || textIdx < 0 ||
            static_cast<size_t>(std::max({startIdx, endIdx, textIdx})) >= fields.size()) {
            continue;
        }

        int startMs = ParseAssTimestamp(Trim(fields[static_cast<size_t>(startIdx)]));
        int endMs = ParseAssTimestamp(Trim(fields[static_cast<size_t>(endIdx)]));
        std::wstring text = StripAssOverrides(fields[static_cast<size_t>(textIdx)]);
        if (startMs < 0 || endMs <= startMs || text.empty()) {
            continue;
        }

        parsed.push_back({startMs, endMs, text, L"", kPendingLangMarker, L""});
    }

    std::sort(parsed.begin(), parsed.end(), [](const Caption& left, const Caption& right) {
        return left.startMs < right.startMs;
    });
    return parsed;
}

TargetFormat ParseTargetFormatOrDefault(const std::wstring& arg, bool* matched) {
    std::wstring lower = ToLowerW(arg);
    if (lower == L"/to:smi" || lower == L"--to=smi" || lower == L"-to:smi") {
        if (matched) *matched = true;
        return TargetFormat::ToSmi;
    }
    if (lower == L"/to:srt" || lower == L"--to=srt" || lower == L"-to:srt") {
        if (matched) *matched = true;
        return TargetFormat::ToSrt;
    }
    if (lower == L"/to:ass" || lower == L"--to=ass" || lower == L"-to:ass") {
        if (matched) *matched = true;
        return TargetFormat::ToAss;
    }
    if (matched) *matched = false;
    return TargetFormat::ToSrt;
}

std::vector<Caption> ParseCaptionsByExtension(const std::wstring& extension, const std::wstring& content) {
    std::wstring ext = ToLowerW(extension);
    if (ext == L".smi") {
        return ParseSmiCaptions(content);
    }
    if (ext == L".srt") {
        return ParseSrtCaptions(content);
    }
    if (ext == L".ass") {
        return ParseAssCaptions(content);
    }
    return {};
}

std::wstring BuildSrtText(const std::vector<Caption>& items) {
    std::wstring srt;
    int index = 1;
    for (size_t i = 0; i < items.size(); ++i) {
        int startMs = std::max(0, items[i].startMs);
        int endMs = ResolveCaptionEndMs(items, i);
        srt += std::to_wstring(index++);
        srt += L"\r\n";
        srt += FormatTimestamp(startMs) + L" --> " + FormatTimestamp(endMs) + L"\r\n";
        srt += items[i].text + L"\r\n\r\n";
    }
    return srt;
}

std::wstring BuildSmiText(const std::vector<Caption>& items) {
    std::wstring smi = L"<SAMI>\r\n<BODY>\r\n";
    for (size_t i = 0; i < items.size(); ++i) {
        int startMs = std::max(0, items[i].startMs);
        smi += L"<SYNC Start=" + std::to_wstring(startMs) + L"><P Class=KRCC>" + ToSmiText(items[i].text) + L"\r\n";
    }
    smi += L"</BODY>\r\n</SAMI>\r\n";
    return smi;
}

// ─────────────────────────────────────────────────────────────────────────────
// ASS style optimisation helpers
// ─────────────────────────────────────────────────────────────────────────────

// Represents the set of formatting properties that can be expressed as a
// named [V4+ Styles] entry.
struct StyleDef {
    std::wstring color;   // ASS inline format  &HBBGGRR&  (empty = default white)
    bool bold      = false;
    bool italic    = false;
    bool underline = false;
    bool strikeout = false;
    int  fontSize  = 0;   // 0 = inherit default

    bool IsDefault() const {
        return color.empty() && !bold && !italic && !underline && !strikeout && fontSize == 0;
    }
    bool operator==(const StyleDef& o) const {
        return color == o.color && bold == o.bold && italic == o.italic &&
               underline == o.underline && strikeout == o.strikeout && fontSize == o.fontSize;
    }
    bool operator<(const StyleDef& o) const {
        if (color != o.color) return color < o.color;
        if (bold != o.bold)   return static_cast<int>(bold) < static_cast<int>(o.bold);
        if (italic != o.italic) return static_cast<int>(italic) < static_cast<int>(o.italic);
        if (underline != o.underline) return static_cast<int>(underline) < static_cast<int>(o.underline);
        if (strikeout != o.strikeout) return static_cast<int>(strikeout) < static_cast<int>(o.strikeout);
        return fontSize < o.fontSize;
    }
};

// Render a StyleDef back to ASS inline override codes (used when the style
// is not declared as a named style).
std::wstring StyleDefToInlineCodes(const StyleDef& s) {
    std::wstring codes;
    if (s.bold)      codes += L"{\\b1}";
    if (s.italic)    codes += L"{\\i1}";
    if (s.underline) codes += L"{\\u1}";
    if (s.strikeout) codes += L"{\\s1}";
    if (!s.color.empty())  codes += L"{\\c" + s.color + L"}";
    if (s.fontSize > 0)    codes += L"{\\fs" + std::to_wstring(s.fontSize) + L"}";
    return codes;
}

// "Arial" has no Hangul glyphs, so an ASS renderer (libass, VSFilter, ...)
// silently substitutes some arbitrary fallback font for Korean text — which
// can look nothing like whatever font the source SMI was actually previewed
// with. Malgun Gothic ships with every Windows install since Vista, has full
// Hangul coverage, and is the OS's own default Korean UI font, so it's a much
// safer default than a Latin-only font for output that's overwhelmingly
// Korean-subtitle content.
const wchar_t* const kAssFontName = L"맑은 고딕";

// Build the [V4+ Styles] "Style: ..." line for a named style.
std::wstring StyleDefToAssStyleLine(const std::wstring& name, const StyleDef& s, int baseFontSize) {
    int fontSize = (s.fontSize > 0) ? s.fontSize : baseFontSize;

    // s.color is &HBBGGRR& (9 chars); style field needs &H00BBGGRR (10 chars).
    std::wstring primaryColor = L"&H00FFFFFF";
    if (!s.color.empty() && s.color.size() >= 9) {
        primaryColor = L"&H00" + s.color.substr(2, 6); // skip &H, take 6 hex digits
    }

    return L"Style: " + name +
           L"," + kAssFontName + L"," + std::to_wstring(fontSize) + L"," +
           primaryColor + L",&H000000FF,&H00000000,&H64000000," +
           (s.bold      ? L"1" : L"0") + L"," +
           (s.italic    ? L"1" : L"0") + L"," +
           (s.underline ? L"1" : L"0") + L"," +
           (s.strikeout ? L"1" : L"0") + L"," +
           L"100,100,0,0,1,2,1,2,20,20,20,0\r\n";
}

// Derive a human-readable style name from a StyleDef.
// Colors are expressed as RGB hex (e.g. Clr_87CEEB for skyblue).
std::wstring StyleDefToName(const StyleDef& s) {
    std::vector<std::wstring> parts;

    if (!s.color.empty() && s.color.size() >= 9) {
        // s.color = &HBBGGRR& → convert to RGB order for readability
        try {
            int b = std::stoi(s.color.substr(2, 2), nullptr, 16);
            int g = std::stoi(s.color.substr(4, 2), nullptr, 16);
            int r = std::stoi(s.color.substr(6, 2), nullptr, 16);
            wchar_t buf[16];
            swprintf_s(buf, L"Clr_%02X%02X%02X", r, g, b);
            parts.push_back(buf);
        } catch (...) {
            parts.push_back(L"Color");
        }
    }
    if (s.bold)      parts.push_back(L"Bold");
    if (s.italic)    parts.push_back(L"Italic");
    if (s.underline) parts.push_back(L"Underline");
    if (s.strikeout) parts.push_back(L"Strikeout");
    if (s.fontSize > 0) parts.push_back(L"Fs" + std::to_wstring(s.fontSize));

    std::wstring name;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) name += L"_";
        name += parts[i];
    }
    return name.empty() ? L"Custom" : name;
}

// Strip ASS override blocks at the END of a dialogue text that merely reset
// formatting to defaults.  Because each ASS Dialogue event resets style to
// its named style automatically, these trailing resets are redundant.
std::wstring StripTrailingAssResets(const std::wstring& text, int baseFontSize) {
    const std::wstring fsReset = L"{\\fs" + std::to_wstring(baseFontSize) + L"}";

    std::wstring out = text;
    bool changed = true;
    while (changed) {
        changed = false;
        if (out.empty() || out.back() != L'}') break;
        size_t open = out.rfind(L'{');
        if (open == std::wstring::npos) break;
        std::wstring last = out.substr(open);

        bool isReset = (last == L"{\\b0}" || last == L"{\\i0}" ||
                        last == L"{\\u0}" || last == L"{\\s0}" ||
                        last == L"{\\c&H00FFFFFF&}" || last == fsReset);
        if (isReset) {
            out = out.substr(0, open);
            changed = true;
        }
    }
    return out;
}

// Remove redundant reset/reopen pairs across \N and override blocks with
// no intervening visible text.  Examples:
//   {\c&H00FFFFFF&}\N{\i1}{\c&HEBCE87&}  →  \N{\i1}{\c&HEBCE87&}
//   {\i0}\N{\i1}                         →  \N{\i1}
// Applied iteratively until stable.
std::wstring OptimizeAssInlineCodes(std::wstring text, int baseFontSize) {
    // (?:\N|\{[^}]*\})* matches sequences of line-break or any override block
    static const std::wregex colorReset(
        LR"(\{\\c&H00FFFFFF&\}((?:\\N|\{[^}]*\})*)(\{\\c[^}]+\}))",
        std::regex_constants::icase);
    static const std::wregex italicReset(
        LR"(\{\\i0\}((?:\\N|\{[^}]*\})*)(\{\\i1\}))");
    static const std::wregex boldReset(
        LR"(\{\\b0\}((?:\\N|\{[^}]*\})*)(\{\\b1\}))");
    static const std::wregex underlineReset(
        LR"(\{\\u0\}((?:\\N|\{[^}]*\})*)(\{\\u1\}))");
    static const std::wregex strikeReset(
        LR"(\{\\s0\}((?:\\N|\{[^}]*\})*)(\{\\s1\}))");
    // Depends on baseFontSize (varies per matched video resolution), so unlike
    // the patterns above this one can't be a compile-time constant.
    const std::wregex fsReset(
        std::wstring(LR"(\{\\fs)") + std::to_wstring(baseFontSize) +
        LR"(\}((?:\\N|\{[^}]*\})*)(\{\\fs\d+\}))");

    const std::wregex* patterns[] = {
        &colorReset, &italicReset, &boldReset, &underlineReset, &strikeReset, &fsReset
    };

    bool changed;
    do {
        changed = false;
        for (const auto* re : patterns) {
            std::wstring next = std::regex_replace(text, *re, L"$1$2");
            if (next != text) { text = std::move(next); changed = true; }
        }
    } while (changed);

    return text;
}

bool ApplyAssOverrideToStyle(const std::wstring& inner, StyleDef& style, int baseFontSize) {
    if (inner == L"\\b1") {
        style.bold = true;
        return true;
    }
    if (inner == L"\\b0") {
        style.bold = false;
        return true;
    }
    if (inner == L"\\i1") {
        style.italic = true;
        return true;
    }
    if (inner == L"\\i0") {
        style.italic = false;
        return true;
    }
    if (inner == L"\\u1") {
        style.underline = true;
        return true;
    }
    if (inner == L"\\u0") {
        style.underline = false;
        return true;
    }
    if (inner == L"\\s1") {
        style.strikeout = true;
        return true;
    }
    if (inner == L"\\s0") {
        style.strikeout = false;
        return true;
    }
    if (inner == L"\\r" || inner == L"\\rDefault") {
        style = StyleDef{};
        return true;
    }
    if (inner.rfind(L"\\c", 0) == 0 && inner.size() > 2) {
        std::wstring color = inner.substr(2);
        if (ToLowerW(color) == L"&h00ffffff&") {
            style.color.clear();
        } else {
            style.color = color;
        }
        return true;
    }
    if (inner.rfind(L"\\fs", 0) == 0 && inner.size() > 3) {
        try {
            int fontSize = std::stoi(inner.substr(3));
            style.fontSize = (fontSize == baseFontSize) ? 0 : fontSize;
            return true;
        } catch (...) {
            return false;
        }
    }
    return false;
}

bool HasVisibleTextBeforeNextOverride(const std::wstring& text, size_t pos) {
    while (pos < text.size()) {
        if (text[pos] == L'{') {
            return false;
        }
        if (text[pos] == L'\\' && pos + 1 < text.size() && text[pos + 1] == L'N') {
            pos += 2;
            continue;
        }
        return true;
    }
    return false;
}

void CollectStyleRunsFromAssText(const std::wstring& text, std::map<StyleDef, int>& styleCounts, int baseFontSize) {
    StyleDef currentStyle;
    size_t pos = 0;

    while (pos < text.size()) {
        if (text[pos] != L'{') {
            if (text[pos] == L'\\' && pos + 1 < text.size() && text[pos + 1] == L'N') {
                pos += 2;
            } else {
                ++pos;
            }
            continue;
        }

        StyleDef nextStyle = currentStyle;
        bool recognizedAny = false;
        bool allRecognized = true;

        while (pos < text.size() && text[pos] == L'{') {
            size_t close = text.find(L'}', pos);
            if (close == std::wstring::npos) {
                allRecognized = false;
                pos = text.size();
                break;
            }

            std::wstring inner = text.substr(pos + 1, close - pos - 1);
            if (ApplyAssOverrideToStyle(inner, nextStyle, baseFontSize)) {
                recognizedAny = true;
            } else {
                allRecognized = false;
            }
            pos = close + 1;
        }

        if (allRecognized && recognizedAny && !(nextStyle == currentStyle) &&
            !nextStyle.IsDefault() && HasVisibleTextBeforeNextOverride(text, pos)) {
            ++styleCounts[nextStyle];
        }

        if (allRecognized && recognizedAny) {
            currentStyle = nextStyle;
        }
    }
}

std::wstring RewriteAssTextWithNamedStyles(
    const std::wstring& text,
    const std::map<StyleDef, std::wstring>& styleNames,
    int baseFontSize) {

    std::wstring out;
    out.reserve(text.size());

    StyleDef currentStyle;
    size_t pos = 0;

    while (pos < text.size()) {
        if (text[pos] != L'{') {
            if (text[pos] == L'\\' && pos + 1 < text.size() && text[pos + 1] == L'N') {
                out += L"\\N";
                pos += 2;
            } else {
                out.push_back(text[pos]);
                ++pos;
            }
            continue;
        }

        StyleDef nextStyle = currentStyle;
        std::wstring rawBlocks;
        bool recognizedAny = false;
        bool allRecognized = true;

        while (pos < text.size() && text[pos] == L'{') {
            size_t close = text.find(L'}', pos);
            if (close == std::wstring::npos) {
                allRecognized = false;
                rawBlocks += text.substr(pos);
                pos = text.size();
                break;
            }

            rawBlocks += text.substr(pos, close - pos + 1);
            std::wstring inner = text.substr(pos + 1, close - pos - 1);
            if (ApplyAssOverrideToStyle(inner, nextStyle, baseFontSize)) {
                recognizedAny = true;
            } else {
                allRecognized = false;
            }
            pos = close + 1;
        }

        bool hasVisibleText = HasVisibleTextBeforeNextOverride(text, pos);
        if (allRecognized && recognizedAny) {
            if (!(nextStyle == currentStyle) && hasVisibleText) {
                if (nextStyle.IsDefault()) {
                    out += L"{\\rDefault}";
                } else {
                    auto it = styleNames.find(nextStyle);
                    if (it != styleNames.end()) {
                        out += L"{\\r" + it->second + L"}";
                    } else {
                        out += rawBlocks;
                    }
                }
            }
            currentStyle = nextStyle;
        } else {
            out += rawBlocks;
        }
    }

    return out;
}

// Try to detect a "uniform style": the ASS text begins with a sequence of
// opening override codes and the rest of the content (which may contain \N
// line-breaks) carries no further override codes.  If successful, the
// extracted StyleDef is placed in |outStyle| and the bare text is placed
// in |outStripped|.
bool TryExtractUniformStyle(const std::wstring& text,
                             StyleDef& outStyle, std::wstring& outStripped) {
    StyleDef s;
    size_t pos = 0;

    while (pos < text.size() && text[pos] == L'{') {
        size_t close = text.find(L'}', pos);
        if (close == std::wstring::npos) break;
        std::wstring inner = text.substr(pos + 1, close - pos - 1);

        if (inner == L"\\b1") {
            s.bold = true;
        } else if (inner == L"\\i1") {
            s.italic = true;
        } else if (inner == L"\\u1") {
            s.underline = true;
        } else if (inner == L"\\s1") {
            s.strikeout = true;
        } else if (inner.rfind(L"\\c", 0) == 0 && inner.size() > 2) {
            s.color = inner.substr(2); // &HBBGGRR&  (skip \c prefix)
        } else if (inner.rfind(L"\\fs", 0) == 0) {
            try { s.fontSize = std::stoi(inner.substr(3)); } catch (...) {}
        } else {
            return false; // unknown override — can't convert to named style
        }
        pos = close + 1;
    }

    if (s.IsDefault()) return false; // nothing to extract

    std::wstring remaining = text.substr(pos);
    if (remaining.empty()) return false;
    if (remaining.find(L'{') != std::wstring::npos) return false; // mixed formatting

    outStyle   = s;
    outStripped = remaining;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// SMI <STYLE> CSS font-weight → ASS bold mapping.
// SAMI files commonly declare default/per-track boldness via CSS instead of
// (or in addition to) inline <b> tags, e.g.:
//   P { font-weight:normal; ... }
//   .ENCC { font-weight:bold; }
// These rules never appear as inline tags in the caption body, so without
// reading the <STYLE> block a track declared bold only via CSS would render
// as regular weight in the converted ASS. ParseSmiCssBoldRules extracts the
// bold/normal verdict per class selector (".KRCC", ".ENCC", ...) and for the
// bare "P" element selector (used as the file-wide default).
// ─────────────────────────────────────────────────────────────────────────────

struct SmiCssBoldRules {
    bool hasDefaultBold = false;
    bool defaultBold = false;
    std::map<std::wstring, bool> classBold; // lowercase class name -> bold
};

// Interprets a CSS font-weight value ("bold", "normal", "bolder", "lighter",
// or a numeric weight 100-900) as a bold/not-bold verdict. Returns false via
// outBold and false as the return value if the value isn't recognized.
bool InterpretFontWeightValue(const std::wstring& rawValue, bool& outBold) {
    std::wstring value = ToLowerW(Trim(rawValue));
    if (value == L"bold" || value == L"bolder") {
        outBold = true;
        return true;
    }
    if (value == L"normal" || value == L"lighter") {
        outBold = false;
        return true;
    }
    try {
        size_t consumed = 0;
        int weight = std::stoi(value, &consumed);
        if (consumed == value.size()) {
            outBold = (weight >= 600);
            return true;
        }
    } catch (...) {}
    return false;
}

SmiCssBoldRules ParseSmiCssBoldRules(const std::wstring& content) {
    SmiCssBoldRules rules;

    std::wregex styleBlockRe(LR"(<style[^>]*>([\s\S]*?)</style>)", std::regex_constants::icase);
    std::wsmatch styleMatch;
    if (!std::regex_search(content, styleMatch, styleBlockRe)) {
        return rules;
    }
    std::wstring css = styleMatch[1].str();
    // SAMI wraps its CSS in an HTML comment (<!-- ... -->) so non-CSS-aware
    // renderers ignore it; strip the markers so they don't get absorbed into
    // the first rule's selector (e.g. "<!--\nP" failing to match "p").
    css = std::regex_replace(css, std::wregex(LR"(<!--|-->)"), L"");

    std::wregex ruleRe(LR"(([^{}]+)\{([^}]*)\})");
    std::wsregex_iterator it(css.begin(), css.end(), ruleRe);
    std::wsregex_iterator end;
    std::wregex fontWeightRe(LR"(font-weight\s*:\s*([a-z0-9]+))", std::regex_constants::icase);

    for (; it != end; ++it) {
        std::wstring body = (*it)[2].str();
        std::wsmatch fwMatch;
        if (!std::regex_search(body, fwMatch, fontWeightRe)) {
            continue;
        }
        bool isBold = false;
        if (!InterpretFontWeightValue(fwMatch[1].str(), isBold)) {
            continue;
        }

        std::wstring selectorPart = (*it)[1].str();
        std::wstringstream selStream(selectorPart);
        std::wstring selector;
        while (std::getline(selStream, selector, L',')) {
            std::wstring sel = ToLowerW(Trim(selector));
            if (sel.empty()) {
                continue;
            }
            if (sel[0] == L'.') {
                rules.classBold[sel.substr(1)] = isBold;
            } else if (sel == L"p") {
                rules.hasDefaultBold = true;
                rules.defaultBold = isBold;
            }
        }
    }

    return rules;
}

// Resolves whether a caption should render bold by default per the SMI's CSS,
// preferring its own class's rule and falling back to the file-wide "P"
// default when the class has no explicit font-weight declared.
bool ResolveCssBold(const std::wstring& smiClass, const SmiCssBoldRules& rules) {
    if (!smiClass.empty()) {
        auto it = rules.classBold.find(ToLowerW(Trim(smiClass)));
        if (it != rules.classBold.end()) {
            return it->second;
        }
    }
    return rules.hasDefaultBold && rules.defaultBold;
}

std::wstring BuildAssText(const std::vector<Caption>& items, int playResX, int playResY, int baseFontSize,
                           const SmiCssBoldRules& cssBoldRules = SmiCssBoldRules{}) {
    // ── Pass 1: generate per-item ASS text and detect uniform styles ──────────
    struct ItemInfo {
        std::wstring assText;      // dialogue text after local optimization
        StyleDef     style;        // uniform style, if detected
        bool         hasUniform;   // true  → assText has no inline codes
    };

    std::vector<ItemInfo> infos;
    infos.reserve(items.size());

    std::map<StyleDef, int> styleCounts;

    for (const auto& item : items) {
        ItemInfo info{};
        std::wstring raw;

        if (!item.rawHtml.empty()) {
            raw = SmiHtmlToAssText(item.rawHtml, baseFontSize);
        } else {
            raw = item.text;
            size_t p = 0;
            while ((p = raw.find(L"\n", p)) != std::wstring::npos) {
                raw.replace(p, 1, L"\\N");
                p += 2;
            }
        }

        // A caption whose SMI class (or the file-wide "P" selector) declares
        // font-weight:bold in CSS never carries an inline <b> tag for it —
        // the bold-ness only exists in the <STYLE> block. Prepend the same
        // override an explicit <b> would have produced so it flows through
        // the existing style-detection/dedup logic below unchanged.
        if (ResolveCssBold(item.smiClass, cssBoldRules)) {
            raw = L"{\\b1}" + raw;
        }

        raw = StripTrailingAssResets(raw, baseFontSize);
        raw = OptimizeAssInlineCodes(raw, baseFontSize);
        CollectStyleRunsFromAssText(raw, styleCounts, baseFontSize);

        StyleDef s;
        std::wstring stripped;
        if (TryExtractUniformStyle(raw, s, stripped)) {
            info.style      = s;
            info.assText    = stripped;
            info.hasUniform = true;
        } else {
            info.assText    = raw;
            info.hasUniform = false;
        }

        infos.push_back(std::move(info));
    }

    // ── Pass 2: declare named styles for those used ≥ 2 times ────────────────
    // Sort by occurrence count descending so the most common style comes first.
    std::vector<std::pair<int, StyleDef>> ranked;
    ranked.reserve(styleCounts.size());
    for (const auto& [s, cnt] : styleCounts) {
        if (cnt >= 2) ranked.push_back({cnt, s});
    }
    std::sort(ranked.begin(), ranked.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });

    std::map<StyleDef, std::wstring> styleNames;
    std::set<std::wstring> usedNames;
    usedNames.insert(L"Default");

    for (const auto& [cnt, s] : ranked) {
        std::wstring name = StyleDefToName(s);
        if (usedNames.count(name)) {
            int n = 2;
            while (usedNames.count(name + L"_" + std::to_wstring(n))) ++n;
            name += L"_" + std::to_wstring(n);
        }
        usedNames.insert(name);
        styleNames[s] = name;
    }

    // ── Build output ──────────────────────────────────────────────────────────
    std::wstring ass;
    ass += L"[Script Info]\r\n";
    ass += L"Title: subConverter\r\n";
    ass += L"ScriptType: v4.00+\r\n";
    ass += L"PlayResX: " + std::to_wstring(playResX) + L"\r\n";
    ass += L"PlayResY: " + std::to_wstring(playResY) + L"\r\n";
    ass += L"WrapStyle: 0\r\n";
    ass += L"ScaledBorderAndShadow: yes\r\n\r\n";
    ass += L"[V4+ Styles]\r\n";
    ass += L"Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\r\n";
    ass += L"Style: Default," + std::wstring(kAssFontName) + L"," + std::to_wstring(baseFontSize) +
           L",&H00FFFFFF,&H000000FF,&H00000000,&H64000000,0,0,0,0,100,100,0,0,1,2,1,2,20,20,20,0\r\n";

    // Named styles sorted alphabetically for deterministic output
    std::vector<std::pair<std::wstring, StyleDef>> sortedStyles;
    sortedStyles.reserve(styleNames.size());
    for (const auto& [s, name] : styleNames) sortedStyles.push_back({name, s});
    std::sort(sortedStyles.begin(), sortedStyles.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [name, s] : sortedStyles) {
        ass += StyleDefToAssStyleLine(name, s, baseFontSize);
    }

    ass += L"\r\n[Events]\r\n";
    ass += L"Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\r\n";

    for (size_t i = 0; i < items.size(); ++i) {
        int startMs = std::max(0, items[i].startMs);
        int endMs   = ResolveCaptionEndMs(items, i);

        std::wstring styleName = L"Default";
        std::wstring text      = infos[i].assText;

        if (infos[i].hasUniform) {
            auto it = styleNames.find(infos[i].style);
            if (it != styleNames.end()) {
                // Use the declared named style — no inline codes needed
                styleName = it->second;
            } else {
                // Style used only once — keep inline codes, text is already stripped
                text = StyleDefToInlineCodes(infos[i].style) + text;
            }
        } else {
            text = RewriteAssTextWithNamedStyles(text, styleNames, baseFontSize);
        }

        ass += L"Dialogue: 0," + FormatTimestampAss(startMs) + L"," + FormatTimestampAss(endMs) +
               L"," + styleName + L",,,,,," + text + L"\r\n";
    }
    return ass;
}

std::wstring TargetExtension(TargetFormat target) {
    if (target == TargetFormat::ToSmi) return L".smi";
    if (target == TargetFormat::ToAss) return L".ass";
    return L".srt";
}

// Strips a trailing recognized language-code segment (".ko"/".en"/".jp", any
// case; loops to catch more than one stacked) from a stem before the
// newly-detected language tag is appended, so re-converting an
// already-tagged file (e.g. "Show.ko.smi") doesn't produce "Show.ko.ko.srt".
std::wstring StripKnownLanguageSuffix(std::wstring stem) {
    static const std::set<std::wstring> knownLangCodes = {L"ko", L"en", L"jp"};
    while (true) {
        size_t dot = stem.rfind(L'.');
        if (dot == std::wstring::npos) break;
        std::wstring suffix = ToLowerW(stem.substr(dot + 1));
        if (knownLangCodes.find(suffix) == knownLangCodes.end()) break;
        stem = stem.substr(0, dot);
    }
    return stem;
}

std::wstring BuildOutputText(TargetFormat target, const std::vector<Caption>& items,
                              int playResX = 1920, int playResY = 1080, int baseFontSize = 75,
                              const SmiCssBoldRules& cssBoldRules = SmiCssBoldRules{}) {
    if (target == TargetFormat::ToSmi) {
        return BuildSmiText(items);
    }
    if (target == TargetFormat::ToAss) {
        return BuildAssText(items, playResX, playResY, baseFontSize, cssBoldRules);
    }
    return BuildSrtText(items);
}

fs::path EnsureUniqueOutputPath(const fs::path& candidate) {
    std::error_code ec;
    if (!fs::exists(candidate, ec)) {
        return candidate;
    }

    fs::path parent = candidate.parent_path();
    std::wstring stem = candidate.stem().wstring();
    std::wstring ext = candidate.extension().wstring();

    for (int n = 1; n < 100000; ++n) {
        fs::path next = parent / (stem + L" (" + std::to_wstring(n) + L")" + ext);
        std::error_code existsEc;
        if (!fs::exists(next, existsEc)) {
            return next;
        }
    }

    return candidate;
}

bool WriteUtf8File(const fs::path& outPath, const std::wstring& content) {
    int needed = WideCharToMultiByte(CP_UTF8, 0, content.c_str(), static_cast<int>(content.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        return false;
    }

    std::string utf8(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, content.c_str(), static_cast<int>(content.size()), utf8.data(), needed, nullptr, nullptr);

    std::wstring ioPath = ToLongPath(outPath.wstring());

    HANDLE out = CreateFileW(
        ioPath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (out == INVALID_HANDLE_VALUE) {
        return false;
    }

    const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
    DWORD wrote = 0;
    BOOL bomOk = WriteFile(out, bom, 3, &wrote, nullptr);
    if (!bomOk || wrote != 3) {
        CloseHandle(out);
        return false;
    }

    size_t writtenTotal = 0;
    while (writtenTotal < utf8.size()) {
        DWORD chunk = static_cast<DWORD>(std::min<size_t>(1 << 20, utf8.size() - writtenTotal));
        DWORD chunkWritten = 0;
        BOOL ok = WriteFile(out, utf8.data() + writtenTotal, chunk, &chunkWritten, nullptr);
        if (!ok || chunkWritten == 0) {
            CloseHandle(out);
            return false;
        }
        writtenTotal += chunkWritten;
    }

    CloseHandle(out);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Same-format conversion (e.g. .smi -> .smi). Unlike cross-format conversion,
// which parses into Captions and rebuilds the file (normalizing formatting,
// reclassing SMI tracks, and splitting multi-language sources into separate
// files), a same-format run must leave every surviving byte of the source
// exactly as-is. The only two changes allowed are the rule-based removal of
// leading/trailing credit captions and the output filename. These helpers
// therefore locate each caption's raw span in the original text and excise
// only the spans identified as credits, leaving everything else untouched.
// ─────────────────────────────────────────────────────────────────────────────

std::wstring ExciseSpans(const std::wstring& content, std::vector<std::pair<size_t, size_t>> spans) {
    if (spans.empty()) {
        return content;
    }
    std::sort(spans.begin(), spans.end());

    std::wstring out;
    out.reserve(content.size());
    size_t cursor = 0;
    for (const auto& span : spans) {
        size_t start = std::max(span.first, cursor);
        size_t end = std::max(span.second, cursor);
        if (start > cursor) {
            out.append(content, cursor, start - cursor);
        }
        cursor = std::max(cursor, end);
    }
    if (cursor < content.size()) {
        out.append(content, cursor, content.size() - cursor);
    }
    return out;
}

std::wstring RemoveSrtCreditsRaw(const std::wstring& content) {
    struct Item { size_t start; size_t end; std::wstring text; };
    std::vector<Item> items;

    std::wregex blockRegex(
        LR"((?:^|\r?\n)\s*\d+\s*\r?\n\s*(\d{2}:\d{2}:\d{2},\d{3})\s*-->\s*(\d{2}:\d{2}:\d{2},\d{3})\s*\r?\n([\s\S]*?)(?=\r?\n\s*\r?\n|$))",
        std::regex_constants::icase);

    std::wsregex_iterator it(content.begin(), content.end(), blockRegex);
    std::wsregex_iterator end;
    for (; it != end; ++it) {
        std::wstring text = NormalizePlainSubtitleText((*it)[3].str());
        if (text.empty()) {
            continue;
        }
        items.push_back({static_cast<size_t>(it->position(0)),
                          static_cast<size_t>(it->position(0) + it->length(0)),
                          text});
    }

    size_t begin = 0;
    while (begin < items.size() && IsCreatorCreditCaption(items[begin].text)) {
        ++begin;
    }
    size_t last = items.size();
    while (last > begin && IsCreatorCreditCaption(items[last - 1].text)) {
        --last;
    }

    std::vector<std::pair<size_t, size_t>> spans;
    for (size_t i = 0; i < begin; ++i) spans.push_back({items[i].start, items[i].end});
    for (size_t i = last; i < items.size(); ++i) spans.push_back({items[i].start, items[i].end});

    return ExciseSpans(content, spans);
}

std::wstring RemoveAssCreditsRaw(const std::wstring& content) {
    struct Item { size_t start; size_t end; std::wstring text; };
    std::vector<Item> items;

    bool inEvents = false;
    std::vector<std::wstring> formatFields;
    size_t pos = 0;

    while (pos <= content.size()) {
        size_t nl = content.find(L'\n', pos);
        size_t lineTextEnd = (nl == std::wstring::npos) ? content.size() : nl;
        size_t nextPos = (nl == std::wstring::npos) ? content.size() : nl + 1;

        std::wstring line = content.substr(pos, lineTextEnd - pos);
        if (!line.empty() && line.back() == L'\r') {
            line.pop_back();
        }
        std::wstring lowered = ToLowerW(Trim(line));

        if (lowered == L"[events]") {
            inEvents = true;
        } else if (inEvents && !lowered.empty() && lowered.front() == L'[') {
            inEvents = false;
        } else if (inEvents && lowered.rfind(L"format:", 0) == 0) {
            std::wstring fieldSpec = Trim(line.substr(7));
            formatFields = SplitAssCsv(fieldSpec, 64);
            for (auto& field : formatFields) {
                field = ToLowerW(Trim(field));
            }
        } else if (inEvents && lowered.rfind(L"dialogue:", 0) == 0) {
            std::wstring payload = line.substr(9);
            size_t expected = formatFields.empty() ? 10 : formatFields.size();
            std::vector<std::wstring> fields = SplitAssCsv(payload, expected);

            int textIdx = -1;
            if (formatFields.empty()) {
                textIdx = 9;
            } else {
                for (size_t i = 0; i < formatFields.size(); ++i) {
                    if (formatFields[i] == L"text") { textIdx = static_cast<int>(i); break; }
                }
            }

            if (fields.size() >= expected && expected > 0 &&
                textIdx >= 0 && static_cast<size_t>(textIdx) < fields.size()) {
                std::wstring text = StripAssOverrides(fields[static_cast<size_t>(textIdx)]);
                if (!text.empty()) {
                    items.push_back({pos, nextPos, text});
                }
            }
        }

        if (nl == std::wstring::npos) {
            break;
        }
        pos = nextPos;
    }

    size_t begin = 0;
    while (begin < items.size() && IsCreatorCreditCaption(items[begin].text)) {
        ++begin;
    }
    size_t last = items.size();
    while (last > begin && IsCreatorCreditCaption(items[last - 1].text)) {
        --last;
    }

    std::vector<std::pair<size_t, size_t>> spans;
    for (size_t i = 0; i < begin; ++i) spans.push_back({items[i].start, items[i].end});
    for (size_t i = last; i < items.size(); ++i) spans.push_back({items[i].start, items[i].end});

    return ExciseSpans(content, spans);
}

std::wstring RemoveSmiCreditsRaw(const std::wstring& content) {
    struct Item { size_t start; size_t end; std::wstring text; size_t blockIndex; };
    struct Block { size_t tagStart; size_t tagEnd; };

    std::vector<Item> items;
    std::vector<Block> blocks;

    std::wregex syncRegex(LR"(<sync[^>]*start\s*=\s*(\d+)[^>]*>)", std::regex_constants::icase);
    std::wsregex_iterator sBegin(content.begin(), content.end(), syncRegex);
    std::wsregex_iterator sEnd;

    std::vector<std::pair<size_t, size_t>> syncTagSpans; // [tagStart, tagEnd)
    for (auto it = sBegin; it != sEnd; ++it) {
        syncTagSpans.push_back({static_cast<size_t>(it->position(0)),
                                 static_cast<size_t>(it->position(0) + it->length(0))});
    }

    std::wregex pRegex(LR"(<p[^>]*class\s*=\s*["']?([^"'\s>]+)[^>]*>([\s\S]*?)(?=(<p[^>]*>|$)))", std::regex_constants::icase);

    // The final <SYNC> block's content run extends to end-of-file, so its
    // last <P> match (via the "(<p...>|$)" lookahead) would otherwise swallow
    // the mandatory trailing </BODY></SAMI> markup into the caption's raw
    // span. Since that markup is structural (not caption content), never let
    // a caption's raw span extend past the first closing </BODY> or </SAMI>
    // tag it contains.
    static const std::wregex closingTagRegex(LR"(</\s*(body|sami)\b[^>]*>)", std::regex_constants::icase);
    auto clampBeforeClosingTag = [&](size_t start, size_t end) -> size_t {
        std::wsmatch m;
        std::wstring segment = content.substr(start, end - start);
        if (std::regex_search(segment, m, closingTagRegex)) {
            return start + static_cast<size_t>(m.position(0));
        }
        return end;
    };

    for (size_t i = 0; i < syncTagSpans.size(); ++i) {
        size_t tagStart = syncTagSpans[i].first;
        size_t tagEnd = syncTagSpans[i].second;
        size_t blockContentEnd = (i + 1 < syncTagSpans.size()) ? syncTagSpans[i + 1].first : content.size();

        size_t blockIndex = blocks.size();
        blocks.push_back({tagStart, tagEnd});

        std::wstring block = content.substr(tagEnd, blockContentEnd - tagEnd);
        std::wsregex_iterator pBegin(block.begin(), block.end(), pRegex);
        std::wsregex_iterator pEnd;

        bool addedAny = false;
        for (auto pit = pBegin; pit != pEnd; ++pit) {
            std::wstring innerHtml = (*pit)[2].str();
            std::wstring text = NormalizeSubtitleText(innerHtml);
            if (text.empty()) {
                continue;
            }
            size_t itemStart = tagEnd + static_cast<size_t>(pit->position(0));
            size_t itemEnd = clampBeforeClosingTag(itemStart, tagEnd + static_cast<size_t>(pit->position(0) + pit->length(0)));
            items.push_back({itemStart, itemEnd, text, blockIndex});
            addedAny = true;
        }

        if (!addedAny) {
            std::wstring text = NormalizeSubtitleText(block);
            if (!text.empty()) {
                items.push_back({tagEnd, clampBeforeClosingTag(tagEnd, blockContentEnd), text, blockIndex});
            }
        }
    }

    size_t begin = 0;
    while (begin < items.size() && IsCreatorCreditCaption(items[begin].text)) {
        ++begin;
    }
    size_t last = items.size();
    while (last > begin && IsCreatorCreditCaption(items[last - 1].text)) {
        --last;
    }

    std::set<size_t> removedItemIndices;
    for (size_t i = 0; i < begin; ++i) removedItemIndices.insert(i);
    for (size_t i = last; i < items.size(); ++i) removedItemIndices.insert(i);

    std::map<size_t, int> totalItemsInBlock;
    std::map<size_t, int> removedItemsInBlock;
    for (size_t i = 0; i < items.size(); ++i) {
        ++totalItemsInBlock[items[i].blockIndex];
        if (removedItemIndices.count(i)) {
            ++removedItemsInBlock[items[i].blockIndex];
        }
    }

    std::vector<std::pair<size_t, size_t>> spans;
    for (size_t i : removedItemIndices) {
        spans.push_back({items[i].start, items[i].end});
    }
    for (const auto& [blockIdx, total] : totalItemsInBlock) {
        auto foundIt = removedItemsInBlock.find(blockIdx);
        if (foundIt != removedItemsInBlock.end() && foundIt->second == total) {
            spans.push_back({blocks[blockIdx].tagStart, blocks[blockIdx].tagEnd});
        }
    }

    return ExciseSpans(content, spans);
}

ConvertResult ConvertSingleFile(const fs::path& inputPath, TargetFormat target,
                                 const std::map<std::wstring, int>& bareEpisodes) {
    std::wstring ioPath = ToLongPath(inputPath.wstring());

    HANDLE in = CreateFileW(
        ioPath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (in == INVALID_HANDLE_VALUE) {
        return {false, L"파일을 열 수 없습니다."};
    }

    LARGE_INTEGER fileSize{};
    if (!GetFileSizeEx(in, &fileSize) || fileSize.QuadPart < 0) {
        CloseHandle(in);
        return {false, L"파일 크기 확인에 실패했습니다."};
    }

    std::string bytes;
    bytes.reserve(static_cast<size_t>(std::min<LONGLONG>(fileSize.QuadPart, 16 * 1024 * 1024)));

    char buffer[1 << 20];
    while (true) {
        DWORD read = 0;
        BOOL ok = ReadFile(in, buffer, static_cast<DWORD>(sizeof(buffer)), &read, nullptr);
        if (!ok) {
            CloseHandle(in);
            return {false, L"파일 읽기에 실패했습니다."};
        }
        if (read == 0) {
            break;
        }
        bytes.append(buffer, buffer + read);
    }
    CloseHandle(in);

    std::wstring content = DecodeToWide(bytes);
    if (content.empty()) {
        return {false, L"파일 인코딩을 해석하지 못했습니다."};
    }

    std::wstring inputExt = ToLowerW(inputPath.extension().wstring());
    std::wstring outputExt = TargetExtension(target);

    // Same-format conversion (e.g. .smi -> .smi): only remove rule-based
    // credit captions and rename the file — everything else in the source
    // must survive byte-for-byte, so this bypasses the normalize/rebuild
    // pipeline used below for cross-format conversion.
    if (inputExt == outputExt) {
        std::vector<Caption> probe = ParseCaptionsByExtension(inputExt, content);
        if (probe.empty()) {
            return {false, L"변환 가능한 자막 구간을 찾지 못했습니다."};
        }

        std::wstring newContent;
        if (inputExt == L".smi") {
            newContent = RemoveSmiCreditsRaw(content);
        } else if (inputExt == L".srt") {
            newContent = RemoveSrtCreditsRaw(content);
        } else if (inputExt == L".ass") {
            newContent = RemoveAssCreditsRaw(content);
        } else {
            newContent = content;
        }

        std::vector<Caption> remaining = ParseCaptionsByExtension(inputExt, newContent);
        if (remaining.empty()) {
            return {false, L"크레딧(제작자 정보) 제거 후 변환 가능한 자막이 없습니다."};
        }

        fs::path matchedVideo = ResolveMatchedVideoPath(inputPath, bareEpisodes);
        std::wstring outputBaseStem = matchedVideo.empty() ? inputPath.stem().wstring() : matchedVideo.stem().wstring();
        outputBaseStem = StripKnownLanguageSuffix(outputBaseStem);

        fs::path out = inputPath.parent_path() / (outputBaseStem + outputExt);
        out = EnsureUniqueOutputPath(out);

        if (!WriteUtf8File(out, newContent)) {
            return {false, L"변환 파일 저장에 실패했습니다."};
        }
        return {true, L""};
    }

    std::vector<Caption> captions = ParseCaptionsByExtension(inputExt, content);
    if (captions.empty()) {
        return {false, L"변환 가능한 자막 구간을 찾지 못했습니다."};
    }

    RemoveCreditCaptions(captions);
    if (captions.empty()) {
        return {false, L"크레딧(제작자 정보) 제거 후 변환 가능한 자막이 없습니다."};
    }
    ResolveTextBasedLanguages(captions);

    std::map<std::string, std::vector<Caption>> byLang;
    for (const Caption& caption : captions) {
        std::string lang = caption.lang.empty() ? "und" : caption.lang;
        byLang[lang].push_back(caption);
    }

    fs::path matchedVideo = ResolveMatchedVideoPath(inputPath, bareEpisodes);
    std::wstring outputBaseStem = matchedVideo.empty() ? inputPath.stem().wstring() : matchedVideo.stem().wstring();
    outputBaseStem = StripKnownLanguageSuffix(outputBaseStem);

    int playResX = 1920;
    int playResY = 1080;
    int baseFontSize = 75;
    if (target == TargetFormat::ToAss && !matchedVideo.empty()) {
        int videoW = 0;
        int videoH = 0;
        if (GetVideoDimensions(matchedVideo, videoW, videoH) && videoW > 0 && videoH > 0) {
            playResX = videoW;
            playResY = videoH;
            baseFontSize = std::max(10, static_cast<int>(std::lround(75.0 * videoH / 1080.0)));
        }
    }

    // SMI's font-weight often lives only in the <STYLE> CSS block (per class
    // or as a file-wide "P" default) rather than as inline <b> tags, so it
    // has to be read from the source once up front and threaded into the
    // ASS build for every language group below.
    SmiCssBoldRules cssBoldRules;
    if (inputExt == L".smi" && target == TargetFormat::ToAss) {
        cssBoldRules = ParseSmiCssBoldRules(content);
    }

    for (auto& pair : byLang) {
        auto& items = pair.second;
        if (items.empty()) {
            continue;
        }
        std::sort(items.begin(), items.end(), [](const Caption& left, const Caption& right) {
            return left.startMs < right.startMs;
        });

        std::wstring outText = BuildOutputText(target, items, playResX, playResY, baseFontSize, cssBoldRules);

        fs::path out = inputPath.parent_path() / outputBaseStem;
        if (pair.first != "und") {
            out += L"." + MultiByteToWide(pair.first, CP_UTF8);
        }
        out += outputExt;
        out = EnsureUniqueOutputPath(out);

        if (!WriteUtf8File(out, outText)) {
            return {false, L"변환 파일 저장에 실패했습니다."};
        }
    }

    return {true, L""};
}

std::wstring JoinFailures(const std::vector<std::wstring>& failed) {
    std::wstring joined;
    for (size_t i = 0; i < failed.size(); ++i) {
        joined += failed[i];
        if (i + 1 < failed.size()) {
            joined += L"\n";
        }
    }
    return joined;
}

void DebugLog(const std::wstring& message) {
    const char* env = std::getenv("SUBCONVERTER_DEBUG");
    if (!env || env[0] == '\0') {
        env = std::getenv("SMI2SRT_DEBUG");
    }
    if (!env || env[0] == '\0') {
        return;
    }

    wchar_t tmp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    std::wstring path = tmp;
    if (!path.empty() && path.back() != L'\\') {
        path += L'\\';
    }
    path += L"subConverter_debug.log";

    HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return;
    }

    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t prefix[64] = {};
    swprintf_s(prefix, L"[%04d-%02d-%02d %02d:%02d:%02d] ",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    std::wstring line = std::wstring(prefix) + message + L"\r\n";
    DWORD wrote = 0;
    WriteFile(h, line.data(), static_cast<DWORD>(line.size() * sizeof(wchar_t)), &wrote, nullptr);
    CloseHandle(h);
}

// ─────────────────────────────────────────────────────────────────────────────
// Multi-instance file queue
// Explorer launches one process per file when registered with %1.
// The first process becomes the "collector": it waits while the other
// instances each enqueue their file path into a shared temp directory, then
// exit immediately.  The collector drains the queue and processes every file
// in one batch, showing a single summary notification.
//
// For large multi-selects (10+ files) Explorer does not necessarily spawn all
// the per-file processes at once — they can arrive in separate bursts a few
// hundred ms apart. A fixed sleep after the first arrival can elapse before
// the stragglers enqueue, so the collector drains early and the selection
// gets split into multiple output batches (e.g. 24 files becoming 12+9+3).
// Instead of a fixed delay, poll the queue file and keep waiting as long as
// it keeps growing, only draining once it has been quiet for
// COLLECT_QUIET_MS — capped at COLLECT_MAX_MS so a wedged instance can't hang
// the collector indefinitely.
// ─────────────────────────────────────────────────────────────────────────────

static constexpr DWORD COLLECT_POLL_MS = 50;
static constexpr DWORD COLLECT_QUIET_MS = 300;
static constexpr DWORD COLLECT_MAX_MS = 4000;
static const wchar_t* COLLECTOR_MUTEX_NAME = L"Local\\smi2srt_collector";
static const wchar_t* QUEUE_WRITE_MUTEX_NAME = L"Local\\smi2srt_queue_writer";
static const wchar_t* QUEUE_FILE_NAME = L"smi2srt_queue.txt";

std::wstring GetQueueFilePath(const wchar_t* queueFileName) {
    wchar_t tmp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    std::wstring path = tmp;
    if (!path.empty() && path.back() != L'\\') {
        path += L'\\';
    }
    path += queueFileName;
    return path;
}

void EnqueueFiles(const std::vector<fs::path>& files, const wchar_t* writeMutexName, const wchar_t* queueFileName) {
    HANDLE writeMutex = CreateMutexW(nullptr, FALSE, writeMutexName);
    if (!writeMutex) {
        return;
    }

    WaitForSingleObject(writeMutex, INFINITE);

    std::wstring queuePath = GetQueueFilePath(queueFileName);
    HANDLE h = CreateFileW(queuePath.c_str(), FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        ReleaseMutex(writeMutex);
        CloseHandle(writeMutex);
        return;
    }

    for (const auto& p : files) {
        std::wstring line = p.wstring() + L"\n";
        DWORD wrote = 0;
        WriteFile(h,
                  line.data(),
                  static_cast<DWORD>(line.size() * sizeof(wchar_t)),
                  &wrote,
                  nullptr);
    }
    CloseHandle(h);

    ReleaseMutex(writeMutex);
    CloseHandle(writeMutex);
}

// Blocks the collector until the queue file stops growing (no straggler
// process has appended a new path for COLLECT_QUIET_MS), or until
// COLLECT_MAX_MS total has elapsed, whichever comes first.
void WaitForQueueToSettle(const wchar_t* queueFileName) {
    std::wstring queuePath = GetQueueFilePath(queueFileName);
    DWORD elapsedMs = 0;
    DWORD quietMs = 0;
    LONGLONG lastSize = -1;

    while (elapsedMs < COLLECT_MAX_MS) {
        Sleep(COLLECT_POLL_MS);
        elapsedMs += COLLECT_POLL_MS;

        WIN32_FILE_ATTRIBUTE_DATA data{};
        LONGLONG curSize = 0;
        if (GetFileAttributesExW(queuePath.c_str(), GetFileExInfoStandard, &data)) {
            curSize = (static_cast<LONGLONG>(data.nFileSizeHigh) << 32) | static_cast<LONGLONG>(data.nFileSizeLow);
        }

        if (curSize != lastSize) {
            lastSize = curSize;
            quietMs = 0;
        } else {
            quietMs += COLLECT_POLL_MS;
            if (quietMs >= COLLECT_QUIET_MS) {
                return;
            }
        }
    }
}

std::vector<fs::path> DrainQueue(const wchar_t* queueFileName, bool filterSubtitleExt) {
    std::wstring queuePath = GetQueueFilePath(queueFileName);
    std::vector<fs::path> result;

    HANDLE hFile = CreateFileW(queuePath.c_str(), GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return result;
    }

    std::wstring fileText;
    wchar_t buf[2048];
    DWORD read = 0;
    while (ReadFile(hFile, buf, sizeof(buf), &read, nullptr) && read > 0) {
        size_t chars = static_cast<size_t>(read / sizeof(wchar_t));
        fileText.append(buf, buf + chars);
    }
    CloseHandle(hFile);
    DeleteFileW(queuePath.c_str());

    std::wstringstream stream(fileText);
    std::wstring line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == L'\r') { line.pop_back(); }
        line = Trim(line);
        if (!line.empty()) {
            fs::path p(line);
            if (!filterSubtitleExt || IsSubtitleExtension(p.extension().wstring())) {
                result.push_back(p);
            }
        }
    }

    return result;
}

unsigned int ResolveWorkerCount(size_t fileCount) {
    if (fileCount <= 1) {
        return 1;
    }

    unsigned int hardware = std::thread::hardware_concurrency();
    if (hardware == 0) {
        hardware = 4;
    }

    unsigned int workerCount = std::min<unsigned int>(hardware, static_cast<unsigned int>(fileCount));

    const char* env = std::getenv("SUBCONVERTER_THREADS");
    if (!env || !*env) {
        env = std::getenv("SMI2SRT_THREADS");
    }
    if (env && *env) {
        char* parseEnd = nullptr;
        long custom = std::strtol(env, &parseEnd, 10);
        if (parseEnd != env && custom > 0) {
            workerCount = std::min<unsigned int>(
                static_cast<unsigned int>(custom),
                static_cast<unsigned int>(fileCount));
        }
    }

    return std::max(1u, workerCount);
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool silentMode = false;
    TargetFormat target = TargetFormat::ToSrt;
    DebugLog(L"launch argc=" + std::to_wstring(argc));

    if (!argv || argc <= 1) {
        if (!silentMode) {
            MessageBoxW(nullptr,
                L"사용법:\n파일 탐색기에서 .smi/.srt/.ass 파일을 선택하고 subConverter 메뉴에서 변환하세요.",
                L"subConverter",
                MB_OK | MB_ICONINFORMATION);
        }
        if (argv) {
            LocalFree(argv);
        }
        return 0;
    }

    std::vector<fs::path> inputFiles;
    inputFiles.reserve(static_cast<size_t>(argc));
    int skippedCount = 0;

    for (int i = 1; i < argc; ++i) {
        DebugLog(L"arg=" + std::wstring(argv[i]));
        std::wstring arg = ToLowerW(argv[i]);
        if (arg == L"/silent" || arg == L"-silent" || arg == L"--silent") {
            silentMode = true;
            continue;
        }

        bool isTargetArg = false;
        TargetFormat parsedTarget = ParseTargetFormatOrDefault(argv[i], &isTargetArg);
        if (isTargetArg) {
            target = parsedTarget;
            continue;
        }

        fs::path path(argv[i]);
        if (IsSubtitleExtension(path.extension().wstring())) {
            inputFiles.push_back(path);
        } else {
            ++skippedCount;
        }
    }

    if (inputFiles.empty()) {
        DebugLog(L"inputFiles=0");
        LocalFree(argv);
        return 0;
    }

    // Explorer may launch one process per selected file when command uses %1.
    // Aggregate those launches into one batch so users get one summary result.
    HANDLE collectorMutex = CreateMutexW(nullptr, TRUE, COLLECTOR_MUTEX_NAME);
    if (collectorMutex) {
        DWORD lastErr = GetLastError();
        if (lastErr == ERROR_ALREADY_EXISTS) {
            EnqueueFiles(inputFiles, QUEUE_WRITE_MUTEX_NAME, QUEUE_FILE_NAME);
            CloseHandle(collectorMutex);
            LocalFree(argv);
            return 0;
        }

        EnqueueFiles(inputFiles, QUEUE_WRITE_MUTEX_NAME, QUEUE_FILE_NAME);
        WaitForQueueToSettle(QUEUE_FILE_NAME);
        inputFiles = DrainQueue(QUEUE_FILE_NAME, true);

        ReleaseMutex(collectorMutex);
        CloseHandle(collectorMutex);

        if (inputFiles.empty()) {
            LocalFree(argv);
            return 0;
        }
    }

    DebugLog(L"inputFiles=" + std::to_wstring(inputFiles.size()));

    // Computed once up front (read-only afterwards) so every worker thread
    // can consult the same batch-wide episode inference without re-scanning
    // each directory per file.
    std::map<std::wstring, int> bareEpisodes = BuildBareEpisodeNumberMap(inputFiles);

    std::atomic<int> success(0);
    std::vector<std::wstring> failedFiles;
    std::mutex failedMutex;

    unsigned int workerCount = ResolveWorkerCount(inputFiles.size());
    if (workerCount <= 1) {
        for (const fs::path& path : inputFiles) {
            ConvertResult result = ConvertSingleFile(path, target, bareEpisodes);
            if (result.ok) {
                success.fetch_add(1);
            } else {
                failedFiles.push_back(path.filename().wstring() + L": " + result.message);
            }
        }
    } else {
        std::atomic<size_t> nextIndex(0);
        std::vector<std::thread> workers;
        workers.reserve(workerCount);

        auto worker = [&]() {
            while (true) {
                size_t current = nextIndex.fetch_add(1);
                if (current >= inputFiles.size()) {
                    break;
                }

                const fs::path& path = inputFiles[current];
                ConvertResult result = ConvertSingleFile(path, target, bareEpisodes);
                if (result.ok) {
                    success.fetch_add(1);
                } else {
                    std::lock_guard<std::mutex> lock(failedMutex);
                    failedFiles.push_back(path.filename().wstring() + L": " + result.message);
                }
            }
        };

        for (unsigned int i = 0; i < workerCount; ++i) {
            workers.emplace_back(worker);
        }
        for (std::thread& thread : workers) {
            thread.join();
        }
    }

    std::sort(failedFiles.begin(), failedFiles.end());

    int successCount = success.load();
    int fail = static_cast<int>(failedFiles.size());
    DebugLog(L"result success=" + std::to_wstring(successCount) + L" fail=" + std::to_wstring(fail));
    if (successCount == 0 && fail == 0) {
        LocalFree(argv);
        return 0;
    }

    std::wstring summary;
    if (successCount > 0) {
        summary += L"성공: " + std::to_wstring(successCount) + L"개";
    }
    if (fail > 0) {
        if (!summary.empty()) {
            summary += L"\n";
        }
        summary += L"실패: " + std::to_wstring(fail) + L"개";
    }
    if (fail > 0) {
        summary += L"\n\n" + JoinFailures(failedFiles);
    }
    if (skippedCount > 0) {
        summary += L"\n";
        summary += L"건너뜀(자막 아님): " + std::to_wstring(skippedCount) + L"개";
    }

    if (!silentMode) {
        MessageBoxW(nullptr,
            summary.c_str(),
            L"subConverter 변환 결과",
            MB_OK | (fail > 0 ? MB_ICONWARNING : MB_ICONINFORMATION));
    }

    LocalFree(argv);
    return fail == 0 ? 0 : 1;
}
