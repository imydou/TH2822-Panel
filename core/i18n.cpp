#include "i18n.hpp"
#include <cstring>
namespace i18n {
static const Locale *active = nullptr;
static const Locale *find(const std::string &id) {
    for (size_t i = 0; i < locale_count; ++i)
        if (id == locales[i].id)
            return &locales[i];
    return nullptr;
}
const Locale &current() {
    if (!active)
        active = find(default_locale);
    return *active;
}
bool set_locale(const std::string &id) {
    auto *p = find(id);
    if (!p)
        return false;
    active = p;
    return true;
}
static const char *lookup(const Locale &locale, const char *key) {
    for (size_t i = 0; i < locale.count; ++i)
        if (!std::strcmp(key, locale.entries[i].key))
            return locale.entries[i].text;
    return nullptr;
}
const char *text(const char *key) {
    auto *p = lookup(current(), key);
    if (!p)
        p = lookup(*find(fallback_locale), key);
    return p ? p : key;
}
std::string format(const char *key,
                   std::initializer_list<std::pair<std::string, std::string>> values) {
    std::string s = text(key);
    for (auto &v : values) {
        std::string token = "{" + v.first + "}";
        size_t offset = 0;
        while ((offset = s.find(token, offset)) != std::string::npos) {
            s.replace(offset, token.size(), v.second);
            offset += v.second.size();
        }
    }
    return s;
}
} // namespace i18n
