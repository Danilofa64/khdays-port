// The interface's words: every one in the five
// languages, placeholders filled.
#include "test.h"

#include "strings.h"

#include <cstdio>
#include <string>

using namespace studio;

TEST(every_word_in_every_language) {
    int missing = 0;
    for (int l = 0; l < static_cast<int>(Language::count); ++l) {
        for (int s = 0; s < static_cast<int>(S::count); ++s) {
            if (!translated(static_cast<Language>(l), static_cast<S>(s))) {
                std::fprintf(stderr, "  no %s word for string %d\n", language_code(static_cast<Language>(l)), s);
                ++missing;
            }
        }
    }
    CHECK(missing == 0);
}

TEST(placeholders) {
    CHECK(fmt(Language::en, S::saved, {"mods/x.png"}) == "Saved: mods/x.png");
    CHECK(fmt(Language::es, S::size, {"16", "8"}) == "16 x 8");
    CHECK(fmt(Language::en, S::size, {"16"}) == "16 x ");  // a missing argument is left empty
}

TEST(language_codes) {
    CHECK(language_from_code("es", Language::en) == Language::es);
    CHECK(language_from_code("de-AT", Language::en) == Language::de);
    CHECK(language_from_code("pt", Language::en) == Language::en);
    CHECK(std::string(language_code(Language::it)) == "it");
}
