#include "fonts.h"

extern "C" {
#include "../../host/app/host.h"
#include "../../host/render/png_io.h"
}
extern "C" int khdays_mods_find(const char *relative, char *path, int size);

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace khdays::hd_fonts {
namespace {

struct Value {
    enum Type { Null, Boolean, Integer, String, Array, Object } type = Null;
    std::int64_t number = 0;
    std::string str;
    std::vector<Value> array;
    std::map<std::string, Value> object;
};

// Deliberately small, strict JSON reader. Metadata uses integers, strings,
// objects, and arrays. Unknown optional keys are accepted and recursively read.
class Reader {
public:
    explicit Reader(std::string data) : text_(std::move(data)) {}
    Value read() {
        Value value = parse(0);
        skip_space();
        if (pos_ != text_.size()) fail("trailing content");
        return value;
    }
private:
    std::string text_;
    std::size_t pos_ = 0;
    void fail(const char *why) const { throw std::runtime_error(std::string(why) + " at JSON byte " + std::to_string(pos_)); }
    void skip_space() {
        while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\n' ||
               text_[pos_] == '\r' || text_[pos_] == '\t')) ++pos_;
    }
    bool take(char ch) {
        skip_space();
        if (pos_ < text_.size() && text_[pos_] == ch) { ++pos_; return true; }
        return false;
    }
    void expect(char ch) { if (!take(ch)) fail("unexpected JSON character"); }
    std::string string() {
        expect('"');
        std::string out;
        while (pos_ < text_.size()) {
            const unsigned char c = static_cast<unsigned char>(text_[pos_++]);
            if (c == '"') return out;
            if (c < 0x20) fail("control character in JSON string");
            if (c != '\\') { out.push_back(static_cast<char>(c)); continue; }
            if (pos_ >= text_.size()) fail("unterminated JSON escape");
            const char escaped = text_[pos_++];
            switch (escaped) {
            case '"': case '\\': case '/': out.push_back(escaped); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'u': {
                if (pos_ + 4 > text_.size()) fail("short Unicode escape");
                unsigned code = 0;
                for (int i = 0; i < 4; ++i) {
                    const char h = text_[pos_++];
                    if (h >= '0' && h <= '9') code = (code << 4) | static_cast<unsigned>(h - '0');
                    else if (h >= 'a' && h <= 'f') code = (code << 4) | static_cast<unsigned>(h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F') code = (code << 4) | static_cast<unsigned>(h - 'A' + 10);
                    else fail("invalid Unicode escape");
                }
                if (code < 0x80) out.push_back(static_cast<char>(code));
                else if (code < 0x800) { out.push_back(static_cast<char>(0xC0 | (code >> 6)));
                                        out.push_back(static_cast<char>(0x80 | (code & 63))); }
                else { out.push_back(static_cast<char>(0xE0 | (code >> 12)));
                       out.push_back(static_cast<char>(0x80 | ((code >> 6) & 63)));
                       out.push_back(static_cast<char>(0x80 | (code & 63))); }
                break;
            }
            default: fail("unknown JSON escape");
            }
        }
        fail("unterminated JSON string");
        return {};
    }
    Value parse(int depth) {
        if (depth > 32) fail("JSON nesting too deep");
        skip_space();
        if (pos_ >= text_.size()) fail("unexpected end of JSON");
        Value value;
        const char ch = text_[pos_];
        if (ch == '{') {
            ++pos_; value.type = Value::Object;
            if (take('}')) return value;
            do {
                skip_space();
                if (pos_ >= text_.size() || text_[pos_] != '"') fail("object key must be a string");
                std::string key = string();
                expect(':');
                auto entry = value.object.emplace(std::move(key), parse(depth + 1));
                if (!entry.second) fail("duplicate JSON object key");
                if (take('}')) return value;
                expect(',');
            } while (true);
        }
        if (ch == '[') {
            ++pos_; value.type = Value::Array;
            if (take(']')) return value;
            do {
                value.array.push_back(parse(depth + 1));
                if (take(']')) return value;
                expect(',');
            } while (true);
        }
        if (ch == '"') { value.type = Value::String; value.str = string(); return value; }
        if (text_.compare(pos_, 4, "null") == 0) { pos_ += 4; return value; }
        if (text_.compare(pos_, 4, "true") == 0) { pos_ += 4; value.type = Value::Boolean; return value; }
        if (text_.compare(pos_, 5, "false") == 0) { pos_ += 5; value.type = Value::Boolean; return value; }
        if (ch == '-' || (ch >= '0' && ch <= '9')) {
            const std::size_t begin = pos_;
            if (text_[pos_] == '-') ++pos_;
            if (pos_ >= text_.size()) fail("invalid JSON number");
            if (text_[pos_] == '0') ++pos_;
            else {
                if (text_[pos_] < '1' || text_[pos_] > '9') fail("invalid JSON number");
                while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
            }
            if (pos_ < text_.size() && (text_[pos_] == '.' || text_[pos_] == 'e' || text_[pos_] == 'E'))
                fail("font atlas metadata requires integer numbers");
            try { value.number = std::stoll(text_.substr(begin, pos_ - begin)); }
            catch (...) { fail("JSON integer out of range"); }
            value.type = Value::Integer;
            return value;
        }
        fail("invalid JSON value");
        return {};
    }
};

const Value &field(const Value &obj, const char *name, Value::Type type) {
    if (obj.type != Value::Object) throw std::runtime_error("expected JSON object");
    const auto it = obj.object.find(name);
    if (it == obj.object.end() || it->second.type != type)
        throw std::runtime_error(std::string("missing or invalid field: ") + name);
    return it->second;
}
int integer(const Value &v, const char *name, int lo, int hi) {
    const auto n = field(v, name, Value::Integer).number;
    if (n < lo || n > hi) throw std::runtime_error(std::string("out of range: ") + name);
    return static_cast<int>(n);
}
std::string text_file(const std::filesystem::path &path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) throw std::runtime_error("cannot open metadata JSON");
    const auto size = f.tellg();
    if (size < 0 || size > 16 * 1024 * 1024) throw std::runtime_error("metadata JSON too large");
    std::string data(static_cast<std::size_t>(size), '\0');
    f.seekg(0);
    if (!data.empty() && !f.read(data.data(), size)) throw std::runtime_error("cannot read metadata JSON");
    return data;
}

