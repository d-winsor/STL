// Copyright (c) Microsoft Corporation.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <chrono>
#include <string>
#include <vector>
#include <xtzdb.h>

#define NOMINMAX
#include <icu.h>
#include <internal_shared.h>

#include <Windows.h>

#pragma comment(lib, "Advapi32")

namespace {
    namespace _Icu {
        template <typename T>
        T load_fn(HMODULE module, LPCSTR name) {
            auto address = GetProcAddress(module, name);
            return reinterpret_cast<T>(address);
        }

#define DECLARE_FN(name) decltype(name)* name
        DECLARE_FN(ucal_close);
        DECLARE_FN(ucal_get);
        DECLARE_FN(ucal_getDefaultTimeZone);
        DECLARE_FN(ucal_getTimeZoneDisplayName);
        DECLARE_FN(ucal_getTimeZoneIDForWindowsID);
        DECLARE_FN(ucal_getTimeZoneTransitionDate);
        DECLARE_FN(ucal_inDaylightTime);
        DECLARE_FN(ucal_open);
        DECLARE_FN(ucal_openTimeZones);
        DECLARE_FN(ucal_setMillis);
        DECLARE_FN(uenum_close);
        DECLARE_FN(uenum_unext);

        void load_module() {
            HMODULE _Icu_module = LoadLibraryExW(L"icu.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (_Icu_module == nullptr) {
                __debugbreak();
            }

#define STORE_FN(name) name = load_fn<decltype(name)>(_Icu_module, #name)
            STORE_FN(ucal_close);
            STORE_FN(ucal_get);
            STORE_FN(ucal_getDefaultTimeZone);
            STORE_FN(ucal_getTimeZoneDisplayName);
            STORE_FN(ucal_getTimeZoneIDForWindowsID);
            STORE_FN(ucal_getTimeZoneTransitionDate);
            STORE_FN(ucal_inDaylightTime);
            STORE_FN(ucal_open);
            STORE_FN(ucal_openTimeZones);
            STORE_FN(ucal_setMillis);
            STORE_FN(uenum_close);
            STORE_FN(uenum_unext);
        }
    } // namespace _Icu

    // TODO: Use locale-specific-narrow-encoding not just UTF8
    std::u16string _Covert_UT8_to_UTF16(std::string_view _UT8_Str) {
        int _Count = MultiByteToWideChar(CP_UTF8, 0, _UT8_Str.data(), _UT8_Str.length(), nullptr, 0);
        std::u16string _U16_str(_Count, 0);
        MultiByteToWideChar(CP_UTF8, 0, _UT8_Str.data(), _UT8_Str.length(), (wchar_t*) _U16_str.data(), _Count);
        return _U16_str;
    }

    // TODO: Use locale-specific-narrow-encoding not just UTF8
    const char* _Allocate_UT8_from_UTF16(std::u16string_view _U16_str) {
        int _Count = WideCharToMultiByte(
            CP_UTF8, 0, (wchar_t*) _U16_str.data(), _U16_str.length(), nullptr, 0, nullptr, nullptr);

        auto _UT8_str = new char[_Count + 1];
        WideCharToMultiByte(
            CP_UTF8, 0, (wchar_t*) _U16_str.data(), _U16_str.length(), _UT8_str, _Count, nullptr, nullptr);
        _UT8_str[_Count] = '\0';
        return _UT8_str;
    }

