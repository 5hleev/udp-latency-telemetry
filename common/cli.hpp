// common/cli.hpp - разбор аргументов вида --key value с проверкой диапазонов.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace common {

class ArgParser {
public:
    // flags - ключи без значения (например, --quiet).
    ArgParser(int argc, char** argv, std::set<std::string> flags = {});

    bool Has(const std::string& key) const;
    std::string GetString(const std::string& key, const std::string& fallback);
    std::int64_t GetInt(const std::string& key, std::int64_t fallback, std::int64_t min, std::int64_t max);
    double GetDouble(const std::string& key, double fallback, double min, double max);

    // Проверяет, что не осталось неизвестных ключей. Ошибки накапливаются.
    void RejectUnknown(const std::set<std::string>& known);
    const std::vector<std::string>& Errors() const { return errors_; }
    bool Ok() const { return errors_.empty(); }

private:
    std::map<std::string, std::string> values_;
    std::vector<std::string> errors_;
};

std::optional<std::int64_t> ParseInt(const std::string& text);
std::optional<double> ParseDouble(const std::string& text);

}  // namespace common