std::shared_ptr<const Atlas> read_atlas(const std::string &resource, const std::filesystem::path &png_path) {
    std::filesystem::path json_path = png_path;
    json_path.replace_extension(".json"); // foo.nftr.png -> foo.nftr.json
    const Value doc = Reader(text_file(json_path)).read();
    const auto &src = field(doc, "source", Value::String).str;
    if (src != resource) throw std::runtime_error("JSON source does not match original NFTR path");
    if (field(doc, "format", Value::String).str != "NFTR") throw std::runtime_error("JSON is not an NFTR atlas");
    const auto &reference = field(doc, "atlas", Value::Object);
    auto output = std::make_shared<Atlas>();
    output->source = resource;
    output->png_path = png_path.string();
    output->reference_width = integer(reference, "width", 1, 16384);
    output->reference_height = integer(reference, "height", 1, 16384);
    output->cell_width = integer(doc, "cell_width", 1, 256);
    output->cell_height = integer(doc, "cell_height", 1, 256);
    output->baseline = integer(doc, "baseline", -256, 256);
    output->line_feed = integer(doc, "line_feed", 0, 256);
    output->max_width = integer(doc, "max_width", 0, 256);

    const Value &glyphs = field(doc, "glyphs", Value::Array);
    if (glyphs.array.empty() || glyphs.array.size() > 16384) throw std::runtime_error("invalid glyph count");
    output->glyphs.resize(glyphs.array.size());
    std::vector<bool> seen(glyphs.array.size());
    for (const Value &g : glyphs.array) {
        const int id = integer(g, "index", 0, static_cast<int>(glyphs.array.size()) - 1);
        if (seen[id]) throw std::runtime_error("duplicate glyph index");
        seen[id] = true;
        Glyph &dst = output->glyphs[id];
        dst.x = integer(g, "x", 0, output->reference_width);
        dst.y = integer(g, "y", 0, output->reference_height);
        dst.w = integer(g, "w", 1, output->reference_width);
        dst.h = integer(g, "h", 1, output->reference_height);
        if (dst.w > output->reference_width - dst.x || dst.h > output->reference_height - dst.y)
            throw std::runtime_error("glyph rectangle exceeds reference atlas");
        dst.left = integer(g, "left", -1024, 1024);
        dst.width = integer(g, "width", 0, 1024);
        dst.advance = integer(g, "advance", -1024, 1024);
    }
    const Value &charmap = field(doc, "charmap", Value::Array);
    for (const Value &m : charmap.array) {
        const auto code = integer(m, "codepoint", 0, 0x10ffff);
        const auto glyph = integer(m, "glyph", 0, static_cast<int>(glyphs.array.size()) - 1);
        output->charmap.push_back({static_cast<std::uint32_t>(code), static_cast<std::uint32_t>(glyph)});
    }

    int iw = 0, ih = 0;
    const char *error = nullptr;
    std::unique_ptr<std::uint8_t, decltype(&khdays_png_free)> rgba(
        khdays_png_read(png_path.string().c_str(), &iw, &ih, &error), &khdays_png_free);
    if (!rgba) throw std::runtime_error(std::string("PNG read failed: ") + (error ? error : "unknown"));
    if (iw < 1 || ih < 1 || iw > 16384 || ih > 16384 ||
        static_cast<std::int64_t>(iw) * output->reference_height !=
        static_cast<std::int64_t>(ih) * output->reference_width)
        throw std::runtime_error("PNG dimensions must be a uniform scale of the JSON reference atlas");
    output->width = iw;
    output->height = ih;
    // Keep logical glyph sizes untouched. Only sampling rectangles scale.
    const double sx = static_cast<double>(iw) / output->reference_width;
    const double sy = static_cast<double>(ih) / output->reference_height;
    for (Glyph &g : output->glyphs) {
        g.image_x = g.x * sx;
        g.image_y = g.y * sy;
        g.image_w = g.w * sx;
        g.image_h = g.h * sy;
    }
    const auto byte_count = static_cast<std::size_t>(iw) * static_cast<std::size_t>(ih) * 4;
    output->rgba.assign(rgba.get(), rgba.get() + byte_count);
    return output;
}

