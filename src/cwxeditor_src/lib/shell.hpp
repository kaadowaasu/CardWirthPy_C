#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace cwxeditor::lib {

class ShellBackend {
public:
    virtual ~ShellBackend() = default;
    virtual bool open_path(const std::filesystem::path& path, std::string& error) = 0;
    virtual bool reveal_path(const std::filesystem::path& path, std::string& error) = 0;
    virtual bool open_url(std::string_view url, std::string& error) = 0;
    [[nodiscard]] virtual std::string associated_command(const std::filesystem::path& path) const = 0;
};

}  // namespace cwxeditor::lib
