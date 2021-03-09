// Copyright (c) Microsoft Corporation.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <yvals_core.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <xfilesystem_abi.h>
#include <xtzdb.h>

#define NOMINMAX
#include <icu.h>
#include <internal_shared.h>

#include <Windows.h>

#pragma comment(lib, "Advapi32")

namespace {
    enum class _Icu_api_level : unsigned long {
        __not_set,
        __detecting,
        __has_failed,
        __has_icu_addresses,
    };

    struct _Icu_functions_table {
        _STD atomic<decltype(&::ucal_close)> _Pfn_ucal_close{nullptr};
        _STD atomic<decltype(&::ucal_get)> _Pfn_ucal_get{nullptr};
        _STD atomic<decltype(&::ucal_getDefaultTimeZone)> _Pfn_ucal_getDefaultTimeZone{nullptr};
        _STD atomic<decltype(&::ucal_getTimeZoneDisplayName)> _Pfn_ucal_getTimeZoneDisplayName{nullptr};
        _STD atomic<decltype(&::ucal_getTimeZoneTransitionDate)> _Pfn_ucal_getTimeZoneTransitionDate{nullptr};
        _STD atomic<decltype(&::ucal_inDaylightTime)> _Pfn_ucal_inDaylightTime{nullptr};
        _STD atomic<decltype(&::ucal_open)> _Pfn_ucal_open{nullptr};
        _STD atomic<decltype(&::ucal_openTimeZoneIDEnumeration)> _Pfn_ucal_openTimeZoneIDEnumeration{nullptr};
        _STD atomic<decltype(&::ucal_setMillis)> _Pfn_ucal_setMillis{nullptr};
        _STD atomic<decltype(&::uenum_close)> _Pfn_uenum_close{nullptr};
        _STD atomic<decltype(&::uenum_count)> _Pfn_uenum_count{nullptr};
        _STD atomic<decltype(&::uenum_unext)> _Pfn_uenum_unext{nullptr};
        _STD atomic<_Icu_api_level> _Api_level{_Icu_api_level::__not_set};
    };

    _Icu_functions_table _Icu_functions;

    template <typename T>
    void _Load_address(const HMODULE _Module, _STD atomic<T>& _Stored_Pfn, LPCSTR _Fn_name, DWORD& _Last_error) {
        const auto _Pfn = reinterpret_cast<T>(GetProcAddress(_Module, _Fn_name));
        if (_Pfn != nullptr) {
            _Stored_Pfn.store(_Pfn, _STD memory_order_relaxed);
        } else {
            _Last_error = GetLastError();
        }
    }

    // FIXME: Inspired from what I found in atomic.cpp. Is this overkill, I wasn't sure what to do
    //        with race conditions and static state. I didn't want to LoadLibraryExW twice.
    _NODISCARD _Icu_api_level _Init_icu_functions(_Icu_api_level _Level) noexcept {
        while (!_Icu_functions._Api_level.compare_exchange_weak(_Level, _Icu_api_level::__detecting)) {
            if (_Level > _Icu_api_level::__detecting) {
                return _Level;
            }
        }

        _Level = _Icu_api_level::__has_failed;

        const HMODULE _Icu_module = LoadLibraryExW(L"icu.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (_Icu_module != nullptr) {
            // collect at least one error if any GetProcAddress call fails
            DWORD _Last_error{0};
            _Load_address(_Icu_module, _Icu_functions._Pfn_ucal_close, "ucal_close", _Last_error);
            _Load_address(_Icu_module, _Icu_functions._Pfn_ucal_get, "ucal_get", _Last_error);
            _Load_address(
                _Icu_module, _Icu_functions._Pfn_ucal_getDefaultTimeZone, "ucal_getDefaultTimeZone", _Last_error);
            _Load_address(_Icu_module, _Icu_functions._Pfn_ucal_getTimeZoneDisplayName, "ucal_getTimeZoneDisplayName",
                _Last_error);
            _Load_address(_Icu_module, _Icu_functions._Pfn_ucal_getTimeZoneTransitionDate,
                "ucal_getTimeZoneTransitionDate", _Last_error);
            _Load_address(_Icu_module, _Icu_functions._Pfn_ucal_inDaylightTime, "ucal_inDaylightTime", _Last_error);
            _Load_address(_Icu_module, _Icu_functions._Pfn_ucal_open, "ucal_open", _Last_error);
            _Load_address(_Icu_module, _Icu_functions._Pfn_ucal_openTimeZoneIDEnumeration,
                "ucal_openTimeZoneIDEnumeration", _Last_error);
            _Load_address(_Icu_module, _Icu_functions._Pfn_ucal_setMillis, "ucal_setMillis", _Last_error);
            _Load_address(_Icu_module, _Icu_functions._Pfn_uenum_close, "uenum_close", _Last_error);
            _Load_address(_Icu_module, _Icu_functions._Pfn_uenum_count, "uenum_count", _Last_error);
            _Load_address(_Icu_module, _Icu_functions._Pfn_uenum_unext, "uenum_unext", _Last_error);
            if (_Last_error == ERROR_SUCCESS) {
                _Level = _Icu_api_level::__has_icu_addresses;
            } else {
                // reset last-error in-case a later GetProcAddress resets it
                SetLastError(_Last_error);
            }
        }

        _Icu_functions._Api_level.store(_Level, _STD memory_order_release);
        return _Level;
    }

