#include "cwxeditor_src/core/cwx/perf.hpp"
#include "cwxeditor_src/core/cwx/path.hpp"

#include <algorithm>
#include <charconv>
#include <stdexcept>
#include <string>
#include <utility>

namespace cwxeditor::cwx::path {
namespace {

[[nodiscard]] lib::String joinWithSlash(std::string_view owner,
                                         std::string_view child) {
    if (owner.empty()) {
        return lib::String(child);
    }
    lib::String result;
    result.reserve(owner.size() + 1 + child.size());
    result.append(owner);
    result.push_back('/');
    result.append(child);
    return result;
}

[[nodiscard]] lib::String categoryIndex(std::string_view category,
                                         std::size_t index) {
    lib::String result;
    const auto text = std::to_string(index);
    result.reserve(category.size() + 1 + text.size());
    result.append(category);
    result.push_back(':');
    result.append(text);
    return result;
}

[[nodiscard]] lib::String categoryId(std::string_view category,
                                     std::uint64_t id) {
    lib::String result;
    const auto text = std::to_string(id);
    result.reserve(category.size() + 4 + text.size());
    result.append(category);
    result.append(":id:");
    result.append(text);
    return result;
}

[[nodiscard]] int compareStrings(std::string_view left,
                                  std::string_view right) noexcept {
    if (left < right) return -1;
    if (right < left) return 1;
    return 0;
}

[[nodiscard]] std::size_t parseSize(std::string_view value) {
    if (value.empty()) {
        throw std::invalid_argument("CWX path index is empty");
    }

    std::size_t result = 0;
    const auto* begin = value.data();
    const auto* end = begin + value.size();
    const auto parsed = std::from_chars(begin, end, result, 10);
    if (parsed.ec == std::errc::result_out_of_range) {
        throw std::out_of_range("CWX path index is out of range");
    }
    if (parsed.ec != std::errc{} || parsed.ptr != end) {
        throw std::invalid_argument("CWX path index is not an unsigned integer");
    }
    return result;
}

}  // namespace

int cpTypeValue(std::string_view category) noexcept {
    ::cwxeditor::cwx::perf::TraceScope trace_path_d50(__FILE__, __LINE__);
    if (category == "motion") return 0;
    if (category == "dialog") return 1;
    if (category == "text") return 2;
    if (category == "background") return 3;
    if (category == "variable") return 4;
    if (category == "dir") return 5;
    if (category == "flag") return 6;
    if (category == "step") return 7;
    if (category == "variant") return 8;
    if (category == "area") return 9;
    if (category == "battle") return 10;
    if (category == "package") return 11;
    if (category == "castcard") return 12;
    if (category == "skillcard") return 13;
    if (category == "itemcard") return 14;
    if (category == "beastcard") return 15;
    if (category == "infocard") return 16;
    if (category == "playercard") return 17;
    if (category == "menucard") return 18;
    if (category == "enemycard") return 19;
    if (category.empty()) return 20;
    return -1;
}

lib::Array<lib::String> cpsplit(std::string_view path) {
    ::cwxeditor::cwx::perf::TraceScope trace_path_d78(__FILE__, __LINE__);
    const auto body = cpbody(path);
    lib::Array<lib::String> result;
    if (body.empty()) return result;

    std::size_t begin = 0;
    while (true) {
        const auto separator = body.find('/', begin);
        if (separator == std::string_view::npos) {
            result.emplace_back(body.substr(begin));
            break;
        }
        result.emplace_back(body.substr(begin, separator - begin));
        begin = separator + 1;
    }
    return result;
}

int cpcmp(std::span<const lib::String> a, std::span<const lib::String> b) {
    ::cwxeditor::cwx::perf::TraceScope trace_path_d83(__FILE__, __LINE__);
    const auto length = std::max(a.size(), b.size());
    for (std::size_t index = 0; index < length; ++index) {
        ::cwxeditor::cwx::perf::TraceScope trace_path_d84(__FILE__, __LINE__);
        if (index >= a.size()) return -1;
        if (index >= b.size()) return 1;

        const auto ac = cpcategory(a[index]);
        const auto bc = cpcategory(b[index]);
        const auto at = cpTypeValue(ac);
        const auto bt = cpTypeValue(bc);
        if (at != -1 && bt != -1) {
            ::cwxeditor::cwx::perf::TraceScope trace_path_d91(__FILE__, __LINE__);
            if (at < bt) return -1;
            if (bt < at) return 1;
        } else {
            ::cwxeditor::cwx::perf::TraceScope trace_path_d94(__FILE__, __LINE__);
            const auto comparison = compareStrings(ac, bc);
            if (comparison != 0) return comparison;
        }

        const auto ai = cpindex(a[index]);
        const auto bi = cpindex(b[index]);
        if (ai < bi) return -1;
        if (bi < ai) return 1;
    }
    return 0;
}

lib::String cpjoin(const lib::Owner<CWXPath>& owner, std::size_t index, bool id) {
    ::cwxeditor::cwx::perf::TraceScope trace_path_d139(__FILE__, __LINE__);
    return cpjoin(owner, {}, index, id);
}

lib::String cpjoin(const lib::Owner<CWXPath>& owner,
                   std::string_view category,
                   std::size_t index,
                   bool id) {
    ::cwxeditor::cwx::perf::TraceScope trace_path_d143(__FILE__, __LINE__);
    if (!owner) {
        throw std::invalid_argument("CWX path owner is null");
    }
    return cpjoin(owner->cwxPath(id), category, index);
}

lib::String cpjoin(const lib::Owner<CWXPath>& owner, std::string_view name, bool id) {
    ::cwxeditor::cwx::perf::TraceScope trace_path_d147(__FILE__, __LINE__);
    if (!owner) {
        throw std::invalid_argument("CWX path owner is null");
    }
    return joinWithSlash(owner->cwxPath(id), name);
}

lib::String cpjoinid(const lib::Owner<CWXPath>& owner, std::uint64_t id) {
    ::cwxeditor::cwx::perf::TraceScope trace_path_d152(__FILE__, __LINE__);
    return cpjoinid(owner, {}, id);
}

lib::String cpjoinid(const lib::Owner<CWXPath>& owner,
                     std::string_view category,
                     std::uint64_t id) {
    ::cwxeditor::cwx::perf::TraceScope trace_path_d156(__FILE__, __LINE__);
    if (!owner) {
        throw std::invalid_argument("CWX path owner is null");
    }
    return cpjoinid(owner->cwxPath(true), category, id);
}

lib::String cpjoin(std::string_view ownerPath,
                   std::string_view category,
                   std::size_t index) {
    ::cwxeditor::cwx::perf::TraceScope trace_path_d160(__FILE__, __LINE__);
    return joinWithSlash(ownerPath, categoryIndex(category, index));
}

lib::String cpjoin2(std::string_view ownerPath,
                    std::string_view category,
                    std::size_t index) {
    ::cwxeditor::cwx::perf::TraceScope trace_path_d165(__FILE__, __LINE__);
    return cpjoin(ownerPath, category, index);
}

lib::String cpjoinid(std::string_view ownerPath,
                     std::string_view category,
                     std::uint64_t id) {
    ::cwxeditor::cwx::perf::TraceScope trace_path_d170(__FILE__, __LINE__);
    return joinWithSlash(ownerPath, categoryId(category, id));
}

lib::String cpjoin(std::string_view ownerPath, std::string_view subPath) {
    ::cwxeditor::cwx::perf::TraceScope trace_path_d175(__FILE__, __LINE__);
    return joinWithSlash(ownerPath, subPath);
}

lib::String cpjoin2(std::string_view ownerPath, std::string_view subPath) {
    // 言語差: 結合結果は所有する文字列値。D版の空ownerでの領域共有は再現しない。
    // パスの内容は同じにし、呼出し元の可変バッファの寿命には依存させない。
    ::cwxeditor::cwx::perf::TraceScope trace_path_d179(__FILE__, __LINE__);
    return cpjoin(ownerPath, subPath);
}

lib::Array<lib::String> cpattr(std::string_view path) {
    ::cwxeditor::cwx::perf::TraceScope trace_path_d184(__FILE__, __LINE__);
    lib::Array<lib::String> result;
    while (true) {
        ::cwxeditor::cwx::perf::TraceScope trace_path_d186(__FILE__, __LINE__);
        const auto separator = path.rfind(';');
        if (separator == std::string_view::npos) break;
        result.emplace_back(path.substr(separator + 1));
        path = path.substr(0, separator);
    }
    return result;
}

lib::String cpattrRef(lib::String& path) {
    ::cwxeditor::cwx::perf::TraceScope trace_path_d203(__FILE__, __LINE__);
    const auto separator = path.find(';');
    if (separator == std::string_view::npos) {
        ::cwxeditor::cwx::perf::TraceScope trace_path_d205(__FILE__, __LINE__);
        return lib::String{};
    }

    lib::String attributes = path.substr(separator);
    path.erase(separator);
    return attributes;
}

lib::String cpbody(std::string_view path) {
    ::cwxeditor::cwx::perf::TraceScope trace_path_d222(__FILE__, __LINE__);
    const auto separator = path.find(';');
    if (separator != std::string_view::npos) {
        ::cwxeditor::cwx::perf::TraceScope trace_path_d224(__FILE__, __LINE__);
        return lib::String(path.substr(0, separator));
    }
    return lib::String(path);
}

lib::String cpaddattr(std::string_view path, std::string_view attr) {
    ::cwxeditor::cwx::perf::TraceScope trace_path_d236(__FILE__, __LINE__);
    lib::String result;
    result.reserve(path.size() + 1 + attr.size());
    result.append(path);
    result.push_back(';');
    result.append(attr);
    return result;
}

bool cphasattr(std::string_view path, std::string_view attr) {
    ::cwxeditor::cwx::perf::TraceScope trace_path_d241(__FILE__, __LINE__);
    const auto attrs = cpattr(path);
    return std::find(attrs.begin(), attrs.end(), attr) != attrs.end();
}

bool cpeq(std::string_view path1, std::string_view path2) {
    ::cwxeditor::cwx::perf::TraceScope trace_path_d246(__FILE__, __LINE__);
    return cpbody(path1) == cpbody(path2);
}

bool cpempty(std::string_view path) {
    ::cwxeditor::cwx::perf::TraceScope trace_path_d251(__FILE__, __LINE__);
    if (path.empty()) return true;
    return path.find(';') == 0;
}

lib::String cpparent(std::string_view path) {
    ::cwxeditor::cwx::perf::TraceScope trace_path_d264(__FILE__, __LINE__);
    lib::String body(path);
    const auto attributes = cpattrRef(body);
    const auto separator = body.rfind('/');
    const auto parent = separator == std::string_view::npos
                            ? std::string_view{}
                            : std::string_view(body).substr(0, separator);
    lib::String result(parent);
    result.append(attributes);
    return result;
}

lib::String cptop(std::string_view path) {
    ::cwxeditor::cwx::perf::TraceScope trace_path_d274(__FILE__, __LINE__);
    lib::String body(path);
    const auto attributes = cpattrRef(body);
    const auto separator = body.find('/');
    const auto top = separator == std::string_view::npos
                         ? std::string_view(body)
                         : std::string_view(body).substr(0, separator);
    lib::String result(top);
    result.append(attributes);
    return result;
}

lib::String cpbottom(std::string_view path) {
    ::cwxeditor::cwx::perf::TraceScope trace_path_d283(__FILE__, __LINE__);
    lib::String body(path);
    const auto attributes = cpattrRef(body);
    const auto separator = body.find('/');
    const auto bottom = separator == std::string_view::npos
                            ? std::string_view{}
                            : std::string_view(body).substr(separator + 1);
    lib::String result(bottom);
    result.append(attributes);
    return result;
}

lib::String cpcategory(std::string_view path) {
    ::cwxeditor::cwx::perf::TraceScope trace_path_d292(__FILE__, __LINE__);
    const auto top = cpbody(cptop(path));
    const auto separator = top.rfind(':');
    return separator == std::string_view::npos
               ? top
               : lib::String(top.substr(0, separator));
}

std::size_t cpindex(std::string_view path) {
    ::cwxeditor::cwx::perf::TraceScope trace_path_d301(__FILE__, __LINE__);
    const auto top = cpbody(cptop(path));
    const auto separator = top.rfind(':');
    return separator == std::string_view::npos
               ? 0
               : parseSize(std::string_view(top).substr(separator + 1));
}

lib::String cplast(std::string_view path) {
    ::cwxeditor::cwx::perf::TraceScope trace_path_d310(__FILE__, __LINE__);
    lib::String body(path);
    const auto attributes = cpattrRef(body);
    const auto separator = body.rfind('/');
    const auto last = separator == std::string_view::npos
                          ? std::string_view(body)
                          : std::string_view(body).substr(separator + 1);
    lib::String result(last);
    result.append(attributes);
    return result;
}

bool cpdescendant(std::string_view path1, std::string_view path2) {
    ::cwxeditor::cwx::perf::TraceScope trace_path_d320(__FILE__, __LINE__);
    const auto ancestor = cpbody(path1);
    const auto candidate = cpbody(path2);
    return candidate.starts_with(ancestor);
}

lib::String cprel(std::string_view path, std::string_view ancestor) {
    ::cwxeditor::cwx::perf::TraceScope trace_path_d326(__FILE__, __LINE__);
    if (path.starts_with(ancestor) && ancestor.size() + 1 < path.size()) {
        ::cwxeditor::cwx::perf::TraceScope trace_path_d327(__FILE__, __LINE__);
        return lib::String(path.substr(ancestor.size() + 1));
    }
    return lib::String{};
}

lib::Owner<CWXPath> cwxTop(lib::Owner<CWXPath> cwxPath) {
    ::cwxeditor::cwx::perf::TraceScope trace_path_d335(__FILE__, __LINE__);
    while (cwxPath) {
        ::cwxeditor::cwx::perf::TraceScope trace_path_d336(__FILE__, __LINE__);
        auto parent = cwxPath->cwxParent().lock();
        if (!parent) return cwxPath;
        cwxPath = std::move(parent);
    }
    return lib::Owner<CWXPath>{};
}

}  // namespace cwxeditor::cwx::path
