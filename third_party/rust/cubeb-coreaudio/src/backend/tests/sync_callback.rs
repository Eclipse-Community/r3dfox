// Copyright © 2026 Mozilla Foundation
//
// This program is made available under an ISC-style license.  See the
// accompanying file LICENSE for details.

use super::*;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;

// Test basic registration and unregistration
#[test]
fn test_sync_callback_register_unregister() {
    let dummy_data = Box::new(42u32);
    let ptr = Box::into_raw(dummy_data);
    let ptr_usize = ptr as usize;

    // Register the pointer
    sync_callback_registry_register(ptr_usize);

    // Verify the callback is called when registered
    let called = Arc::new(AtomicBool::new(false));
    let called_clone = called.clone();
    with_sync_callback_ptr(ptr as *mut c_void, || {
        called_clone.store(true, Ordering::SeqCst);
    });
    assert!(
        called.load(Ordering::SeqCst),
        "Callback should be called when pointer is registered"
    );

    // Unregister the pointer
    sync_callback_registry_unregister(ptr_usize);

    // Verify the callback is NOT called after unregistration
    let called2 = Arc::new(AtomicBool::new(false));
    let called2_clone = called2.clone();
    with_sync_callback_ptr(ptr as *mut c_void, || {
        called2_clone.store(true, Ordering::SeqCst);
    });
    assert!(
        !called2.load(Ordering::SeqCst),
        "Callback should NOT be called after unregistration"
    );

    // Clean up
    unsafe { drop(Box::from_raw(ptr)) };
}

// Test that unregistered pointers don't trigger callbacks
#[test]
fn test_sync_callback_unregistered_pointer() {
    let dummy_data = Box::new(100u32);
    let ptr = Box::into_raw(dummy_data);

    // Try to use callback without registration
    let called = Arc::new(AtomicBool::new(false));
    let called_clone = called.clone();
    with_sync_callback_ptr(ptr as *mut c_void, || {
        called_clone.store(true, Ordering::SeqCst);
    });
    assert!(
        !called.load(Ordering::SeqCst),
        "Callback should NOT be called for unregistered pointer"
    );

    // Clean up
    unsafe { drop(Box::from_raw(ptr)) };
}

// Test multiple registrations don't cause issues
#[test]
fn test_sync_callback_multiple_pointers() {
    let data1 = Box::new(1u32);
    let ptr1 = Box::into_raw(data1);
    let ptr1_usize = ptr1 as usize;

    let data2 = Box::new(2u32);
    let ptr2 = Box::into_raw(data2);
    let ptr2_usize = ptr2 as usize;

    // Register both pointers
    sync_callback_registry_register(ptr1_usize);
    sync_callback_registry_register(ptr2_usize);

    // Both should work
    let called1 = Arc::new(AtomicBool::new(false));
    let called1_clone = called1.clone();
    with_sync_callback_ptr(ptr1 as *mut c_void, || {
        called1_clone.store(true, Ordering::SeqCst);
    });
    assert!(
        called1.load(Ordering::SeqCst),
        "First pointer callback should be called"
    );

    let called2 = Arc::new(AtomicBool::new(false));
    let called2_clone = called2.clone();
    with_sync_callback_ptr(ptr2 as *mut c_void, || {
        called2_clone.store(true, Ordering::SeqCst);
    });
    assert!(
        called2.load(Ordering::SeqCst),
        "Second pointer callback should be called"
    );

    // Unregister first pointer
    sync_callback_registry_unregister(ptr1_usize);

    // First should not work, second should still work
    let called1_after = Arc::new(AtomicBool::new(false));
    let called1_after_clone = called1_after.clone();
    with_sync_callback_ptr(ptr1 as *mut c_void, || {
        called1_after_clone.store(true, Ordering::SeqCst);
    });
    assert!(
        !called1_after.load(Ordering::SeqCst),
        "First pointer callback should NOT be called after unregister"
    );

    let called2_after = Arc::new(AtomicBool::new(false));
    let called2_after_clone = called2_after.clone();
    with_sync_callback_ptr(ptr2 as *mut c_void, || {
        called2_after_clone.store(true, Ordering::SeqCst);
    });
    assert!(
        called2_after.load(Ordering::SeqCst),
        "Second pointer callback should still be called"
    );

    // Clean up
    sync_callback_registry_unregister(ptr2_usize);
    unsafe {
        drop(Box::from_raw(ptr1));
        drop(Box::from_raw(ptr2));
    };
}
