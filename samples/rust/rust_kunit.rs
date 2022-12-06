// SPDX-License-Identifier: GPL-2.0

//! Rust KUnit test sample.
//! Note that this uses KUnit directly, not via Rust tests
#![allow(warnings, unused)]

use kernel::c_str;
use kernel::prelude::*;
use kernel::bindings::*;

module! {
    type: RustMinimal,
    name: "rust_kunit",
    author: "David Gow davidgow@google.com>",
    description: "KUnit test written in Rust",
    license: "GPL",
}

struct RustMinimal {
    message: String,
}

impl kernel::Module for RustMinimal {
    fn init(_name: &'static CStr, _module: &'static ThisModule) -> Result<Self> {
        pr_info!("Rust minimal sample (init)\n");
        pr_info!("Am I built-in? {}\n", !cfg!(MODULE));

        Ok(RustMinimal {
            message: "on the heap!".try_to_owned()?,
        })
    }
}

impl Drop for RustMinimal {
    fn drop(&mut self) {
        pr_info!("My message is {}\n", self.message);
        pr_info!("Rust minimal sample (exit)\n");
    }
}

unsafe extern "C" fn rust_sample_test(test: *mut kernel::bindings::kunit) {
        //pr_info!("Running test!\n");
}

static mut testcase : kernel::bindings::kunit_case = kernel::kunit_case!(rust_sample_test);

static mut testcases : &mut[kernel::bindings::kunit_case] = unsafe { &mut[ testcase, kernel::kunit::kunit_null_test_case] };

// TODO: This macro fails so far...
//kernel::kunit_test_suite!(rust_sample2, testcase);

static mut testsuite : kernel::bindings::kunit_suite = unsafe{ kernel::bindings::kunit_suite {
    name: c_str!(core::stringify!(rust_sample2)).as_char_ptr(),
    suite_init: None,
    suite_exit: None,
    init: None,
    exit: None,
    test_cases: unsafe { testcases.as_mut_ptr() },
    status_comment: [0; 256usize],
    debugfs: core::ptr::null_mut(),
    log: core::ptr::null_mut(),
    suite_init_err: 0,
}};

#[used]
#[link_section = ".kunit_test_suites"]
static mut test_suite_entry : *const kernel::bindings::kunit_suite = unsafe { &testsuite };

