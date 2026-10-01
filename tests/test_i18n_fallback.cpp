#include "i18n.hpp"
#include <cassert>
#include <string>
// Isolated third-locale fixture verifies extension and actual missing-translation fallback.
namespace i18n {
static const Entry english[] = {{"button", "Hold"}, {"message", "Value {value}"}};
static const Entry partial[] = {{"button", "Halten"}};
const Locale locales[] = {{"en", "English", "latin", english, 2},
                          {"de-test", "Deutsch", "latin", partial, 1}};
const size_t locale_count = 2;
const char *default_locale = "de-test";
const char *fallback_locale = "en";
} // namespace i18n
int main() {
    assert(std::string(i18n::current().id) == "de-test");
    assert(std::string(i18n::text("button")) == "Halten");
    assert(std::string(i18n::text("message")) == "Value {value}");
    assert(i18n::format("message", {{"value", "12.3"}}) == "Value 12.3");
    assert(std::string(i18n::text("absent")) == "absent");
}
