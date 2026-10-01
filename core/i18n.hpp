#pragma once
#include <cstddef>
#include <initializer_list>
#include <string>
#include <utility>
namespace i18n {
struct Entry {
    const char *key;
    const char *text;
};
struct Locale {
    const char *id;
    const char *name;
    const char *font;
    const Entry *entries;
    size_t count;
};
extern const Locale locales[];
extern const size_t locale_count;
extern const char *default_locale;
extern const char *fallback_locale;
const Locale &current();
bool set_locale(const std::string &id);
const char *text(const char *key);
std::string format(const char *key,
                   std::initializer_list<std::pair<std::string, std::string>> values);
} // namespace i18n