    _NODISCARD _Icu_api_level _Acquire_icu_functions() noexcept {
        auto _Level = _Icu_functions._Api_level.load(_STD memory_order_acquire);
        if (_Level <= _Icu_api_level::__detecting) {
            _Level = _Init_icu_functions(_Level);
        }

        return _Level;
    }

    _NODISCARD void __icu_ucal_close(UCalendar* cal) noexcept {
        const auto _Fun = _Icu_functions._Pfn_ucal_close.load(_STD memory_order_relaxed);
        _Fun(cal);
    }

    _NODISCARD int32_t __icu_ucal_get(const UCalendar* cal, UCalendarDateFields field, UErrorCode* status) noexcept {
        const auto _Fun = _Icu_functions._Pfn_ucal_get.load(_STD memory_order_relaxed);
        return _Fun(cal, field, status);
    }

    _NODISCARD int32_t __icu_ucal_getDefaultTimeZone(UChar* result, int32_t resultCapacity, UErrorCode* ec) noexcept {
        const auto _Fun = _Icu_functions._Pfn_ucal_getDefaultTimeZone.load(_STD memory_order_relaxed);
        return _Fun(result, resultCapacity, ec);
    }

    _NODISCARD int32_t __icu_ucal_getTimeZoneDisplayName(const UCalendar* cal, UCalendarDisplayNameType type,
        const char* locale, UChar* result, int32_t resultLength, UErrorCode* status) noexcept {
        const auto _Fun = _Icu_functions._Pfn_ucal_getTimeZoneDisplayName.load(_STD memory_order_relaxed);
        return _Fun(cal, type, locale, result, resultLength, status);
    }

    _NODISCARD UBool __icu_ucal_getTimeZoneTransitionDate(
        const UCalendar* cal, UTimeZoneTransitionType type, UDate* transition, UErrorCode* status) noexcept {
        const auto _Fun = _Icu_functions._Pfn_ucal_getTimeZoneTransitionDate.load(_STD memory_order_relaxed);
        return _Fun(cal, type, transition, status);
    }

    _NODISCARD UBool __icu_ucal_inDaylightTime(const UCalendar* cal, UErrorCode* status) noexcept {
        const auto _Fun = _Icu_functions._Pfn_ucal_inDaylightTime.load(_STD memory_order_relaxed);
        return _Fun(cal, status);
    }

    _NODISCARD UCalendar* __icu_ucal_open(
        const UChar* zoneID, int32_t len, const char* locale, UCalendarType type, UErrorCode* status) noexcept {
        const auto _Fun = _Icu_functions._Pfn_ucal_open.load(_STD memory_order_relaxed);
        return _Fun(zoneID, len, locale, type, status);
    }

    _NODISCARD UEnumeration* __icu_ucal_openTimeZoneIDEnumeration(
        USystemTimeZoneType zoneType, const char* region, const int32_t* rawOffset, UErrorCode* ec) noexcept {
        const auto _Fun = _Icu_functions._Pfn_ucal_openTimeZoneIDEnumeration.load(_STD memory_order_relaxed);
        return _Fun(zoneType, region, rawOffset, ec);
    }

    _NODISCARD void __icu_ucal_setMillis(UCalendar* cal, UDate dateTime, UErrorCode* status) noexcept {
        const auto _Fun = _Icu_functions._Pfn_ucal_setMillis.load(_STD memory_order_relaxed);
        _Fun(cal, dateTime, status);
    }

