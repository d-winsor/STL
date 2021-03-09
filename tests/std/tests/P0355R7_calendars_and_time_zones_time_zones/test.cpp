// Copyright (c) Microsoft Corporation.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <algorithm>
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
// Standard time (AEST : UTC+10) -1 @ 3am
// Daylight time (AEDT : UTC+11) +1 @ 2am
//
namespace Sydney {
    static constexpr std::string_view Tz_name{"Australia/Sydney"sv};
    static constexpr seconds Standard_offset{hours{10}};
    static constexpr seconds Daylight_offset{hours{11}};
    static constexpr auto Daylight_2019_day = year{2019} / October / day{6};
    static constexpr auto Standard_2020_day = year{2020} / April / day{5};
    static constexpr auto Daylight_2020_day = year{2020} / October / day{4};
    static constexpr auto Daylight_2019     = sys_seconds{sys_days{Daylight_2019_day}} + hours{2} - Standard_offset;
    static constexpr auto Standard_2020     = sys_seconds{sys_days{Standard_2020_day}} + hours{3} - Daylight_offset;
    static constexpr auto Daylight_2020     = sys_seconds{sys_days{Daylight_2020_day}} + hours{2} - Standard_offset;
} // namespace Sydney

//
// Los Angeles
// Standard time (PST : UTC-8) +1 @ 2am
// Daylight time (PDT : UTC-7) -1 @ 2am
//
namespace LA {
    static constexpr std::string_view Tz_name{"America/Los_Angeles"sv};
    static constexpr seconds Standard_offset{hours{-8}};
    static constexpr seconds Daylight_offset{hours{-7}};
    static constexpr auto Daylight_2020_day = year{2020} / March / day{8};
    static constexpr auto Standard_2020_day = year{2020} / November / day{1};
    static constexpr auto daylight_2021_day = year{2021} / March / day{14};
    static constexpr auto Daylight_2020     = sys_seconds{sys_days{Daylight_2020_day}} + hours{2} - Standard_offset;
    static constexpr auto Standard_2020     = sys_seconds{sys_days{Standard_2020_day}} + hours{2} - Daylight_offset;
    static constexpr auto daylight_2021     = sys_seconds{sys_days{daylight_2021_day}} + hours{2} - Standard_offset;
} // namespace LA

