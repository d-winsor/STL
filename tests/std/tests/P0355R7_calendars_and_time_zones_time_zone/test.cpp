// Copyright (c) Microsoft Corporation.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <cassert>
#include <chrono>

using namespace std;
using namespace std::chrono;

//
// These test suites will assume all data from the IANA database is correct
// and will not test historical changes in transitions. Instead the focus
// on using a select sample of transitions in both a positive and negative
// UTC offset zone.
//
// Sydney
// Daylight time (AEST) UTC + 10 -> First Sun April @ 2am
// Standard time (AEDT) UTC + -9 -> First Sun Oct   @ 2am
//
// UTC transitions intersecting 2020
// T1 : AEDT [Oct 6 2019 @2am - Apr 5 2020 @2am)
// T2 : AEST [Apr 5 2020 @3am - Oct 4 2020 @2am)
// T3 : AEDT [Oct 4 2020 @1am - Apr 4 2021 @2am)
//
static constexpr std::string_view Sydney_tz_name{"Australia/Sydney"sv};
static constexpr seconds Sydney_standard_offset{hours{10}};
static constexpr auto Sydney_daylight_2019 =
    sys_seconds{sys_days{year_month_weekday{year{2019}, October, weekday_indexed{Sunday, 1}}}} + hours{2}
    - Sydney_standard_offset;
static constexpr auto Sydney_standard_2020 =
    sys_seconds{sys_days{year_month_weekday{year{2020}, April, weekday_indexed{Sunday, 1}}}} + hours{2}
    - Sydney_standard_offset;
static constexpr auto Sydney_daylight_2020 =
    sys_seconds{sys_days{year_month_weekday{year{2020}, October, weekday_indexed{Sunday, 1}}}} + hours{2}
    - Sydney_standard_offset;

//
// Vancouver
// Standard time (AEST) UTC -8  -> // TODO: ...
// Daylight time (AEDT) UTC -7  -> // TODO: ...
//
// UTC transitions intersecting 2020
// T1 : PDT [??? 2019 - ??? 2020)
// T2 : PST [??? 2020 - ??? 2020)
// T3 : PDT [??? 2020 - ??? 2021)
//
// TODO: ...

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

void timezone_sys_info_test() {
    const auto& tzdb = get_tzdb();
    auto utc_zone    = tzdb.locate_zone("UTC");

    assert(utc_zone != nullptr);

    auto current_time = system_clock::now();
    auto now_info     = utc_zone->get_info(current_time);
    assert(now_info.begin != sys_seconds{});
    assert(now_info.end != sys_seconds{});
    assert(now_info.offset == seconds{0});
    // TODO: abbrev

    auto sydney_tz = tzdb.locate_zone(Sydney_tz_name);
    assert(sydney_tz != nullptr);

    auto info1 = sydney_tz->get_info(Sydney_daylight_2019);
    assert(info1.begin == Sydney_daylight_2019);
    assert(info1.end == Sydney_standard_2020);
    assert(info1.offset == Sydney_standard_offset - hours{1});
    assert(info1.save != minutes{0});
    // TODO: abbrev

    auto info2 = sydney_tz->get_info(Sydney_daylight_2019 + days{3});
    assert(info2.begin == info1.begin);
    assert(info2.end == info1.end);
    assert(info2.offset == info1.offset);
    assert(info2.save == info1.save);
    assert(info2.abbrev == info1.abbrev);

    auto info3 = sydney_tz->get_info(Sydney_standard_2020);
    assert(info3.begin == Sydney_standard_2020);
    assert(info3.end == Sydney_daylight_2020);
    assert(info3.offset == Sydney_standard_offset);
    assert(info3.save == minutes{0});
    // TODO: abbrev

    // TODO: test min & max transitions...
}

bool test() {
    timezone_names_test();
    timezone_sys_info_test();
    return true;
}

int main() {
    test();
}
