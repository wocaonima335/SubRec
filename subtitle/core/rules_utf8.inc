// Package K private implementation, included inside an anonymous namespace.
// Strict UTF-8: reject incomplete/overlong sequences, surrogates and > U+10FFFF.
struct Rune {
    char32_t value;
    size_t begin, end;
};

bool decodeUtf8(std::string_view text, std::vector<Rune> &out)
{
    out.clear();
    for (size_t i = 0; i < text.size();) {
        const size_t begin = i;
        const auto lead = static_cast<unsigned char>(text[i++]);
        if (lead < 0x80) {
            out.push_back({lead, begin, i});
            continue;
        }
        const size_t extra = lead >= 0xc2 && lead <= 0xdf ? 1 :
                             lead >= 0xe0 && lead <= 0xef ? 2 :
                             lead >= 0xf0 && lead <= 0xf4 ? 3 : 0;
        if (!extra || text.size() - i < extra) return false;
        char32_t cp = lead & (0x7f >> (extra + 1));
        for (size_t j = 0; j < extra; ++j) {
            const auto b = static_cast<unsigned char>(text[i++]);
            if ((b & 0xc0) != 0x80) return false;
            cp = (cp << 6) | (b & 0x3f);
        }
        if ((extra == 1 && cp < 0x80) || (extra == 2 && cp < 0x800) ||
            (extra == 3 && cp < 0x10000) || cp > 0x10ffff ||
            (cp >= 0xd800 && cp <= 0xdfff)) return false;
        out.push_back({cp, begin, i});
    }
    return true;
}