    _NODISCARD void __icu_uenum_close(UEnumeration* en) noexcept {
        const auto _Fun = _Icu_functions._Pfn_uenum_close.load(_STD memory_order_relaxed);
        _Fun(en);
    }

    _NODISCARD int32_t __icu_uenum_count(UEnumeration* en, UErrorCode* ec) noexcept {
        const auto _Fun = _Icu_functions._Pfn_uenum_count.load(_STD memory_order_relaxed);
        return _Fun(en, ec);
    }

    _NODISCARD const UChar* __icu_uenum_unext(UEnumeration* en, int32_t* resultLength, UErrorCode* status) noexcept {
        const auto _Fun = _Icu_functions._Pfn_uenum_unext.load(_STD memory_order_relaxed);
        return _Fun(en, resultLength, status);
    }

    struct _Tz_link {
        const char* _Target;
        const char* _Name;
    };

    // FIXME: Likely not the final implementation just here to open a design discussion on
    //        how to handle time_zone_link. See test.cpp for further details on the issue.
    static const _Tz_link _Known_links[] = {
        // clang-format off
        // Target                   // Name
        {"Pacific/Auckland",        "Antarctica/McMurdo"},
        {"Africa/Maputo",           "Africa/Lusaka"}
        // clang-format on
    };

    _NODISCARD const char* _Allocate_wide_to_narrow(
        const char16_t* _Input, int _Input_len, __std_tzdb_error& _Err) noexcept {
        const auto _Code_page      = __std_fs_code_page();
        const auto _Input_as_wchar = reinterpret_cast<const wchar_t*>(_Input);
        // FIXME: Is is ok to pull in xfilesystem_abi.h and use these here?
        const auto _Count = __std_fs_convert_wide_to_narrow(_Code_page, _Input_as_wchar, _Input_len, nullptr, 0);
        if (_Count._Err != __std_win_error::_Success) {
            _Err = __std_tzdb_error::_Win_error;
            return nullptr;
        }

        auto* _Data = new (_STD nothrow) char[_Count._Len + 1];
        if (_Data == nullptr) {
            return nullptr;
        }

        _Data[_Count._Len] = '\0';

        const auto _Result =
            __std_fs_convert_wide_to_narrow(_Code_page, _Input_as_wchar, _Input_len, _Data, _Count._Len);
        if (_Result._Err != __std_win_error::_Success) {
            _Err = __std_tzdb_error::_Win_error;
            delete[] _Data;
            return nullptr;
        }

        return _Data;
    }

    _NODISCARD const char16_t* _Allocate_narrow_to_wide(
        const char* _Input, int _Input_len, __std_tzdb_error& _Err) noexcept {
        const auto _Code_page = __std_fs_code_page();

        const auto _Count = __std_fs_convert_narrow_to_wide(_Code_page, _Input, _Input_len, nullptr, 0);
        if (_Count._Err != __std_win_error::_Success) {
            _Err = __std_tzdb_error::_Win_error;
            return nullptr;
        }

        auto* _Data = new (_STD nothrow) char16_t[_Count._Len + 1];
        if (_Data == nullptr) {
            return nullptr;
        }

        _Data[_Count._Len]         = u'\0';
        const auto _Ouput_as_wchar = reinterpret_cast<wchar_t*>(_Data);

        const auto _Result =
            __std_fs_convert_narrow_to_wide(_Code_page, _Input, _Input_len, _Ouput_as_wchar, _Count._Len);
        if (_Result._Err != __std_win_error::_Success) {
            _Err = __std_tzdb_error::_Win_error;
            delete[] _Data;
            return nullptr;
        }

        return _Data;
    }

    _STD unique_ptr<UCalendar, decltype(&__icu_ucal_close)> _Get_cal(
        const char* _Tz, size_t _Tz_len, __std_tzdb_error& _Err) {
        _STD unique_ptr<const char16_t[]> _Tz_name{_Allocate_narrow_to_wide(_Tz, _Tz_len, _Err)};
        if (_Tz_name == nullptr) {
            return {nullptr, &__icu_ucal_close};
        }

        UErrorCode _UErr{};
        _STD unique_ptr<UCalendar, decltype(&__icu_ucal_close)> _Cal{
            __icu_ucal_open(_Tz_name.get(), -1, nullptr, UCalendarType::UCAL_DEFAULT, &_UErr), &__icu_ucal_close};
        if (U_FAILURE(_UErr)) {
            _Err = __std_tzdb_error::_Icu_error;
        }

        return _Cal;
    }

