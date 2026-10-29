#pragma once

#include <chrono>
#include <cstdint>
#include <ctime>
#include <stdexcept>

namespace cw::lib {

// Py の datetime は OS 時計の瞬間値ではなく、タイムゾーンなし
// のグレゴリオ暦。0001-01-01 からのマイクロ秒で 1～9999 年
// の全範囲を保持し、ローカル日時の減算を夏時間から独立させる。OS 固有処理
// は現在時刻の変換だけに使い、保存された暦日時を mktime に通さない。
struct DateTime {
    std::chrono::microseconds value{};

    DateTime() = default;
    explicit DateTime(std::chrono::system_clock::time_point instant) {
        const auto ticks = std::chrono::duration_cast<std::chrono::microseconds>(instant.time_since_epoch()).count();
        auto seconds = ticks / 1'000'000;
        auto fraction = ticks % 1'000'000;
        if (fraction < 0) { --seconds; fraction += 1'000'000; }
        const auto raw = static_cast<std::time_t>(seconds);
        std::tm local{};
#ifdef _WIN32
        if (localtime_s(&local, &raw) != 0)
#else
        if (!localtime_r(&raw, &local))
#endif
            throw std::runtime_error("local datetime conversion failed");
        *this = from_calendar(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                              local.tm_hour, local.tm_min, local.tm_sec, fraction);
    }

    static DateTime now() { return DateTime(std::chrono::system_clock::now()); }

    static DateTime from_calendar(std::int64_t year, std::int64_t month, std::int64_t day,
                                  std::int64_t hour = 0, std::int64_t minute = 0,
                                  std::int64_t second = 0, std::int64_t microsecond = 0) {
        if (year < 1 || year > 9999 || month < 1 || month > 12 ||
            day < 1 || day > days_in_month(year, month) || hour < 0 || hour > 23 ||
            minute < 0 || minute > 59 || second < 0 || second > 59 ||
            microsecond < 0 || microsecond > 999999)
            throw std::invalid_argument("invalid Gregorian datetime");
        auto days = days_before_year(year);
        for (std::int64_t m = 1; m < month; ++m) days += days_in_month(year, m);
        days += day - 1;
        DateTime result;
        result.value = std::chrono::microseconds(
            ((days * 24 + hour) * 60 * 60 + minute * 60 + second) * 1'000'000 + microsecond);
        return result;
    }

    std::tm calendar() const {
        const auto ticks = value.count();
        if (ticks < 0 || ticks >= days_before_year(10000) * 86400 * 1'000'000)
            throw std::out_of_range("datetime outside Gregorian range");
        auto days = ticks / (86400 * std::int64_t{1'000'000});
        const auto day_seconds = (ticks / 1'000'000) % 86400;
        std::int64_t lo = 1, hi = 10000;
        while (lo + 1 < hi) {
            const auto mid = (lo + hi) / 2;
            if (days_before_year(mid) <= days) lo = mid; else hi = mid;
        }
        std::tm result{};
        result.tm_year = static_cast<int>(lo - 1900);
        result.tm_wday = static_cast<int>((days + 1) % 7);
        days -= days_before_year(lo);
        result.tm_yday = static_cast<int>(days);
        std::int64_t month = 1;
        while (days >= days_in_month(lo, month)) days -= days_in_month(lo, month++);
        result.tm_mon = static_cast<int>(month - 1);
        result.tm_mday = static_cast<int>(days + 1);
        result.tm_hour = static_cast<int>(day_seconds / 3600);
        result.tm_min = static_cast<int>((day_seconds / 60) % 60);
        result.tm_sec = static_cast<int>(day_seconds % 60);
        result.tm_isdst = -1;
        return result;
    }

    double seconds_since(const DateTime& earlier) const {
        return std::chrono::duration<double>(value - earlier.value).count();
    }

private:
    static std::int64_t days_before_year(std::int64_t year) {
        const auto prior = year - 1;
        return prior * 365 + prior / 4 - prior / 100 + prior / 400;
    }
    static int days_in_month(std::int64_t year, std::int64_t month) {
        static constexpr int days[]{31,28,31,30,31,30,31,31,30,31,30,31};
        return days[month - 1] + (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    }
};

} // namespace cw::lib
