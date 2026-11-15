// Copyright 2012-2014 The Rust Project Developers. See the COPYRIGHT
// file at the top-level directory of this distribution and at
// http://rust-lang.org/COPYRIGHT.
//
// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::io::Error;
use std::mem::MaybeUninit;
use std::ptr;

use super::win_bindings::{
    FILETIME, SYSTEMTIME, SystemTimeToFileTime, SystemTimeToTzSpecificLocalTime,
    TzSpecificLocalTimeToSystemTime,
};
use crate::{Datelike, FixedOffset, MappedLocalTime, NaiveDateTime, Timelike};

// The caller must use the failure value specified by the Windows API so the output remains
// uninitialized only when we return an error.
macro_rules! windows_sys_call {
    ($name:ident($($arg:expr),*), $error_id:expr) => {
        if $name($($arg),*) == $error_id {
            return Err(Error::last_os_error());
        }
    };
}

const HECTONANOSECS_IN_SEC: i64 = 10_000_000;
const HECTONANOSEC_TO_UNIX_EPOCH: i64 = 11_644_473_600 * HECTONANOSECS_IN_SEC;

pub(super) fn offset_from_utc_datetime(utc: &NaiveDateTime) -> MappedLocalTime<FixedOffset> {
    offset(utc, false)
}

pub(super) fn offset_from_local_datetime(local: &NaiveDateTime) -> MappedLocalTime<FixedOffset> {
    offset(local, true)
}

fn offset(dt: &NaiveDateTime, local: bool) -> MappedLocalTime<FixedOffset> {
    let naive_sys_time = system_time_from_naive_date_time(dt);
    let result = if local {
        from_local_time(naive_sys_time)
    } else {
        from_utc_time(naive_sys_time)
    };

    match result {
        Ok(offset) => MappedLocalTime::Single(offset),
        Err(_) => MappedLocalTime::None,
    }
}

fn from_utc_time(utc_time: SYSTEMTIME) -> Result<FixedOffset, Error> {
    let local_time = utc_to_local_time(&utc_time)?;
    let utc_secs = system_time_as_unix_seconds(&utc_time)?;
    let local_secs = system_time_as_unix_seconds(&local_time)?;
    let offset = (local_secs - utc_secs) as i32;
    Ok(FixedOffset::east_opt(offset).unwrap())
}

fn from_local_time(local_time: SYSTEMTIME) -> Result<FixedOffset, Error> {
    let utc_time = local_to_utc_time(&local_time)?;
    let utc_secs = system_time_as_unix_seconds(&utc_time)?;
    let local_secs = system_time_as_unix_seconds(&local_time)?;
    let offset = (local_secs - utc_secs) as i32;
    Ok(FixedOffset::east_opt(offset).unwrap())
}

fn system_time_from_naive_date_time(dt: &NaiveDateTime) -> SYSTEMTIME {
    SYSTEMTIME {
        wYear: dt.year() as u16,
        wMonth: dt.month() as u16,
        wDayOfWeek: dt.weekday().num_days_from_sunday() as u16,
        wDay: dt.day() as u16,
        wHour: dt.hour() as u16,
        wMinute: dt.minute() as u16,
        wSecond: dt.second() as u16,
        wMilliseconds: 0,
    }
}

fn local_to_utc_time(local: &SYSTEMTIME) -> Result<SYSTEMTIME, Error> {
    let mut sys_time = MaybeUninit::<SYSTEMTIME>::uninit();
    unsafe {
        windows_sys_call!(
            TzSpecificLocalTimeToSystemTime(ptr::null(), local, sys_time.as_mut_ptr()),
            0
        );
        Ok(sys_time.assume_init())
    }
}

fn utc_to_local_time(utc_time: &SYSTEMTIME) -> Result<SYSTEMTIME, Error> {
    let mut local = MaybeUninit::<SYSTEMTIME>::uninit();
    unsafe {
        windows_sys_call!(
            SystemTimeToTzSpecificLocalTime(ptr::null(), utc_time, local.as_mut_ptr()),
            0
        );
        Ok(local.assume_init())
    }
}

fn system_time_as_unix_seconds(st: &SYSTEMTIME) -> Result<i64, Error> {
    let mut init = MaybeUninit::<FILETIME>::uninit();
    let filetime = unsafe {
        windows_sys_call!(SystemTimeToFileTime(st, init.as_mut_ptr()), 0);
        init.assume_init()
    };
    let bit_shift = ((filetime.dwHighDateTime as u64) << 32) | (filetime.dwLowDateTime as u64);
    Ok((bit_shift as i64 - HECTONANOSEC_TO_UNIX_EPOCH) / HECTONANOSECS_IN_SEC)
}