    // returns true if encountered no errors
    bool _Get_sys_time(
        __std_tzdb_sys_data& _Data, UCalendar* _Cal, __std_tzdb_epoch_milli _Sys, __std_tzdb_error& _Err) {

        UErrorCode _UErr{};
        __icu_ucal_setMillis(_Cal, _Sys, &_UErr);

        if (U_SUCCESS(_UErr) && __icu_ucal_inDaylightTime(_Cal, &_UErr)) {
            if (U_SUCCESS(_UErr)) {
                _Data.save = __icu_ucal_get(_Cal, UCalendarDateFields::UCAL_DST_OFFSET, &_UErr);
            }

            if (U_SUCCESS(_UErr)) {
                _Data.offset = __icu_ucal_get(_Cal, UCalendarDateFields::UCAL_ZONE_OFFSET, &_UErr) + _Data.save;
            }
        } else if (U_SUCCESS(_UErr)) {
            _Data.offset = __icu_ucal_get(_Cal, UCalendarDateFields::UCAL_ZONE_OFFSET, &_UErr);
            _Data.save   = 0;
        }

        if (U_SUCCESS(_UErr)) {
            _Data.save = __icu_ucal_get(_Cal, UCalendarDateFields::UCAL_DST_OFFSET, &_UErr);
        }

        if (U_SUCCESS(_UErr)
            && !__icu_ucal_getTimeZoneTransitionDate(
                _Cal, UTimeZoneTransitionType::UCAL_TZ_TRANSITION_PREVIOUS_INCLUSIVE, &_Data.begin, &_UErr)) {
            _Data.begin = U_DATE_MIN;
        }

        if (U_SUCCESS(_UErr)
            && !__icu_ucal_getTimeZoneTransitionDate(
                _Cal, UTimeZoneTransitionType::UCAL_TZ_TRANSITION_NEXT, &_Data.end, &_UErr)) {
            _Data.end = U_DATE_MAX;
        }

        if (U_SUCCESS(_UErr)) {
            auto _Display_type = _Data.save == 0 ? UCalendarDisplayNameType::UCAL_SHORT_STANDARD
                                                 : UCalendarDisplayNameType::UCAL_SHORT_DST;
            char16_t _Name_buf[256];
            auto _Name_buf_len =
                __icu_ucal_getTimeZoneDisplayName(_Cal, _Display_type, nullptr, _Name_buf, sizeof(_Name_buf), &_UErr);
            if (U_SUCCESS(_UErr)) {
                _Data.abbrev = _Allocate_wide_to_narrow(_Name_buf, _Name_buf_len, _Err);
                if (_Data.abbrev == nullptr && _Err == __std_tzdb_error::_Success) {
                    return false;
                }
            }
        }

        if (U_FAILURE(_UErr)) {
            _Err = __std_tzdb_error::_Icu_error;
            return false;
        }

        return true;
    }

} // namespace

_EXTERN_C