    const UErrorCode _Get_sys_time(UCalendar* _Cal, _Sys_time_info* _Info) {
        UErrorCode ec{};
        if (_Icu::ucal_inDaylightTime(_Cal, &ec)) {
            if (U_SUCCESS(ec)) {
                _Info->save = _Icu::ucal_get(_Cal, UCalendarDateFields::UCAL_DST_OFFSET, &ec);
            }

            if (U_SUCCESS(ec)) {
                _Info->offset = _Icu::ucal_get(_Cal, UCalendarDateFields::UCAL_ZONE_OFFSET, &ec) + _Info->save;
            }
        } else if (U_SUCCESS(ec)) {
            _Info->offset = _Icu::ucal_get(_Cal, UCalendarDateFields::UCAL_ZONE_OFFSET, &ec);
            _Info->save   = 0;
        }

        if (U_SUCCESS(ec)) {
            UDate _Begin{};
            _Info->begin = _Icu::ucal_getTimeZoneTransitionDate(
                               _Cal, UTimeZoneTransitionType::UCAL_TZ_TRANSITION_PREVIOUS_INCLUSIVE, &_Begin, &ec)
                             ? _Begin
                             : U_DATE_MIN;
        }

        if (U_SUCCESS(ec)) {
            UDate _End{};
            _Info->end =
                _Icu::ucal_getTimeZoneTransitionDate(_Cal, UTimeZoneTransitionType::UCAL_TZ_TRANSITION_NEXT, &_End, &ec)
                    ? _End
                    : U_DATE_MAX;
        }

        auto _Display_type =
            _Info->save == 0 ? UCalendarDisplayNameType::UCAL_SHORT_STANDARD : UCalendarDisplayNameType::UCAL_SHORT_DST;
        char16_t _Name_buf[256];
        auto _Name_buf_len =
            _Icu::ucal_getTimeZoneDisplayName(_Cal, _Display_type, nullptr, _Name_buf, sizeof(_Name_buf), &ec);
        if (U_SUCCESS(ec)) {
            _Info->abbrev = _Allocate_UT8_from_UTF16({_Name_buf, static_cast<size_t>(_Name_buf_len)});
        }

        return ec;
    }

} // namespace

_EXTERN_C

_RegistryLeapSecondInfo* __stdcall __std_tzdb_get_reg_leap_seconds(
    const size_t prev_reg_ls_size, size_t* current_reg_ls_size) {
    // On exit---
    //    *current_reg_ls_size <= prev_reg_ls_size, *reg_ls_data == nullptr --> no new data
    //    *current_reg_ls_size >  prev_reg_ls_size, *reg_ls_data != nullptr --> new data, successfully read
    //    *current_reg_ls_size == 0,                *reg_ls_data != nullptr --> new data, failed reading
    //    *current_reg_ls_size >  prev_reg_ls_size, *reg_ls_data == nullptr --> new data, failed allocation

    constexpr auto reg_key_name    = TEXT("SYSTEM\\CurrentControlSet\\Control\\LeapSecondInformation");
    constexpr auto reg_subkey_name = TEXT("LeapSeconds");
    *current_reg_ls_size           = 0;
    HKEY leap_sec_key              = 0;

    LSTATUS status = RegOpenKeyEx(HKEY_LOCAL_MACHINE, reg_key_name, 0, KEY_READ, &leap_sec_key);
    if (status != ERROR_SUCCESS) {
        // May not exist on older systems. Treat this as equivalent to the key existing but with no data.
        return nullptr;
    }

    DWORD byte_size = 0;
    status          = RegQueryValueEx(leap_sec_key, reg_subkey_name, nullptr, nullptr, nullptr, &byte_size);
    static_assert(sizeof(_RegistryLeapSecondInfo) == 12);
    const auto ls_size   = byte_size / 12;
    *current_reg_ls_size = ls_size;

    _RegistryLeapSecondInfo* reg_ls_data = nullptr;
    if ((status == ERROR_SUCCESS || status == ERROR_MORE_DATA) && ls_size > prev_reg_ls_size) {
        try {
            reg_ls_data = new _RegistryLeapSecondInfo[ls_size];
            status      = RegQueryValueEx(
                leap_sec_key, reg_subkey_name, nullptr, nullptr, reinterpret_cast<LPBYTE>(reg_ls_data), &byte_size);
            if (status != ERROR_SUCCESS) {
                *current_reg_ls_size = 0;
            }
        } catch (...) {
        }
    }

    RegCloseKey(leap_sec_key);
    if (status != ERROR_SUCCESS) {
        SetLastError(status);
    }

    return reg_ls_data;
}

