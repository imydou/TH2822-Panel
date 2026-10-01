#include "i18n.hpp"
#include <cassert>
#include <iostream>
#include <string>
int main() {
    assert(std::string(i18n::current().id) == "zh-CN");
    assert(std::string(i18n::text("button.hold")) == "保持");
    assert(i18n::format("error.timeout", {{"detail", "FREQ?"}}).find("FREQ?") != std::string::npos);
    assert(i18n::set_locale("en"));
    assert(std::string(i18n::text("button.hold")) == "HOLD");
    assert(!i18n::set_locale("missing"));
    assert(std::string(i18n::current().id) == "en");
    assert(std::string(i18n::text("unknown.key")) == "unknown.key");
    for (size_t l = 0; l < i18n::locale_count; ++l) {
        assert(i18n::set_locale(i18n::locales[l].id));
        for (size_t k = 0; k < i18n::locales[l].count; ++k)
            assert(*i18n::text(i18n::locales[l].entries[k].key));
    }
    std::cout << "PASS locale registry, default, selection, invalid locale, format, missing key "
                 "fallback\n";
}