_NODISCARD __std_tzdb_time_zones_info* __stdcall __std_tzdb_get_time_zones() noexcept {
    // On exit---
    //    *_Info == nullptr         --> bad_alloc()
    //    _Info->_Err == _Win_error --> failed, call GetLastError()
    //    _Info->_Err == _Icu_error --> runtime_error interacting with ICU
    _STD unique_ptr<__std_tzdb_time_zones_info, decltype(&__std_tzdb_delete_time_zones)> _Info{
        new (_STD nothrow) __std_tzdb_time_zones_info{}, &__std_tzdb_delete_time_zones};
    if (_Info == nullptr) {
        return nullptr;
    }

    if (_Acquire_icu_functions() < _Icu_api_level::__has_icu_addresses) {
        _Info->_Err = __std_tzdb_error::_Win_error;
        return _Info.release();
    }

    UErrorCode _Err{};
    _STD unique_ptr<UEnumeration, decltype(&__icu_uenum_close)> _Enum{
        __icu_ucal_openTimeZoneIDEnumeration(USystemTimeZoneType::UCAL_ZONE_TYPE_CANONICAL, nullptr, nullptr, &_Err),
        &__icu_uenum_close};
    if (U_FAILURE(_Err)) {
        _Info->_Err = __std_tzdb_error::_Icu_error;
        return _Info.release();
    }

    // uenum_count may be expensive but is required to pre allocated arrays.
    int32_t _Num_time_zones = __icu_uenum_count(_Enum.get(), &_Err);
    if (U_FAILURE(_Err)) {
        _Info->_Err = __std_tzdb_error::_Icu_error;
        return _Info.release();
    }

    _Info->_Num_time_zones = static_cast<size_t>(_Num_time_zones);
    _Info->_Names          = new (_STD nothrow) const char*[_Info->_Num_time_zones];
    if (_Info->_Names == nullptr) {
        return nullptr;
    }

    // init to ensure __std_tzdb_delete_init_info cleanup is valid
    _STD fill_n(_Info->_Names, (ptrdiff_t) _Info->_Num_time_zones, nullptr);

    _Info->_Links = new (_STD nothrow) const char*[_Info->_Num_time_zones];
    if (_Info->_Links == nullptr) {
        return nullptr;
    }

    // init to ensure __std_tzdb_delete_init_info cleanup is valid
    _STD fill_n(_Info->_Links, _Info->_Num_time_zones, nullptr);

    for (size_t _Name_idx = 0; _Name_idx < _Info->_Num_time_zones; ++_Name_idx) {
        int32_t _Elem_len{};
        const auto* _Elem = __icu_uenum_unext(_Enum.get(), &_Elem_len, &_Err);
        if (U_FAILURE(_Err) || _Elem == nullptr) {
            _Info->_Err = __std_tzdb_error::_Icu_error;
            return _Info.release();
        }

        _Info->_Names[_Name_idx] = _Allocate_wide_to_narrow(_Elem, _Elem_len, _Info->_Err);
        if (_Info->_Names[_Name_idx] == nullptr) {
            return _Info->_Err != __std_tzdb_error::_Success ? _Info.release() : nullptr;
        }

        // ensure time_zone is not a known time_zone_link
        for (const auto& _Link : _Known_links) {
            if (strcmp(_Info->_Names[_Name_idx], _Link._Name) == 0) {
                _Info->_Links[_Name_idx] = _Link._Target; // no need to allocate a string
            }
        }
    }

    return _Info.release();
}

void __stdcall __std_tzdb_delete_time_zones(__std_tzdb_time_zones_info* _Info) noexcept {
    if (_Info != nullptr) {
        if (_Info->_Names != nullptr) {
            for (size_t _Idx = 0; _Idx < _Info->_Num_time_zones; _Idx++) {
                if (_Info->_Names[_Idx] != nullptr) {
                    delete[] _Info->_Names[_Idx];
                }
            }

            delete[] _Info->_Names;
            _Info->_Names = nullptr;
        }

        if (_Info->_Links != nullptr) {
            delete[] _Info->_Links;
            _Info->_Links = nullptr;
        }
    }
}

_NODISCARD __std_tzdb_current_zone_info* __stdcall __std_tzdb_get_current_zone() noexcept {
    // On exit---
    //    *_Info == nullptr         --> bad_alloc()
    //    _Info->_Err == _Win_error --> failed, call GetLastError()
    //    _Info->_Err == _Icu_error --> runtime_error interacting with ICU
    _STD unique_ptr<__std_tzdb_current_zone_info, decltype(&__std_tzdb_delete_current_zone)> _Info{
        new (_STD nothrow) __std_tzdb_current_zone_info{}, &__std_tzdb_delete_current_zone};
    if (_Info == nullptr) {
        return nullptr;
    }

    if (_Acquire_icu_functions() < _Icu_api_level::__has_icu_addresses) {
        _Info->_Err = __std_tzdb_error::_Win_error;
        return _Info.release();
    }

    UErrorCode _Err{};
    char16_t _Id_buf[256];
    const auto _Id_buf_len = __icu_ucal_getDefaultTimeZone(_Id_buf, sizeof(_Id_buf), &_Err);
    if (U_FAILURE(_Err) || _Id_buf_len == 0) {
        _Info->_Err = __std_tzdb_error::_Icu_error;
        return _Info.release();
    }

    _Info->_Tz_name = _Allocate_wide_to_narrow(_Id_buf, static_cast<size_t>(_Id_buf_len), _Info->_Err);
    if (_Info->_Tz_name == nullptr) {
        return _Info->_Err != __std_tzdb_error::_Success ? _Info.release() : nullptr;
    }

    return _Info.release();
}

