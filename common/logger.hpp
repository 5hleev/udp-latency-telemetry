// common/logger.hpp - журнал в консоль и (опционально) в файл с меткой времени.
#pragma once

#include <fstream>
#include <string>

namespace common {

class Logger {
public:
    // filePath пустой - только консоль. append = false - файл перезаписывается.
    explicit Logger(const std::string& filePath = {}, bool append = true);

    void Info(const std::string& message);
    void Warn(const std::string& message);
    // Только в файл (подробные записи о каждом пакете, чтобы не засорять консоль).
    void Detail(const std::string& message);

    void SetConsoleDetails(bool enabled) { consoleDetails_ = enabled; }
    bool FileOk() const { return !wantFile_ || file_.is_open(); }

private:
    void Write(const char* level, const std::string& message, bool toConsole);

    std::ofstream file_;
    bool wantFile_ = false;
    bool consoleDetails_ = false;
};

// Включает UTF-8 в консоли Windows, чтобы русский текст выводился корректно.
void SetupConsole();

}  // namespace common
