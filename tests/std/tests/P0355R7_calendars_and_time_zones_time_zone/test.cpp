// Copyright (c) Microsoft Corporation.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <cassert>
#include <chrono>
#include <iostream>

using namespace std;
using namespace std::chrono;

static constexpr sys_seconds min_date{sys_days{year::min() / January / day{1}}};
static constexpr sys_seconds max_date{sys_days{year::max() / December / day{31}}};

//
// These test suites will assume all data from the IANA database is correct
// and will not test historical changes in transitions. Instead the focus
// on using a select sample of transitions in both a positive and negative
// UTC offset zone.
//
// Sydney
// Standard time (AEST : UTC+10) -1 @ 3am
// Daylight time (AEDT : UTC+11) +1 @ 2am
//
static constexpr std::string_view Sydney_tz_name{"Australia/Sydney"sv};
static constexpr seconds Sydney_standard_offset{hours{10}};
static constexpr seconds Sydney_daylight_offset{hours{11}};
static constexpr auto Sydney_daylight_2019 =
    sys_seconds{sys_days{year{2019} / October / day{6}}} + hours{2} - Sydney_standard_offset;
static constexpr auto Sydney_standard_2020 =
    sys_seconds{sys_days{year{2020} / April / day{5}}} + hours{3} - Sydney_daylight_offset;
static constexpr auto Sydney_daylight_2020 =
    sys_seconds{sys_days{year{2020} / October / day{4}}} + hours{2} - Sydney_standard_offset;
//
// Los Angeles
// Standard time (PST : UTC-8) +1 @ 2am
// Daylight time (PDT : UTC-7) -1 @ 2am
//

static constexpr std::string_view LA_tz_name{"America/Los_Angeles"sv};
static constexpr seconds LA_standard_offset{hours{-8}};
static constexpr seconds LA_daylight_offset{hours{-7}};
static constexpr auto LA_daylight_2020 =
    sys_seconds{sys_days{year{2020} / March / day{8}}} + hours{2} - LA_standard_offset;
static constexpr auto LA_standard_2020 =
    sys_seconds{sys_days{year{2020} / November / day{1}}} + hours{2} - LA_daylight_offset;
static constexpr auto LA_daylight_2021 =
    sys_seconds{sys_days{year{2021} / March / day{14}}} + hours{2} - LA_standard_offset;


bool operator==(const sys_info& _Left, const sys_info& _Right) {
    return _Left.begin == _Right.begin && _Left.end == _Right.end && _Left.offset == _Right.offset
        && _Left.save == _Right.save && _Left.abbrev == _Right.abbrev;
}

void timezone_tzdb_test() {
    const auto& tzbd_list = get_tzdb_list();
    const auto& tzdb      = get_tzdb();
    assert(&tzbd_list.front() == &tzdb);

    const auto& reloaded = reload_tzdb();
    assert(&tzbd_list.front() == &reloaded);
    assert(&tzdb == &reloaded);

    // TODO: finish tzdb_list impl and add more tests.

    // Test basic functionality
    assert(tzdb.locate_zone("UTC") != nullptr);
    assert(reloaded.locate_zone("UTC") != nullptr);
}

void timezone_names_test() {
    const auto& tzdb = get_tzdb();

    auto utc_zone = tzdb.locate_zone("UTC");
    assert(utc_zone != nullptr);
    assert(utc_zone->name() == "UTC");

    auto utc_zone2 = tzdb.locate_zone(utc_zone->name());
    assert(utc_zone2 != nullptr);
    assert(utc_zone2->name() == "UTC");
    assert(utc_zone2 == utc_zone);

    auto current_zone = tzdb.current_zone();
    assert(current_zone != nullptr);
    assert(current_zone->name().empty() == false);

    auto non_existent = tzdb.locate_zone("Non/Existent");
    assert(non_existent == nullptr);
}

void validate_timezone_transitions(const time_zone* tz, sys_seconds daylight1, sys_seconds standard1,
    sys_seconds daylight2, seconds st_offset, seconds dt_offset, std::string_view st_abbrev,
    std::string_view dt_abbrev) {

    auto info1 = tz->get_info(daylight1);
    assert(info1.begin == daylight1);
    assert(info1.end == standard1);
    assert(info1.offset == dt_offset);
    assert(info1.save != minutes{0});
    assert(info1.abbrev == dt_abbrev);

    auto info2 = tz->get_info(daylight1 + days{3});
    assert(info2 == info1);

    auto info3 = tz->get_info(standard1);
    assert(info3.begin == standard1);
    assert(info3.end == daylight2);
    assert(info3.offset == st_offset);
    assert(info3.save == minutes{0});
    assert(info3.abbrev == st_abbrev);

    // Ensure min/max transition return valid results.
    auto min_info = tz->get_info(min_date);
    auto max_info = tz->get_info(max_date);
    assert(min_info.begin <= min_date);
    assert(max_info.end >= max_date);
}