void __stdcall __std_tzdb_delete_current_zone(__std_tzdb_current_zone_info* _Info) noexcept {
    if (_Info) {
        delete[] _Info->_Tz_name;
        _Info->_Tz_name = nullptr;
    }
}

_NODISCARD __std_tzbd_sys_info* __stdcall __std_tzbd_get_sys_info(
    const char* _Tz, size_t _Tz_len, __std_tzdb_epoch_milli _Sys) noexcept {
    // On exit---
    //    *_Info == nullptr         --> bad_alloc()
    //    _Info->_Err == _Win_error --> failed, call GetLastError()
    //    _Info->_Err == _Icu_error --> runtime_error interacting with ICU
    _STD unique_ptr<__std_tzbd_sys_info, decltype(&__std_tzdb_delete_sys_info)> _Info{
        new (_STD nothrow) __std_tzbd_sys_info{}, &__std_tzdb_delete_sys_info};
    if (_Info == nullptr) {
        return nullptr;
    }

    auto _Cal = _Get_cal(_Tz, _Tz_len, _Info->_Err);
    if (_Cal == nullptr) {
        return _Info->_Err != __std_tzdb_error::_Success ? _Info.release() : nullptr;
    }

    if (!_Get_sys_time(_Info->_Data, _Cal.get(), _Sys, _Info->_Err)) {
        return _Info->_Err != __std_tzdb_error::_Success ? _Info.release() : nullptr;
    }

    return _Info.release();
}

void __stdcall __std_tzdb_delete_sys_info(__std_tzbd_sys_info* _Info) noexcept {
    if (_Info) {
        delete[] _Info->_Data.abbrev;
        _Info->_Data.abbrev = nullptr;
    }
}

_NODISCARD __std_tzbd_local_info* __stdcall __std_tzbd_get_local_info(
    const char* _Tz, size_t _Tz_len, __std_tzdb_epoch_milli _Local) noexcept {
    // On exit---
    //    *_Info == nullptr         --> bad_alloc()
    //    _Info->_Err == _Win_error --> failed, call GetLastError()
    //    _Info->_Err == _Icu_error --> runtime_error interacting with ICU
    _STD unique_ptr<__std_tzbd_local_info, decltype(&__std_tzdb_delete_local_info)> _Info{
        new (_STD nothrow) __std_tzbd_local_info{}, &__std_tzdb_delete_local_info};
    if (_Info == nullptr) {
        return nullptr;
    }

    auto _Cal = _Get_cal(_Tz, _Tz_len, _Info->_Err);
    if (_Cal == nullptr) {
        return _Info->_Err != __std_tzdb_error::_Success ? _Info.release() : nullptr;
    }

    if (!_Get_sys_time(_Info->_First, _Cal.get(), _Local, _Info->_Err)) {
        return _Info->_Err != __std_tzdb_error::_Success ? _Info.release() : nullptr;
    }

    // validate the edge cases when the local time is within 1 day of transition boundaries
    const auto _Curr_sys = _Local - _Info->_First.offset;
    if (_Info->_First.begin != U_DATE_MIN && _Curr_sys < _Info->_First.begin + U_MILLIS_PER_DAY) {
        // get previous transition information
        if (!_Get_sys_time(_Info->_Second, _Cal.get(), _Info->_First.begin - 1, _Info->_Err)) {
            return _Info->_Err != __std_tzdb_error::_Success ? _Info.release() : nullptr;
        }

        const auto _Transition = _Info->_First.begin;
        const auto _Prev_sys   = _Local - _Info->_Second.offset;
        if (_Curr_sys >= _Transition) {
            if (_Prev_sys < _Transition) {
                // First:     [-x-----
                // Second: -----x-)
                _Info->_Result = _CHRONO local_info::ambiguous;
                std::swap(_Info->_First, _Info->_Second);
            } else {
                // First:       [---x-
                // Second: ---)???)
                _Info->_Result = _CHRONO local_info::unique;
            }
        } else {
            if (_Prev_sys >= _Transition) {
                // First:       x [---
                // Second: ---) x
                _Info->_Result = _CHRONO local_info::nonexistent;
                std::swap(_Info->_First, _Info->_Second);
            } else {
                // First:     [???[---
                // Second: -x---)
                _Info->_Result = _CHRONO local_info::unique;
                std::swap(_Info->_First, _Info->_Second);
            }
        }
    } else if (_Info->_First.end != U_DATE_MAX && _Curr_sys > _Info->_First.end - U_MILLIS_PER_DAY) {
        // get next transition information
        if (!_Get_sys_time(_Info->_Second, _Cal.get(), _Info->_First.end + 1, _Info->_Err)) {
            return _Info->_Err != __std_tzdb_error::_Success ? _Info.release() : nullptr;
        }

        const auto _Transition = _Info->_First.end;
        const auto _Next_sys   = _Local - _Info->_Second.offset;
        if (_Curr_sys < _Transition) {
            if (_Next_sys >= _Transition) {
                // First:  -----x-)
                // Second:    [-x-----
                _Info->_Result = _CHRONO local_info::ambiguous;
            } else {
                // First:  -x---)
                // Second:    [???[---
                _Info->_Result = _CHRONO local_info::unique;
            }
        } else {
            if (_Next_sys < _Transition) {
                // First:  ---) x
                // Second:      x [---
                _Info->_Result = _CHRONO local_info::nonexistent;
            } else {
                // First:  ---)???)
                // Second:      [----x-
                _Info->_Result = _CHRONO local_info::unique;
                std::swap(_Info->_First, _Info->_Second);
            }
        }
    } else {
        // local time is contained inside of _First transition boundaries by at least 1 day
        _Info->_Result = _CHRONO local_info::unique;
    }

    return _Info.release();
}