void __stdcall __std_decalloc_reg_leap_seconds(_RegistryLeapSecondInfo* _Rlsi) {
    delete[] _Rlsi;
}

_NODISCARD _Tzdb_init_info* __stdcall __std_tzdb_init() {
    // Dynamically load icu.dll and required function ptrs
    _Icu::load_module();
    UErrorCode ec{};
    UEnumeration* _Enumeration = _Icu::ucal_openTimeZones(&ec);
    if (!_Enumeration) {
        return nullptr;
    }

    int32_t _Elem_len{};
    std::vector<std::u16string> _TimeZone_ids;
    while (auto* _Elem = _Icu::uenum_unext(_Enumeration, &_Elem_len, &ec)) {
        if (U_FAILURE(ec)) {
            _Icu::uenum_close(_Enumeration);
            return nullptr;
        }

        _TimeZone_ids.push_back({_Elem, static_cast<size_t>(_Elem_len)});
    }

    _Icu::uenum_close(_Enumeration);

    auto _Info             = new _Tzdb_init_info();
    _Info->_Num_time_zones = _TimeZone_ids.size();
    _Info->_Names          = new const char*[_Info->_Num_time_zones];
    for (size_t i = 0; i < _Info->_Num_time_zones; i++) {
        _Info->_Names[i] = _Allocate_UT8_from_UTF16(_TimeZone_ids[i]);
    }

    return _Info;
}

void __stdcall __std_tzdb_dealloc_init_info(_Tzdb_init_info* _Info) {
    delete[] _Info->_Names;
    delete _Info;
}

_NODISCARD const char* __stdcall __std_tzdb_get_current_zone() {
    UErrorCode ec{};
    char16_t _Id_buf[256];
    auto _Id_buf_len = _Icu::ucal_getDefaultTimeZone(_Id_buf, sizeof(_Id_buf), &ec);
    if (U_FAILURE(ec) && _Id_buf_len != 0) {
        return nullptr;
    }

    return _Allocate_UT8_from_UTF16({_Id_buf, static_cast<size_t>(_Id_buf_len)});
}

void __stdcall __std_tzdb_dealloc_current_zone(const char* _Tz) {
    delete[] _Tz;
}

_NODISCARD _Sys_time_info* __stdcall __std_tzbd_get_sys_info(const char* _Tz, size_t _Tz_len, _SysTimeRep _Sys) {
    UErrorCode ec{};
    auto _Tz_name = _Covert_UT8_to_UTF16({_Tz, _Tz_len});
    auto _Cal     = _Icu::ucal_open(_Tz_name.c_str(), _Tz_name.length(), nullptr, UCalendarType::UCAL_DEFAULT, &ec);

    if (U_SUCCESS(ec)) {
        _Icu::ucal_setMillis(_Cal, _Sys, &ec);
    }

    auto _Info = new _Sys_time_info;
    if (U_SUCCESS(ec)) {
        ec = _Get_sys_time(_Cal, _Info);
    }

    _Icu::ucal_close(_Cal);
    if (U_FAILURE(ec)) {
        __std_tzdb_dealloc_sys_info(_Info);
        _Info = nullptr;
    }

    return _Info;
}

void __stdcall __std_tzdb_dealloc_sys_info(_Sys_time_info* _Info) {
    if (_Info) {
        if (_Info->abbrev) {
            delete[] _Info->abbrev;
        }

        delete _Info;
    }
}

