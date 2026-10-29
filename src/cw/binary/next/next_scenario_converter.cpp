#include "cw/binary/next/next_scenario_converter.hpp"

#include "cw/cw.hpp"
#include "cw/lib/encoding.hpp"
#include "cwxeditor_src/core/cwx/cwl.hpp"
#include "cwxeditor_src/core/cwx/filesync.hpp"
#include "cwxeditor_src/core/cwx/props.hpp"
#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/core/cwx/system.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace cw::binary::next {
namespace {

std::string lower_extension(const std::filesystem::path& path) {
    auto value = path.extension().string();
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

void copy_non_binary_resources(const std::filesystem::path& source,
                               const std::filesystem::path& destination) {
    std::filesystem::create_directories(destination);
    for (const auto& entry : std::filesystem::recursive_directory_iterator(source)) {
        const auto relative = std::filesystem::relative(entry.path(), source);
        const auto target = destination / relative;
        if (entry.is_directory()) {
            std::filesystem::create_directories(target);
            continue;
        }
        if (!entry.is_regular_file()) continue;
        const auto extension = lower_extension(entry.path());
        if (extension == ".wsm" || extension == ".wid") continue;
        std::filesystem::create_directories(target.parent_path());
        std::filesystem::copy_file(entry.path(), target,
                                   std::filesystem::copy_options::overwrite_existing);
    }
}

}  // namespace

ConversionResult convert_scenario(const ::cw::lib::String& source_directory,
                                  const ::cw::lib::String& destination_directory,
                                  const ::cw::lib::String& skin_type,
                                  const ::cw::lib::String& skin_name) {
    const auto source = std::filesystem::u8path(source_directory);
    const auto destination = std::filesystem::u8path(destination_directory);
    if (!std::filesystem::is_regular_file(source / "Summary.wsm"))
        throw std::runtime_error("CardWirthNext Summary.wsm is missing: " + source_directory);

    copy_non_binary_resources(source, destination);

    cwxeditor::cwx::system::System system;
    cwxeditor::cwx::props::CProps properties(::cw::g_exepath, system);
    cwxeditor::cwx::summary::LoadOption load_option;
    // 変換結果を一定にし、診断コールバックを直列化する。
    // 生成XMLは現在のシナリオ用にキャッシュするため
    // 、ここでは並列解析より起動時の正確さを優先する。
    load_option.doubleIO = false;
    load_option.numberStepToVariantThreshold = 0;
    std::vector<std::pair<std::string, std::string>> issues;
    load_option.issueFunc = [&issues](const std::string member, const std::string reason) {
        issues.emplace_back(member, reason);
    };
    cwxeditor::lib::Array<cwxeditor::lib::String> errors;
    std::int32_t data_version = 0;
    auto summary = cwxeditor::cwx::cwl::loadLScenario(
        source_directory, skin_type, skin_name, properties, load_option,
        errors, data_version);
    if (!summary || data_version < 7)
        throw std::runtime_error("scenario is not a CardWirthNext data-version 7 scenario: " +
                                 source_directory);
    if (!errors.empty()) {
        std::ostringstream message;
        message << "CardWirthNext resource conversion failed (" << errors.size() << "): ";
        for (std::size_t i = 0; i < errors.size(); ++i) {
            if (i != 0) message << ", ";
            message << errors[i];
            const auto issue = std::find_if(issues.begin(), issues.end(), [&](const auto& value) {
                return value.first == errors[i];
            });
            if (issue != issues.end() && !issue->second.empty())
                message << " [" << issue->second << ']';
        }
        throw std::runtime_error(message.str());
    }

    cwxeditor::cwx::summary::SaveOption save_option;
    save_option.dataVersion = 7;
    save_option.doubleIO = true;
    save_option.backup = false;
    save_option.saveChangedOnly = false;
    save_option.saveSkinName = true;
    {
        cwxeditor::cwx::filesync::FileSync sync;
        sync.start();
        summary->saveXMLs(destination_directory, system, save_option, sync);
    }
    if (!std::filesystem::is_regular_file(destination / "Summary.xml"))
        throw std::runtime_error("CardWirthNext conversion did not create Summary.xml: " +
                                 destination_directory);
    return {destination_directory, data_version, 0};
}

}  // namespace cw::binary::next
