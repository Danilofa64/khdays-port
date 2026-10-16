#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "khdays/assets/message.h"

namespace {

using Bytes = std::vector<std::uint8_t>;

// Wrap bytes in an all-literal LZ11 stream (valid input for lz_decompress).
Bytes lz11_store(const Bytes& data) {
    Bytes out;
    out.push_back(0x11U);
    out.push_back(static_cast<std::uint8_t>(data.size() & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((data.size() >> 8U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((data.size() >> 16U) & 0xFFU));
    for (std::size_t i = 0U; i < data.size(); i += 8U) {
        out.push_back(0x00U);  // flag: next 8 are literals
        for (std::size_t j = i; j < i + 8U && j < data.size(); ++j) {
            out.push_back(data[j]);
        }
    }
    return out;
}

void expect(bool ok, const char* what) {
    if (!ok) {
        throw std::runtime_error(what);
    }
}

}  // namespace

int main() {
    try {
        // An all-literal LZ11 stream decompresses to its bytes.
        const Bytes plain{'K', 'H', 0x00U, 0x11U, 0xFFU, '3', '5', '8', '/', '2'};
        expect(khdays::assets::lz_decompress(lz11_store(plain)) == plain, "LZ11 literals");

        expect(
            khdays::assets::message_to_utf8(u"A\nB") == "A\nB",
            "utf8 newline passthrough");

        // Text override escapes: message_from_utf8 decodes what
        // message_to_utf8 emits, and both round-trip control codes/newlines.
        expect(
            khdays::assets::message_from_utf8("Piro\\nPlus") == u"Piro\nPlus",
            "from_utf8 newline escape");
        expect(
            khdays::assets::message_from_utf8("A\\x01B")
                == std::u16string{u'A', u'\x01', u'B'},
            "from_utf8 hex escape");
        {
            const std::u16string original{u'H', u'i', u'\n', u'\x03', u'!'};
            const auto round =
                khdays::assets::message_from_utf8(
                    khdays::assets::message_to_utf8(original));
            expect(round == original, "utf8 round-trip");
        }

        std::cout << "Message test passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Message test failed: " << error.what() << '\n';
        return 1;
    }
}