bool operator==(const sys_info& _Left, const sys_info& _Right) {
    return _Left.begin == _Right.begin && _Left.end == _Right.end && _Left.offset == _Right.offset
        && _Left.save == _Right.save && _Left.abbrev == _Right.abbrev;
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

// get sys_info on either side of an exact time_zone transition given in UTC sys_time
std::pair<sys_info, sys_info> get_transition_pair(const time_zone* tz, sys_seconds transition) {
    std::pair<sys_info, sys_info> result;
    result.first  = tz->get_info(transition - minutes{1});
    result.second = tz->get_info(transition);
    assert(result.first.end == result.second.begin);
    assert(result.first.offset != result.second.offset);

    return result;
}

void assert_local(const time_zone* tz, local_seconds time, int result, const sys_info& first, const sys_info& second) {
    const auto info = tz->get_info(time);
    assert(info.result == result);
    assert(info.first == first);
    assert(info.second == second);

    // time_zone::to_sys depends heavily on local_info so just test it here
    // to exhaust all corner cases.
    sys_seconds sys_earliest{time.time_since_epoch() - info.first.offset};
    sys_seconds sys_latest{time.time_since_epoch() - info.second.offset};
    try {
        assert(tz->to_sys(time) == sys_earliest);
        assert(result == local_info::unique);
    } catch (nonexistent_local_time ex) {
        assert(result == local_info::nonexistent);
    } catch (ambiguous_local_time ex) {
        assert(result == local_info::ambiguous);
    }

    if (result == local_info::unique) {
        assert(tz->to_sys(time, choose::earliest) == sys_earliest);
        assert(tz->to_sys(time, choose::latest) == sys_earliest);
    } else if (result == local_info::nonexistent) {
        assert(tz->to_sys(time, choose::earliest) == info.first.end);
        assert(tz->to_sys(time, choose::latest) == info.first.end);
    } else if (result == local_info::ambiguous) {
        assert(tz->to_sys(time, choose::earliest) == sys_earliest);
        assert(tz->to_sys(time, choose::latest) == sys_latest);
    }
}

void validate_get_local_info(const time_zone* tz, sys_seconds transition, int result) {
    const auto infos = get_transition_pair(tz, transition);
    // Get the local time for the beginning of the ambiguous/nonexistent section
    const auto local = local_seconds{transition.time_since_epoch() + std::min(infos.first.offset, infos.second.offset)};

    assert_local(tz, local - days{2}, local_info::unique, infos.first, sys_info{}); // two days before
    assert_local(tz, local - hours{1}, local_info::unique, infos.first, sys_info{}); // one hour before
    assert_local(tz, local, result, infos.first, infos.second); // transition begin
    assert_local(tz, local + minutes{30}, result, infos.first, infos.second); // transition mid
    assert_local(tz, local + hours{1}, local_info::unique, infos.second, sys_info{}); // transition end
    assert_local(tz, local + hours{2}, local_info::unique, infos.second, sys_info{}); // one hour after
    assert_local(tz, local + days{2}, local_info::unique, infos.second, sys_info{}); // two days after
}

void test_time_zone_and_link(const tzdb& tzdb, string_view tz_name, string_view tz_link_name) {
    const auto orginal_tz = tzdb.locate_zone(tz_name);
    assert(orginal_tz != nullptr);
    assert(orginal_tz->name() == tz_name);

    const auto linked_tz = tzdb.locate_zone(tz_link_name);
    assert(linked_tz != nullptr);
    assert(linked_tz->name() == tz_name);
    assert(orginal_tz == linked_tz);

    const auto tz_link = _Locate_zone_impl(tzdb.links, tz_link_name);
    assert(tz_link != nullptr);
    assert(tz_link->name() == tz_link_name);
    assert(tz_link->target() == tz_name);
    assert(tzdb.locate_zone(tz_link->target()) == orginal_tz);

    assert(_Locate_zone_impl(tzdb.time_zones, tz_name) != nullptr);
    assert(_Locate_zone_impl(tzdb.time_zones, tz_link_name) == nullptr);
    assert(_Locate_zone_impl(tzdb.links, tz_name) == nullptr);
}

void timezone_names_test() {
    const auto& tzdb = get_tzdb();

    test_time_zone_and_link(tzdb, "Africa/Maputo", "Africa/Lusaka");
    test_time_zone_and_link(tzdb, "Pacific/Auckland", "Antarctica/McMurdo");

    const auto current_zone = tzdb.current_zone();
    assert(current_zone != nullptr);
    assert(current_zone->name().empty() == false);

    assert(tzdb.locate_zone("Non/Existent") == nullptr);

    // Abbreviations should not be time_zones or time_zone_links
    assert(tzdb.locate_zone("PST") == nullptr);
    assert(tzdb.locate_zone("AEST") == nullptr);

    // Comparison operators
    const time_zone tz1{"Earlier"};
    const time_zone tz2{"Earlier"};
    const time_zone tz3{"Later"};
    assert(tz1 == tz2);
    assert(tz1 != tz3);
#ifdef __cpp_lib_concepts
    assert(tz1 <=> tz2 == strong_ordering::equal);
    assert(tz1 <=> tz3 == strong_ordering::less);
    assert(tz3 <=> tz1 == strong_ordering::greater);
#endif // __cpp_lib_concepts

    const time_zone_link link1{"Earlier", "Target"};
    const time_zone_link link2{"Earlier", "Is"};
    const time_zone_link link3{"Later", "Ignored"};
    assert(link1 == link2);
    assert(link1 != link3);
#ifdef __cpp_lib_concepts
    assert(link1 <=> link2 == strong_ordering::equal);
    assert(link1 <=> link3 == strong_ordering::less);
    assert(link3 <=> link1 == strong_ordering::greater);
#endif // __cpp_lib_concepts
}

// FIXME: This is an illustration of the gaps/difference between database and ICU timezones.
//        I don't think this would be the best thing to actually test as it is changes.
//          Tz_status::Time_zone =>         IANA time_zone
//          Tz_status::Time_zone_link =>    IANA time_zone_link
//          Tz_status::Canonical =>         ICU time_zone, these include "some" IANA links and are
//                                          treated as regular zones for API calls.
//          Tz_status::Any =>               You can request all timezones. In the ICU non-canonical
//                                          timezones are links to canoncial timezones but annoyingly:
//                                           1) some match IANA link other do not
//                                           2) some of these links match actual IANA timezones
//                                           3) they have lots of aliases unrelated to anything in the IANA
enum Tz_status { Time_zone, Time_zone_link, Absent, Canonical, Any };

void validate_time_zone(string_view name, Tz_status db_status, Tz_status icu_status) {
    (void) db_status; // Used to illustrate expected value in IANA

    const auto& tzdb = get_tzdb();
    switch (icu_status) {
    case Tz_status::Time_zone:
    case Tz_status::Canonical: // using USystemTimeZoneType::UCAL_ZONE_TYPE_CANONICAL
        assert(_Locate_zone_impl(tzdb.time_zones, name) != nullptr);
        assert(_Locate_zone_impl(tzdb.links, name) == nullptr);
        break;
    case Tz_status::Time_zone_link:
        assert(_Locate_zone_impl(tzdb.time_zones, name) == nullptr);
        assert(_Locate_zone_impl(tzdb.links, name) != nullptr);
        break;
    case Tz_status::Absent:
        assert(_Locate_zone_impl(tzdb.time_zones, name) == nullptr);
        assert(_Locate_zone_impl(tzdb.links, name) == nullptr);
        break;
    case Tz_status::Any: // using USystemTimeZoneType::UCAL_ZONE_TYPE_ANY
    default:
        break;
    }
}

void all_timezone_names() {
    // List generated from a script using IANA database (version 2021a) and ICU (Win 10.0.19042 Build 19042)
    // clang-format off
    //                 Name                               IANA status                 ICU status
    validate_time_zone("ACT",                             Tz_status::Absent,          Tz_status::Any);
    validate_time_zone("AET",                             Tz_status::Absent,          Tz_status::Any);
    validate_time_zone("AGT",                             Tz_status::Absent,          Tz_status::Any);
    validate_time_zone("ART",                             Tz_status::Absent,          Tz_status::Any);
    validate_time_zone("AST",                             Tz_status::Absent,          Tz_status::Any);
    validate_time_zone("Africa/Abidjan",                  Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Africa/Accra",                    Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Africa/Addis_Ababa",              Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Africa/Algiers",                  Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Africa/Asmara",                   Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Africa/Asmera",                   Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Africa/Bamako",                   Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Africa/Bangui",                   Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Africa/Banjul",                   Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Africa/Bissau",                   Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Africa/Blantyre",                 Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Africa/Brazzaville",              Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Africa/Bujumbura",                Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Africa/Cairo",                    Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Africa/Casablanca",               Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Africa/Ceuta",                    Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Africa/Conakry",                  Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Africa/Dakar",                    Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Africa/Dar_es_Salaam",            Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Africa/Djibouti",                 Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Africa/Douala",                   Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Africa/El_Aaiun",                 Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Africa/Freetown",                 Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Africa/Gaborone",                 Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Africa/Harare",                   Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Africa/Johannesburg",             Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Africa/Juba",                     Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Africa/Kampala",                  Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Africa/Khartoum",                 Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Africa/Kigali",                   Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Africa/Kinshasa",                 Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Africa/Lagos",                    Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Africa/Libreville",               Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Africa/Lome",                     Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Africa/Luanda",                   Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Africa/Lubumbashi",               Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Africa/Lusaka",                   Tz_status::Time_zone_link,  Tz_status::Time_zone_link); // Tz_status::Canonical. Changed for testing
    validate_time_zone("Africa/Malabo",                   Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Africa/Maputo",                   Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Africa/Maseru",                   Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Africa/Mbabane",                  Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Africa/Mogadishu",                Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Africa/Monrovia",                 Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Africa/Nairobi",                  Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Africa/Ndjamena",                 Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Africa/Niamey",                   Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Africa/Nouakchott",               Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Africa/Ouagadougou",              Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Africa/Porto-Novo",               Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Africa/Sao_Tome",                 Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Africa/Timbuktu",                 Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Africa/Tripoli",                  Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Africa/Tunis",                    Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Africa/Windhoek",                 Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Adak",                    Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Anchorage",               Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Anguilla",                Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("America/Antigua",                 Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("America/Araguaina",               Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Argentina/Buenos_Aires",  Tz_status::Time_zone,       Tz_status::Any);
    validate_time_zone("America/Argentina/Catamarca",     Tz_status::Time_zone,       Tz_status::Any);
    validate_time_zone("America/Argentina/ComodRivadavia",Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("America/Argentina/Cordoba",       Tz_status::Time_zone,       Tz_status::Any);
    validate_time_zone("America/Argentina/Jujuy",         Tz_status::Time_zone,       Tz_status::Any);
    validate_time_zone("America/Argentina/La_Rioja",      Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Argentina/Mendoza",       Tz_status::Time_zone,       Tz_status::Any);
    validate_time_zone("America/Argentina/Rio_Gallegos",  Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Argentina/Salta",         Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Argentina/San_Juan",      Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Argentina/San_Luis",      Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Argentina/Tucuman",       Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Argentina/Ushuaia",       Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Aruba",                   Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("America/Asuncion",                Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Atikokan",                Tz_status::Time_zone,       Tz_status::Any);
    validate_time_zone("America/Atka",                    Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("America/Bahia",                   Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Bahia_Banderas",          Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Barbados",                Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Belem",                   Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Belize",                  Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Blanc-Sablon",            Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Boa_Vista",               Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Bogota",                  Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Boise",                   Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Buenos_Aires",            Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("America/Cambridge_Bay",           Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Campo_Grande",            Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Cancun",                  Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Caracas",                 Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Catamarca",               Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("America/Cayenne",                 Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Cayman",                  Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("America/Chicago",                 Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Chihuahua",               Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Coral_Harbour",           Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("America/Cordoba",                 Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("America/Costa_Rica",              Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Creston",                 Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Cuiaba",                  Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Curacao",                 Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Danmarkshavn",            Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Dawson",                  Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Dawson_Creek",            Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Denver",                  Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Detroit",                 Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Dominica",                Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("America/Edmonton",                Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Eirunepe",                Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/El_Salvador",             Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Ensenada",                Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("America/Fort_Nelson",             Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Fort_Wayne",              Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("America/Fortaleza",               Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Glace_Bay",               Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Godthab",                 Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("America/Goose_Bay",               Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Grand_Turk",              Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Grenada",                 Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("America/Guadeloupe",              Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("America/Guatemala",               Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Guayaquil",               Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Guyana",                  Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Halifax",                 Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Havana",                  Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Hermosillo",              Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Indiana/Indianapolis",    Tz_status::Time_zone,       Tz_status::Any);
    validate_time_zone("America/Indiana/Knox",            Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Indiana/Marengo",         Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Indiana/Petersburg",      Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Indiana/Tell_City",       Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Indiana/Vevay",           Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Indiana/Vincennes",       Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Indiana/Winamac",         Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Indianapolis",            Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("America/Inuvik",                  Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Iqaluit",                 Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Jamaica",                 Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Jujuy",                   Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("America/Juneau",                  Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Kentucky/Louisville",     Tz_status::Time_zone,       Tz_status::Any);
    validate_time_zone("America/Kentucky/Monticello",     Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Knox_IN",                 Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("America/Kralendijk",              Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("America/La_Paz",                  Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Lima",                    Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Los_Angeles",             Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Louisville",              Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("America/Lower_Princes",           Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("America/Maceio",                  Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Managua",                 Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Manaus",                  Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Marigot",                 Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("America/Martinique",              Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Matamoros",               Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Mazatlan",                Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Mendoza",                 Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("America/Menominee",               Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Merida",                  Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Metlakatla",              Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Mexico_City",             Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Miquelon",                Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Moncton",                 Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Monterrey",               Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Montevideo",              Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Montreal",                Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("America/Montserrat",              Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("America/Nassau",                  Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/New_York",                Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Nipigon",                 Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Nome",                    Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Noronha",                 Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/North_Dakota/Beulah",     Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/North_Dakota/Center",     Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/North_Dakota/New_Salem",  Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Nuuk",                    Tz_status::Time_zone,       Tz_status::Absent);
    validate_time_zone("America/Ojinaga",                 Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Panama",                  Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Pangnirtung",             Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Paramaribo",              Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Phoenix",                 Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Port-au-Prince",          Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Port_of_Spain",           Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Porto_Acre",              Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("America/Porto_Velho",             Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Puerto_Rico",             Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Punta_Arenas",            Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Rainy_River",             Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Rankin_Inlet",            Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Recife",                  Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Regina",                  Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Resolute",                Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Rio_Branco",              Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Rosario",                 Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("America/Santa_Isabel",            Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("America/Santarem",                Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Santiago",                Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Santo_Domingo",           Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Sao_Paulo",               Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Scoresbysund",            Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Shiprock",                Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("America/Sitka",                   Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/St_Barthelemy",           Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("America/St_Johns",                Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/St_Kitts",                Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("America/St_Lucia",                Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("America/St_Thomas",               Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("America/St_Vincent",              Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("America/Swift_Current",           Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Tegucigalpa",             Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Thule",                   Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Thunder_Bay",             Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Tijuana",                 Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Toronto",                 Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Tortola",                 Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("America/Vancouver",               Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Virgin",                  Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("America/Whitehorse",              Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Winnipeg",                Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Yakutat",                 Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("America/Yellowknife",             Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Antarctica/Casey",                Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Antarctica/Davis",                Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Antarctica/DumontDUrville",       Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Antarctica/Macquarie",            Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Antarctica/Mawson",               Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Antarctica/McMurdo",              Tz_status::Time_zone_link,  Tz_status::Time_zone_link);; // Tz_status::Canonical. Changed for testing
    validate_time_zone("Antarctica/Palmer",               Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Antarctica/Rothera",              Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Antarctica/South_Pole",           Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Antarctica/Syowa",                Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Antarctica/Troll",                Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Antarctica/Vostok",               Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Arctic/Longyearbyen",             Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Asia/Aden",                       Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Asia/Almaty",                     Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Amman",                      Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Anadyr",                     Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Aqtau",                      Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Aqtobe",                     Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Ashgabat",                   Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Ashkhabad",                  Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Asia/Atyrau",                     Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Baghdad",                    Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Bahrain",                    Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Asia/Baku",                       Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Bangkok",                    Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Barnaul",                    Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Beirut",                     Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Bishkek",                    Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Brunei",                     Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Calcutta",                   Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Asia/Chita",                      Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Choibalsan",                 Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Chongqing",                  Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Asia/Chungking",                  Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Asia/Colombo",                    Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Dacca",                      Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Asia/Damascus",                   Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Dhaka",                      Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Dili",                       Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Dubai",                      Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Dushanbe",                   Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Famagusta",                  Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Gaza",                       Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Harbin",                     Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Asia/Hebron",                     Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Ho_Chi_Minh",                Tz_status::Time_zone,       Tz_status::Any);
    validate_time_zone("Asia/Hong_Kong",                  Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Hovd",                       Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Irkutsk",                    Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Istanbul",                   Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Asia/Jakarta",                    Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Jayapura",                   Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Jerusalem",                  Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Kabul",                      Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Kamchatka",                  Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Karachi",                    Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Kashgar",                    Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Asia/Kathmandu",                  Tz_status::Time_zone,       Tz_status::Any);
    validate_time_zone("Asia/Katmandu",                   Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Asia/Khandyga",                   Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Kolkata",                    Tz_status::Time_zone,       Tz_status::Any);
    validate_time_zone("Asia/Krasnoyarsk",                Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Kuala_Lumpur",               Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Kuching",                    Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Kuwait",                     Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Asia/Macao",                      Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Asia/Macau",                      Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Magadan",                    Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Makassar",                   Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Manila",                     Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Muscat",                     Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Asia/Nicosia",                    Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Novokuznetsk",               Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Novosibirsk",                Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Omsk",                       Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Oral",                       Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Phnom_Penh",                 Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Asia/Pontianak",                  Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Pyongyang",                  Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Qatar",                      Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Qostanay",                   Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Qyzylorda",                  Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Rangoon",                    Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Asia/Riyadh",                     Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Saigon",                     Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Asia/Sakhalin",                   Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Samarkand",                  Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Seoul",                      Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Shanghai",                   Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Singapore",                  Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Srednekolymsk",              Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Taipei",                     Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Tashkent",                   Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Tbilisi",                    Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Tehran",                     Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Tel_Aviv",                   Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Asia/Thimbu",                     Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Asia/Thimphu",                    Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Tokyo",                      Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Tomsk",                      Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Ujung_Pandang",              Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Asia/Ulaanbaatar",                Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Ulan_Bator",                 Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Asia/Urumqi",                     Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Ust-Nera",                   Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Vientiane",                  Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Asia/Vladivostok",                Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Yakutsk",                    Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Yangon",                     Tz_status::Time_zone,       Tz_status::Any);
    validate_time_zone("Asia/Yekaterinburg",              Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Asia/Yerevan",                    Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Atlantic/Azores",                 Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Atlantic/Bermuda",                Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Atlantic/Canary",                 Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Atlantic/Cape_Verde",             Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Atlantic/Faeroe",                 Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Atlantic/Faroe",                  Tz_status::Time_zone,       Tz_status::Any);
    validate_time_zone("Atlantic/Jan_Mayen",              Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Atlantic/Madeira",                Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Atlantic/Reykjavik",              Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Atlantic/South_Georgia",          Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Atlantic/St_Helena",              Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Atlantic/Stanley",                Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Australia/ACT",                   Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Australia/Adelaide",              Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Australia/Brisbane",              Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Australia/Broken_Hill",           Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Australia/Canberra",              Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Australia/Currie",                Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Australia/Darwin",                Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Australia/Eucla",                 Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Australia/Hobart",                Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Australia/LHI",                   Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Australia/Lindeman",              Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Australia/Lord_Howe",             Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Australia/Melbourne",             Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Australia/NSW",                   Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Australia/North",                 Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Australia/Perth",                 Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Australia/Queensland",            Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Australia/South",                 Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Australia/Sydney",                Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Australia/Tasmania",              Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Australia/Victoria",              Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Australia/West",                  Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Australia/Yancowinna",            Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("BET",                             Tz_status::Absent,          Tz_status::Any);
    validate_time_zone("BST",                             Tz_status::Absent,          Tz_status::Any);
    validate_time_zone("Brazil/Acre",                     Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Brazil/DeNoronha",                Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Brazil/East",                     Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Brazil/West",                     Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("CAT",                             Tz_status::Absent,          Tz_status::Any);
    validate_time_zone("CET",                             Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("CNT",                             Tz_status::Absent,          Tz_status::Any);
    validate_time_zone("CST",                             Tz_status::Absent,          Tz_status::Any);
    validate_time_zone("CST6CDT",                         Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("CTT",                             Tz_status::Absent,          Tz_status::Any);
    validate_time_zone("Canada/Atlantic",                 Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Canada/Central",                  Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Canada/East-Saskatchewan",        Tz_status::Absent,          Tz_status::Any);
    validate_time_zone("Canada/Eastern",                  Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Canada/Mountain",                 Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Canada/Newfoundland",             Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Canada/Pacific",                  Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Canada/Saskatchewan",             Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Canada/Yukon",                    Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Chile/Continental",               Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Chile/EasterIsland",              Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Cuba",                            Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("EAT",                             Tz_status::Absent,          Tz_status::Any);
    validate_time_zone("ECT",                             Tz_status::Absent,          Tz_status::Any);
    validate_time_zone("EET",                             Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("EST",                             Tz_status::Time_zone,       Tz_status::Any);
    validate_time_zone("EST5EDT",                         Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Egypt",                           Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Eire",                            Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Etc/GMT",                         Tz_status::Absent,          Tz_status::Canonical);
    validate_time_zone("Etc/GMT+0",                       Tz_status::Absent,          Tz_status::Any);
    validate_time_zone("Etc/GMT+1",                       Tz_status::Absent,          Tz_status::Canonical);
    validate_time_zone("Etc/GMT+10",                      Tz_status::Absent,          Tz_status::Canonical);
    validate_time_zone("Etc/GMT+11",                      Tz_status::Absent,          Tz_status::Canonical);
    validate_time_zone("Etc/GMT+12",                      Tz_status::Absent,          Tz_status::Canonical);
    validate_time_zone("Etc/GMT+2",                       Tz_status::Absent,          Tz_status::Canonical);
    validate_time_zone("Etc/GMT+3",                       Tz_status::Absent,          Tz_status::Canonical);
    validate_time_zone("Etc/GMT+4",                       Tz_status::Absent,          Tz_status::Canonical);
    validate_time_zone("Etc/GMT+5",                       Tz_status::Absent,          Tz_status::Canonical);
    validate_time_zone("Etc/GMT+6",                       Tz_status::Absent,          Tz_status::Canonical);
    validate_time_zone("Etc/GMT+7",                       Tz_status::Absent,          Tz_status::Canonical);
    validate_time_zone("Etc/GMT+8",                       Tz_status::Absent,          Tz_status::Canonical);
    validate_time_zone("Etc/GMT+9",                       Tz_status::Absent,          Tz_status::Canonical);
    validate_time_zone("Etc/GMT-0",                       Tz_status::Absent,          Tz_status::Any);
    validate_time_zone("Etc/GMT-1",                       Tz_status::Absent,          Tz_status::Canonical);
    validate_time_zone("Etc/GMT-10",                      Tz_status::Absent,          Tz_status::Canonical);
    validate_time_zone("Etc/GMT-11",                      Tz_status::Absent,          Tz_status::Canonical);
    validate_time_zone("Etc/GMT-12",                      Tz_status::Absent,          Tz_status::Canonical);
    validate_time_zone("Etc/GMT-13",                      Tz_status::Absent,          Tz_status::Canonical);
    validate_time_zone("Etc/GMT-14",                      Tz_status::Absent,          Tz_status::Canonical);
    validate_time_zone("Etc/GMT-2",                       Tz_status::Absent,          Tz_status::Canonical);
    validate_time_zone("Etc/GMT-3",                       Tz_status::Absent,          Tz_status::Canonical);
    validate_time_zone("Etc/GMT-4",                       Tz_status::Absent,          Tz_status::Canonical);
    validate_time_zone("Etc/GMT-5",                       Tz_status::Absent,          Tz_status::Canonical);
    validate_time_zone("Etc/GMT-6",                       Tz_status::Absent,          Tz_status::Canonical);
    validate_time_zone("Etc/GMT-7",                       Tz_status::Absent,          Tz_status::Canonical);
    validate_time_zone("Etc/GMT-8",                       Tz_status::Absent,          Tz_status::Canonical);
    validate_time_zone("Etc/GMT-9",                       Tz_status::Absent,          Tz_status::Canonical);
    validate_time_zone("Etc/GMT0",                        Tz_status::Absent,          Tz_status::Any);
    validate_time_zone("Etc/Greenwich",                   Tz_status::Absent,          Tz_status::Any);
    validate_time_zone("Etc/UCT",                         Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Etc/UTC",                         Tz_status::Absent,          Tz_status::Canonical);
    validate_time_zone("Etc/Universal",                   Tz_status::Absent,          Tz_status::Any);
    validate_time_zone("Etc/Zulu",                        Tz_status::Absent,          Tz_status::Any);
    validate_time_zone("Europe/Amsterdam",                Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Europe/Andorra",                  Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Europe/Astrakhan",                Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Europe/Athens",                   Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Europe/Belfast",                  Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Europe/Belgrade",                 Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Europe/Berlin",                   Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Europe/Bratislava",               Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Europe/Brussels",                 Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Europe/Bucharest",                Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Europe/Budapest",                 Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Europe/Busingen",                 Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Europe/Chisinau",                 Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Europe/Copenhagen",               Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Europe/Dublin",                   Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Europe/Gibraltar",                Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Europe/Guernsey",                 Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Europe/Helsinki",                 Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Europe/Isle_of_Man",              Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Europe/Istanbul",                 Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Europe/Jersey",                   Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Europe/Kaliningrad",              Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Europe/Kiev",                     Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Europe/Kirov",                    Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Europe/Lisbon",                   Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Europe/Ljubljana",                Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Europe/London",                   Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Europe/Luxembourg",               Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Europe/Madrid",                   Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Europe/Malta",                    Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Europe/Mariehamn",                Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Europe/Minsk",                    Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Europe/Monaco",                   Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Europe/Moscow",                   Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Europe/Nicosia",                  Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Europe/Oslo",                     Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Europe/Paris",                    Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Europe/Podgorica",                Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Europe/Prague",                   Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Europe/Riga",                     Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Europe/Rome",                     Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Europe/Samara",                   Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Europe/San_Marino",               Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Europe/Sarajevo",                 Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Europe/Saratov",                  Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Europe/Simferopol",               Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Europe/Skopje",                   Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Europe/Sofia",                    Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Europe/Stockholm",                Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Europe/Tallinn",                  Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Europe/Tirane",                   Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Europe/Tiraspol",                 Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Europe/Ulyanovsk",                Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Europe/Uzhgorod",                 Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Europe/Vaduz",                    Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Europe/Vatican",                  Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Europe/Vienna",                   Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Europe/Vilnius",                  Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Europe/Volgograd",                Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Europe/Warsaw",                   Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Europe/Zagreb",                   Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Europe/Zaporozhye",               Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Europe/Zurich",                   Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Factory",                         Tz_status::Absent,          Tz_status::Canonical);
    validate_time_zone("GB",                              Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("GB-Eire",                         Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("GMT",                             Tz_status::Absent,          Tz_status::Any);
    validate_time_zone("GMT+0",                           Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("GMT-0",                           Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("GMT0",                            Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Greenwich",                       Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("HST",                             Tz_status::Time_zone,       Tz_status::Any);
    validate_time_zone("Hongkong",                        Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("IET",                             Tz_status::Absent,          Tz_status::Any);
    validate_time_zone("IST",                             Tz_status::Absent,          Tz_status::Any);
    validate_time_zone("Iceland",                         Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Indian/Antananarivo",             Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Indian/Chagos",                   Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Indian/Christmas",                Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Indian/Cocos",                    Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Indian/Comoro",                   Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Indian/Kerguelen",                Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Indian/Mahe",                     Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Indian/Maldives",                 Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Indian/Mauritius",                Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Indian/Mayotte",                  Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Indian/Reunion",                  Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Iran",                            Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Israel",                          Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("JST",                             Tz_status::Absent,          Tz_status::Any);
    validate_time_zone("Jamaica",                         Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Japan",                           Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Kwajalein",                       Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Libya",                           Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("MET",                             Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("MIT",                             Tz_status::Absent,          Tz_status::Any);
    validate_time_zone("MST",                             Tz_status::Time_zone,       Tz_status::Any);
    validate_time_zone("MST7MDT",                         Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Mexico/BajaNorte",                Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Mexico/BajaSur",                  Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Mexico/General",                  Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("NET",                             Tz_status::Absent,          Tz_status::Any);
    validate_time_zone("NST",                             Tz_status::Absent,          Tz_status::Any);
    validate_time_zone("NZ",                              Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("NZ-CHAT",                         Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Navajo",                          Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("PLT",                             Tz_status::Absent,          Tz_status::Any);
    validate_time_zone("PNT",                             Tz_status::Absent,          Tz_status::Any);
    validate_time_zone("PRC",                             Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("PRT",                             Tz_status::Absent,          Tz_status::Any);
    validate_time_zone("PST",                             Tz_status::Absent,          Tz_status::Any);
    validate_time_zone("PST8PDT",                         Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Pacific/Apia",                    Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Pacific/Auckland",                Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Pacific/Bougainville",            Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Pacific/Chatham",                 Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Pacific/Chuuk",                   Tz_status::Time_zone,       Tz_status::Any);
    validate_time_zone("Pacific/Easter",                  Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Pacific/Efate",                   Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Pacific/Enderbury",               Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Pacific/Fakaofo",                 Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Pacific/Fiji",                    Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Pacific/Funafuti",                Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Pacific/Galapagos",               Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Pacific/Gambier",                 Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Pacific/Guadalcanal",             Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Pacific/Guam",                    Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Pacific/Honolulu",                Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Pacific/Johnston",                Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Pacific/Kiritimati",              Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Pacific/Kosrae",                  Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Pacific/Kwajalein",               Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Pacific/Majuro",                  Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Pacific/Marquesas",               Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Pacific/Midway",                  Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Pacific/Nauru",                   Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Pacific/Niue",                    Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Pacific/Norfolk",                 Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Pacific/Noumea",                  Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Pacific/Pago_Pago",               Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Pacific/Palau",                   Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Pacific/Pitcairn",                Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Pacific/Pohnpei",                 Tz_status::Time_zone,       Tz_status::Any);
    validate_time_zone("Pacific/Ponape",                  Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Pacific/Port_Moresby",            Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Pacific/Rarotonga",               Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Pacific/Saipan",                  Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Pacific/Samoa",                   Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Pacific/Tahiti",                  Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Pacific/Tarawa",                  Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Pacific/Tongatapu",               Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Pacific/Truk",                    Tz_status::Time_zone_link,  Tz_status::Canonical);
    validate_time_zone("Pacific/Wake",                    Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Pacific/Wallis",                  Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Pacific/Yap",                     Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Poland",                          Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Portugal",                        Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("ROC",                             Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("ROK",                             Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("SST",                             Tz_status::Absent,          Tz_status::Any);
    validate_time_zone("Singapore",                       Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("SystemV/AST4",                    Tz_status::Absent,          Tz_status::Canonical);
    validate_time_zone("SystemV/AST4ADT",                 Tz_status::Absent,          Tz_status::Canonical);
    validate_time_zone("SystemV/CST6",                    Tz_status::Absent,          Tz_status::Canonical);
    validate_time_zone("SystemV/CST6CDT",                 Tz_status::Absent,          Tz_status::Canonical);
    validate_time_zone("SystemV/EST5",                    Tz_status::Absent,          Tz_status::Canonical);
    validate_time_zone("SystemV/EST5EDT",                 Tz_status::Absent,          Tz_status::Canonical);
    validate_time_zone("SystemV/HST10",                   Tz_status::Absent,          Tz_status::Canonical);
    validate_time_zone("SystemV/MST7",                    Tz_status::Absent,          Tz_status::Canonical);
    validate_time_zone("SystemV/MST7MDT",                 Tz_status::Absent,          Tz_status::Canonical);
    validate_time_zone("SystemV/PST8",                    Tz_status::Absent,          Tz_status::Canonical);
    validate_time_zone("SystemV/PST8PDT",                 Tz_status::Absent,          Tz_status::Canonical);
    validate_time_zone("SystemV/YST9",                    Tz_status::Absent,          Tz_status::Canonical);
    validate_time_zone("SystemV/YST9YDT",                 Tz_status::Absent,          Tz_status::Canonical);
    validate_time_zone("Turkey",                          Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("UCT",                             Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("US/Alaska",                       Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("US/Aleutian",                     Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("US/Arizona",                      Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("US/Central",                      Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("US/East-Indiana",                 Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("US/Eastern",                      Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("US/Hawaii",                       Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("US/Indiana-Starke",               Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("US/Michigan",                     Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("US/Mountain",                     Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("US/Pacific",                      Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("US/Pacific-New",                  Tz_status::Absent,          Tz_status::Any);
    validate_time_zone("US/Samoa",                        Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("UTC",                             Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("Universal",                       Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("VST",                             Tz_status::Absent,          Tz_status::Any);
    validate_time_zone("W-SU",                            Tz_status::Time_zone_link,  Tz_status::Any);
    validate_time_zone("WET",                             Tz_status::Time_zone,       Tz_status::Canonical);
    validate_time_zone("Zulu",                            Tz_status::Time_zone_link,  Tz_status::Any);
    // clang-format on
}

void timezone_sys_info_test() {
    const auto& tzdb = get_tzdb();
    {
        auto utc_zone = tzdb.locate_zone("Etc/UTC");
        assert(utc_zone != nullptr);
        auto min_utc = utc_zone->get_info(min_date);
        auto max_utc = utc_zone->get_info(max_date);
        // Only a single transition in UTC
        assert(min_utc == max_utc);
        assert(min_utc.begin != sys_seconds{});
        assert(min_utc.end != sys_seconds{});
        // FIXME: data loss in double -> long long
        // assert(min_utc.begin < max_utc.end);
    }
    {
        using namespace Sydney;
        auto tz = tzdb.locate_zone(Tz_name);
        assert(tz != nullptr);
        validate_timezone_transitions(tz, Daylight_2019, Standard_2020, Daylight_2020, Standard_offset, Daylight_offset,
            "GMT+10", "GMT+11"); // FIXME: IANA database == "AEST/AEDT"
    }
    {
        using namespace LA;
        auto tz = tzdb.locate_zone(Tz_name);
        assert(tz != nullptr);
        validate_timezone_transitions(
            tz, Daylight_2020, Standard_2020, daylight_2021, Standard_offset, Daylight_offset, "PST", "PDT");

        // Test abbrevations other than standard/daylight savings such as war time.
        // These senarios are not handled correctly by icu.dll
        auto war_time = tz->get_info(sys_days{year{1942} / April / day{1}});
        assert(war_time.abbrev == "PDT"); // IANA datbase == "PWT"
    }
}

void timezone_to_local_test() {
    const auto& tzdb = get_tzdb();

    auto sydney_tz = tzdb.locate_zone(Sydney::Tz_name);
    assert(sydney_tz != nullptr);
    {
        using namespace Sydney;
        local_seconds midnight{local_days{Daylight_2019_day}}; // +1 @ 2am
        assert(sydney_tz->to_local(Daylight_2019 - hours{1}) == midnight + hours{1});
        assert(sydney_tz->to_local(Daylight_2019 + hours{0}) == midnight + hours{3});
        assert(sydney_tz->to_local(Daylight_2019 + hours{1}) == midnight + hours{4});
    }
    {
        using namespace Sydney;
        local_seconds midnight{local_days{Standard_2020_day}}; // -1 @ 3am
        assert(sydney_tz->to_local(Standard_2020 - hours{1}) == midnight + hours{2});
        assert(sydney_tz->to_local(Standard_2020 + hours{0}) == midnight + hours{2});
        assert(sydney_tz->to_local(Standard_2020 + hours{1}) == midnight + hours{3});
    }

    auto la_tz = tzdb.locate_zone(LA::Tz_name);
    assert(la_tz != nullptr);
    {
        using namespace LA;
        local_seconds midnight{local_days{Daylight_2020_day}}; // +1 @ 2am
        assert(la_tz->to_local(Daylight_2020 - hours{1}) == midnight + hours{1});
        assert(la_tz->to_local(Daylight_2020 + hours{0}) == midnight + hours{3});
        assert(la_tz->to_local(Daylight_2020 + hours{1}) == midnight + hours{4});
    }
    {
        using namespace LA;
        local_seconds midnight{local_days{Standard_2020_day}}; // -1 @ 2am
        assert(la_tz->to_local(Standard_2020 - hours{1}) == midnight + hours{1});
        assert(la_tz->to_local(Standard_2020 + hours{0}) == midnight + hours{1});
        assert(la_tz->to_local(Standard_2020 + hours{1}) == midnight + hours{2});
    }
}

void timezone_local_info_test() {
    const auto& tzdb = get_tzdb();
    {
        // positive offset (UTC+10/+11) can fall in previous transition
        using namespace Sydney;
        auto tz = tzdb.locate_zone(Tz_name);
        assert(tz != nullptr);
        validate_get_local_info(tz, Standard_2020, local_info::ambiguous); // AEDT to AEST
        validate_get_local_info(tz, Daylight_2020, local_info::nonexistent); // AEST to AEDT
    }
    {
        // negative offset (UTC-8/-7) can fall in next transition
        using namespace LA;
        auto tz = tzdb.locate_zone(Tz_name);
        assert(tz != nullptr);
        validate_get_local_info(tz, Standard_2020, local_info::ambiguous); // PDT to PST
        validate_get_local_info(tz, daylight_2021, local_info::nonexistent); // PST to PDT
    }
}

bool test() {
    timezone_names_test();
    all_timezone_names();
    timezone_sys_info_test();
    timezone_to_local_test();
    timezone_local_info_test();

    return true;
}

int main() {
    test();
}