_NODISCARD _Local_time_info* __stdcall __std_tzbd_get_local_info(const char* _Tz, size_t _Tz_len, _SysTimeRep _Local) {
    UErrorCode ec{};
    auto _Tz_name = _Covert_UT8_to_UTF16({_Tz, _Tz_len});
    auto _Cal     = _Icu::ucal_open(_Tz_name.c_str(), _Tz_name.length(), nullptr, UCalendarType::UCAL_DEFAULT, &ec);
    if (U_SUCCESS(ec)) {
        _Icu::ucal_setMillis(_Cal, _Local, &ec);
    }

    auto _Info = new _Local_time_info{};
    if (U_SUCCESS(ec)) {
        _Info->first = new _Sys_time_info;
        ec           = _Get_sys_time(_Cal, _Info->first);
    }

    if (U_SUCCESS(ec)) {
        // validate the edge cases when the local time is within 1 day of transition boundaries
        const auto _Curr_sys = _Local - _Info->first->offset;
        if (_Info->first->begin != U_DATE_MIN && _Curr_sys < _Info->first->begin + U_MILLIS_PER_DAY) {
            // get previous transition information
            if (U_SUCCESS(ec)) {
                _Icu::ucal_setMillis(_Cal, _Info->first->begin - 1, &ec);
            }

            auto _Prev_info = new _Sys_time_info;
            if (U_SUCCESS(ec)) {
                ec = _Get_sys_time(_Cal, _Prev_info);
            }

            const auto _Transition = _Info->first->begin;
            const auto _Prev_sys = _Local - _Prev_info->offset;
            if (_Curr_sys >= _Transition) {
                if (_Prev_sys < _Transition) {
                    // Curr:    [-x-----
                    // Prev: -----x-)
                    _Info->result = std::chrono::local_info::ambiguous;
                    _Info->second = _Info->first;
                    _Info->first  = _Prev_info;
                } else {
                    // Curr:      [---x-
                    // Prev: ---)???)
                    __std_tzdb_dealloc_sys_info(_Prev_info);
                }
            } else {
                if (_Prev_sys >= _Transition) {
                    // Curr:      x [---
                    // Prev: ---) x
                    _Info->result = std::chrono::local_info::nonexistent;
                    _Info->second = _Info->first;
                    _Info->first  = _Prev_info;
                } else {
                    // Curr:    [???[---
                    // Prev: -x---)
                    __std_tzdb_dealloc_sys_info(_Info->first);
                    _Info->first = _Prev_info;
                }
            }
        } else if (_Info->first->end != U_DATE_MAX && _Curr_sys > _Info->first->end - U_MILLIS_PER_DAY) {
            // get next transition information
            if (U_SUCCESS(ec)) {
                _Icu::ucal_setMillis(_Cal, _Info->first->end + 1, &ec);
            }

            auto _Next_info = new _Sys_time_info;
            if (U_SUCCESS(ec)) {
                ec = _Get_sys_time(_Cal, _Next_info);
            }

            const auto _Transition = _Info->first->end;
            const auto _Next_sys = _Local - _Next_info->offset;
            if (_Curr_sys < _Transition) {
                if (_Next_sys >= _Transition) {
                    // Curr: -----x-)
                    // Next:    [-x-----
                    _Info->result = std::chrono::local_info::ambiguous;
                    _Info->second = _Next_info;
                } else {
                    // Curr: -x---)
                    // Next:    [???[---
                    __std_tzdb_dealloc_sys_info(_Next_info);
                }
            } else {
                if (_Next_sys < _Transition) {
                    // Curr: ---) x
                    // Next:      x [---
                    _Info->result = std::chrono::local_info::nonexistent;
                    _Info->second = _Next_info;
                } else {
                    // Curr: ---)???)
                    // Next:      [----x-
                    __std_tzdb_dealloc_sys_info(_Info->first);
                    _Info->first = _Next_info;
                }
            }
        }
    }

    _Icu::ucal_close(_Cal);
    if (U_FAILURE(ec)) {
        __std_tzdb_dealloc_local_info(_Info);
        _Info = nullptr;
    }

    return _Info;
}

void __stdcall __std_tzdb_dealloc_local_info(_Local_time_info* _Info) {
    if (_Info) {
        __std_tzdb_dealloc_sys_info(_Info->first);
        __std_tzdb_dealloc_sys_info(_Info->second);
        delete _Info;
    }
}

_NODISCARD void* __stdcall __std_calloc_crt(const size_t count, const size_t size) {
    return _calloc_crt(count, size);
}

void __stdcall __std_free_crt(void* p) {
    _free_crt(p);
}

_END_EXTERN_C
