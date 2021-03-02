// Copyright (c) Microsoft Corporation.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <cassert>
#include <chrono>

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

bool operator==(const sys_info& _Left, const sys_info& _Right) {
    return _Left.begin == _Right.begin && _Left.end == _Right.end && _Left.offset == _Right.offset
        && _Left.save == _Right.save && _Left.abbrev == _Right.abbrev;
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

    auto sydney_tz = tzdb.locate_zone(Sydney_tz_name);
    assert(sydney_tz != nullptr);

    auto info1 = sydney_tz->get_info(Sydney_daylight_2019);
    assert(info1.begin == Sydney_daylight_2019);
    assert(info1.end == Sydney_standard_2020);
    assert(info1.offset == Sydney_standard_offset - hours{1});
    assert(info1.save != minutes{0});
    assert(info1.abbrev == "GMT+11"); // IANA datbase == "AEDT"

    auto info2 = sydney_tz->get_info(Sydney_daylight_2019 + days{3});
    assert(info2 == info1);

    auto info3 = sydney_tz->get_info(Sydney_standard_2020);
    assert(info3.begin == Sydney_standard_2020);
    assert(info3.end == Sydney_daylight_2020);
    assert(info3.offset == Sydney_standard_offset);
    assert(info3.save == minutes{0});
    assert(info3.abbrev == "GMT+10"); // IANA datbase == "AEST"

    auto min_utc = utc_zone->get_info(min_date);
    auto max_utc = utc_zone->get_info(max_date);
    // Only a single transition in UTC
    assert(min_utc == max_utc);
    assert(min_utc.begin != sys_seconds{});
    assert(min_utc.end != sys_seconds{});
    // FIXME: data loss in double -> long long
    // assert(min_utc.begin < max_utc.end);

    // Min/max of transition should match with single UTC transition
    auto min_syd = sydney_tz->get_info(min_date);
    auto max_syd = sydney_tz->get_info(max_date);
    assert(min_syd.begin <= min_utc.begin);
    // FIXME: data loss in double -> long long
    // assert(max_syd.end >= max_utc.end);

    // Test abbrevations other than standard/daylight savings such as war time.
    // These senarios are not handled correctly by icu.dll
    auto ny_tz    = tzdb.locate_zone("America/New_York");
    auto war_time = ny_tz->get_info(sys_days{year{1942} / April / day{1}});
    assert(war_time.abbrev == "EDT"); // IANA datbase == "EWT"
}

bool test() {
    timezone_names_test();
    timezone_sys_info_test();
    return true;
}

int main() {
    test();
}