void timezone_sys_info_test() {
    const auto& tzdb = get_tzdb();
    auto utc_zone    = tzdb.locate_zone("UTC");

    assert(utc_zone != nullptr);

    auto current_time = system_clock::now();
    auto now_info     = utc_zone->get_info(current_time);
    assert(now_info.begin != sys_seconds{});
    assert(now_info.end != sys_seconds{});
    assert(now_info.offset == seconds{0});
    assert(now_info.abbrev == "UTC");

    auto min_utc = utc_zone->get_info(min_date);
    auto max_utc = utc_zone->get_info(max_date);
    // Only a single transition in UTC
    assert(min_utc == max_utc);
    assert(min_utc.begin != sys_seconds{});
    assert(min_utc.end != sys_seconds{});
    // FIXME: data loss in double -> long long
    // assert(min_utc.begin < max_utc.end);

    auto sydney_tz = tzdb.locate_zone(Sydney_tz_name);
    assert(sydney_tz != nullptr);
    validate_timezone_transitions(sydney_tz, Sydney_daylight_2019, Sydney_standard_2020, Sydney_daylight_2020,
        Sydney_standard_offset, Sydney_daylight_offset, "GMT+10", "GMT+11"); // FIXME: IANA database == "AEST/AEDT"

    auto la_tz = tzdb.locate_zone(LA_tz_name);
    assert(la_tz != nullptr);
    validate_timezone_transitions(la_tz, LA_daylight_2020, LA_standard_2020, LA_daylight_2021, LA_standard_offset,
        LA_daylight_offset, "PST", "PDT");

    // Test abbrevations other than standard/daylight savings such as war time.
    // These senarios are not handled correctly by icu.dll
    auto war_time = la_tz->get_info(sys_days{year{1942} / April / day{1}});
    assert(war_time.abbrev == "PDT"); // IANA datbase == "PWT"
}

// get sys_info on either side of an exact time_zone transition given in UTC sys_time
std::pair<sys_info, sys_info> get_transition_pair(const time_zone* tz, sys_seconds transition) {
    std::pair<sys_info, sys_info> result;
    result.first  = tz->get_info(transition - minutes{1});
    result.second = tz->get_info(transition);
    assert(result.first.end == result.second.begin);
    assert(result.first.offset != result.second.offset);

    return result;
}

void assert_local(std::string_view msg, const time_zone* tz, local_seconds time, int result, const sys_info& first,
    const sys_info& second) {
    const auto info = tz->get_info(time);
    if (info.result != result || info.first != first || info.second != second) {
        cout << msg << "\n";
    }

    assert(info.result == result);
    assert(info.first == first);
    assert(info.second == second);
}

void validate_get_local_info(const time_zone* tz, sys_seconds transition, int result) {
    const auto infos = get_transition_pair(tz, transition);
    // Get the local time for the beginning of the ambiguous/nonexistent section
    const auto local = local_seconds{transition.time_since_epoch() + std::min(infos.first.offset, infos.second.offset)};

    // clang-format off
    assert_local("two_days_before", tz, local - days{2}, local_info::unique, infos.first, sys_info{});
    assert_local("one_hour_before", tz, local - hours{1}, local_info::unique, infos.first, sys_info{});
    assert_local("begin", tz,           local, result, infos.first, infos.second);
    assert_local("mid", tz,             local + minutes{30}, result, infos.first, infos.second);
    assert_local("after", tz,           local + hours{1}, local_info::unique, infos.second, sys_info{});
    assert_local("one_hour_after", tz,  local + hours{2}, local_info::unique, infos.second, sys_info{});
    assert_local("two_days_after", tz,  local + days{2}, local_info::unique, infos.second, sys_info{});
    // clang-format on
}

void timezone_local_info_test() {
    const auto& tzdb = get_tzdb();

    // positive offset can end up in previous transition
    auto sydney_tz = tzdb.locate_zone(Sydney_tz_name);
    assert(sydney_tz != nullptr);
    // AEDT to AEST
    validate_get_local_info(sydney_tz, Sydney_standard_2020, local_info::ambiguous);
    // AEST to AEDT
    validate_get_local_info(sydney_tz, Sydney_daylight_2020, local_info::nonexistent);

    // negative offset can end up in next transition
    auto la_tz = tzdb.locate_zone(LA_tz_name);
    assert(la_tz != nullptr);
    // PDT to PST
    validate_get_local_info(la_tz, LA_standard_2020, local_info::ambiguous);
    // PST to PDT
    validate_get_local_info(la_tz, LA_daylight_2021, local_info::nonexistent);
}

bool test() {
    timezone_tzdb_test();
    timezone_sys_info_test();
    timezone_local_info_test();
    return true;
}

int main() {
    test();
}