void __stdcall __std_tzdb_delete_local_info(__std_tzbd_local_info* _Info) noexcept {
    if (_Info) {
        delete[] _Info->_First.abbrev;
        _Info->_First.abbrev = nullptr;
        delete[] _Info->_Second.abbrev;
        _Info->_Second.abbrev = nullptr;
    }
}

__std_tzdb_registry_leap_info* __stdcall __std_tzdb_get_reg_leap_seconds(
    const size_t prev_reg_ls_size, size_t* const current_reg_ls_size) noexcept {
    // On exit---
    //    *current_reg_ls_size <= prev_reg_ls_size, reg_ls_data == nullptr --> no new data
    //    *current_reg_ls_size >  prev_reg_ls_size, reg_ls_data != nullptr --> new data, successfully read
    //    *current_reg_ls_size == 0,                reg_ls_data != nullptr --> new data, failed reading
    //    *current_reg_ls_size >  prev_reg_ls_size, reg_ls_data == nullptr --> new data, failed allocation

    constexpr auto reg_key_name    = LR"(SYSTEM\CurrentControlSet\Control\LeapSecondInformation)";
    constexpr auto reg_subkey_name = L"LeapSeconds";
    *current_reg_ls_size           = 0;
    HKEY leap_sec_key              = nullptr;

    LSTATUS status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, reg_key_name, 0, KEY_READ, &leap_sec_key);
    if (status != ERROR_SUCCESS) {
        // May not exist on older systems. Treat this as equivalent to the key existing but with no data.
        return nullptr;
    }

    DWORD byte_size = 0;
    status          = RegQueryValueExW(leap_sec_key, reg_subkey_name, nullptr, nullptr, nullptr, &byte_size);
    static_assert(sizeof(__std_tzdb_registry_leap_info) == 12);
    const auto ls_size   = byte_size / 12;
    *current_reg_ls_size = ls_size;

    __std_tzdb_registry_leap_info* reg_ls_data = nullptr;
    if ((status == ERROR_SUCCESS || status == ERROR_MORE_DATA) && ls_size > prev_reg_ls_size) {
        try {
            reg_ls_data = new __std_tzdb_registry_leap_info[ls_size];
            status      = RegQueryValueExW(
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

void __stdcall __std_tzdb_delete_reg_leap_seconds(__std_tzdb_registry_leap_info* _Rlsi) noexcept {
    delete[] _Rlsi;
}

_NODISCARD void* __stdcall __std_calloc_crt(const size_t count, const size_t size) noexcept {
    return _calloc_crt(count, size);
}

void __stdcall __std_free_crt(void* p) noexcept {
    _free_crt(p);
}

_END_EXTERN_C