struct Entry { std::shared_ptr<const Atlas> font; bool found = false; bool invalid = false; };
std::mutex cache_mutex;
std::unordered_map<std::string, Entry> cache;

Entry load_uncached(const std::string &original) {
    // Never let a mod-supplied path escape the fonts subtree.
    if (original.empty() || original.front() == '/' || original.find('\\') != std::string::npos ||
        original.find(':') != std::string::npos || original.find("..") != std::string::npos)
        return {};
    const std::string relative = "fonts/" + original + ".png";
    if (relative.size() > 480) return {};
    char path[4096]{};
    if (!khdays_mods_find(relative.c_str(), path, sizeof(path))) return {};
    try {
        auto font = read_atlas(original, std::filesystem::path(path));
        std::fprintf(stderr, "HD fonts: %s <- %s (%dx%d from %dx%d, %zu glyphs)\n",
                     original.c_str(), path, font->width, font->height,
                     font->reference_width, font->reference_height, font->glyphs.size());
        return {std::move(font), true, false};
    } catch (const std::exception &e) {
        std::fprintf(stderr, "HD fonts: rejected %s: %s (original font stays active)\n", path, e.what());
        return {nullptr, true, true};
    }
}
} // namespace

std::shared_ptr<const Atlas> load(const std::string &original_resource) {
    std::lock_guard<std::mutex> lock(cache_mutex);
    const auto [it, inserted] = cache.try_emplace(original_resource);
    if (inserted) it->second = load_uncached(original_resource);
    return it->second.font;
}
int probe(const std::string &original_resource) {
    std::lock_guard<std::mutex> lock(cache_mutex);
    const auto [it, inserted] = cache.try_emplace(original_resource);
    if (inserted) it->second = load_uncached(original_resource);
    return !it->second.found ? 0 : it->second.invalid ? -1 : 1;
}
} // namespace khdays::hd_fonts
