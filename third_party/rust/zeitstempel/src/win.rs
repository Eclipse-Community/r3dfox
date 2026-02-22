//! Timestamp implementation for Windows based on `QueryPerformanceCounter`

use std::mem;
use std::sync::OnceLock;

use winapi::um::profileapi::{QueryPerformanceCounter, QueryPerformanceFrequency};
use winapi::um::realtimeapiset::QueryUnbiasedInterruptTime;
use winapi::um::winnt::LARGE_INTEGER;

/// Windows counts time in a system time unit of 100 nanoseconds.
const SYSTEM_TIME_UNIT: u64 = 100;

fn i64_to_large_integer(i: i64) -> LARGE_INTEGER {
    unsafe {
        let mut large_integer: LARGE_INTEGER = mem::zeroed();
        *large_integer.QuadPart_mut() = i;
        large_integer
    }
}

fn large_integer_to_i64(l: LARGE_INTEGER) -> i64 {
    unsafe { *l.QuadPart() }
}

fn frequency() -> i64 {
    static FREQUENCY: OnceLock<i64> = OnceLock::new();

    *FREQUENCY.get_or_init(|| unsafe {
        let mut l = i64_to_large_integer(0);
        QueryPerformanceFrequency(&mut l);
        large_integer_to_i64(l)
    })
}

// Computes (value*numer)/denom without overflow, as long as both
// (numer*denom) and the overall result fit into i64 (which is the case
// for our time conversions).
fn mul_div_i64(value: i64, numer: i64, denom: i64) -> i64 {
    let q = value / denom;
    let r = value % denom;
    // Decompose value as (value/denom*denom + value%denom),
    // substitute into (value*numer)/denom and simplify.
    // r < denom, so (denom*numer) is the upper bound of (r*numer)
    q * numer + r * numer / denom
}

/// The time based on [`QueryPerformanceCounter`].
/// This includes the suspend time.
///
/// [QueryPerformanceCounter]: https://docs.microsoft.com/en-us/windows/win32/api/profileapi/nf-profileapi-queryperformancecounter
pub fn now_including_suspend() -> u64 {
    let mut ticks = i64_to_large_integer(0);
    unsafe {
        assert!(QueryPerformanceCounter(&mut ticks) != 0);
    }
    mul_div_i64(large_integer_to_i64(ticks), 1000000000, frequency()) as u64
}

use std::convert::TryInto;
use std::time::Instant;

use once_cell::sync::Lazy;

static INIT_TIME: Lazy<Instant> = Lazy::new(Instant::now);

pub fn now_awake() -> u64 {
    // This fallback is not used on Windows, and there it probably is wrong because it includes suspend time.
    //
    // This fallback is not used on Linux, though it would still be correct, as it maps to `CLOCK_MONOTONIC`, which does NOT
    // include suspend time.
    //
    // This fallback is not used on macOS, though it would still be correct, as it maps to `mach_absolute_time`, which does NOT
    // include suspend time. But we don't use it there, so no problem.
    //
    // For other operating systems we make no guarantees, other than that we won't panic.
    let now = Instant::now();
    now.checked_duration_since(*INIT_TIME)
        .and_then(|diff| diff.as_nanos().try_into().ok())
        .unwrap_or(0)
}
