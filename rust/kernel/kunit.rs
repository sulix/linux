// SPDX-License-Identifier: GPL-2.0

//! KUnit-based macros for Rust unit tests.
//!
//! C header: [`include/kunit/test.h`](../../../../../include/kunit/test.h)
//!
//! Reference: <https://www.kernel.org/doc/html/latest/dev-tools/kunit/index.html>

/// Asserts that a boolean expression is `true` at runtime.
///
/// Public but hidden since it should only be used from generated tests.
///
/// Unlike the one in `core`, this one does not panic; instead, it is mapped to the KUnit
/// facilities. See [`assert!`] for more details.
#[doc(hidden)]
#[macro_export]
macro_rules! kunit_assert {
    ($test:expr, $cond:expr $(,)?) => {{
        if !$cond {
            #[repr(transparent)]
            struct Location($crate::bindings::kunit_loc);

            #[repr(transparent)]
            struct UnaryAssert($crate::bindings::kunit_unary_assert);

            // SAFETY: There is only a static instance and in that one the pointer field
            // points to an immutable C string.
            unsafe impl Sync for Location {}

            // SAFETY: There is only a static instance and in that one the pointer field
            // points to an immutable C string.
            unsafe impl Sync for UnaryAssert {}

            static FILE: &'static $crate::str::CStr = $crate::c_str!(core::file!());
            static LOCATION: Location = Location($crate::bindings::kunit_loc {
                file: FILE.as_char_ptr(),
                line: core::line!() as i32,
            });
            static CONDITION: &'static $crate::str::CStr = $crate::c_str!(stringify!($cond));
            static ASSERTION: UnaryAssert = UnaryAssert($crate::bindings::kunit_unary_assert {
                assert: $crate::bindings::kunit_assert {
                    format: Some($crate::bindings::kunit_unary_assert_format),
                },
                condition: CONDITION.as_char_ptr(),
                expected_true: true,
            });

            // SAFETY:
            //   - FFI call.
            //   - The `test` pointer is valid because this hidden macro should only be called by
            //     the generated documentation tests which forward the test pointer given by KUnit.
            //   - The string pointers (`file` and `condition`) point to null-terminated ones.
            //   - The function pointer (`format`) points to the proper function.
            //   - The pointers passed will remain valid since they point to statics.
            //   - The format string is allowed to be null.
            //   - There are, however, problems with this: first of all, this will end up stopping
            //     the thread, without running destructors. While that is problematic in itself,
            //     it is considered UB to have what is effectively an forced foreign unwind
            //     with `extern "C"` ABI. One could observe the stack that is now gone from
            //     another thread. We should avoid pinning stack variables to prevent library UB,
            //     too. For the moment, given test failures are reported immediately before the
            //     next test runs, that test failures should be fixed and that KUnit is explicitly
            //     documented as not suitable for production environments, we feel it is reasonable.
            unsafe {
                $crate::bindings::kunit_do_failed_assertion(
                    $test,
                    core::ptr::addr_of!(LOCATION.0),
                    $crate::bindings::kunit_assert_type_KUNIT_ASSERTION,
                    core::ptr::addr_of!(ASSERTION.0.assert),
                    core::ptr::null(),
                );
            }
        }
    }};
}

/// Asserts that two expressions are equal to each other (using [`PartialEq`]).
///
/// Public but hidden since it should only be used from generated tests.
///
/// Unlike the one in `core`, this one does not panic; instead, it is mapped to the KUnit
/// facilities. See [`assert!`] for more details.
#[doc(hidden)]
#[macro_export]
macro_rules! kunit_assert_eq {
    ($test:expr, $left:expr, $right:expr $(,)?) => {{
        // For the moment, we just forward to the expression assert because,
        // for binary asserts, KUnit supports only a few types (e.g. integers).
        $crate::kunit_assert!($test, $left == $right);
    }};
}

#[macro_export]
macro_rules! kunit_case {
    ($name:ident) => {{
        $crate::bindings::kunit_case {
            run_case: Some($name),
            name: c_str!(core::stringify!($name)).as_char_ptr(),
            // Not a parameterised test.
            generate_params: None,
            // Private fields, all null.
            status: kunit_status_KUNIT_SUCCESS,
            log: core::ptr::null_mut(),
        }
    }};
}

/* We need this to NULL-terminate the list of test cases. */
#[used]
pub static mut kunit_null_test_case : bindings::kunit_case = bindings::kunit_case {
    run_case: None,
    name: core::ptr::null_mut(),
    generate_params: None,
    // Internal only
    status: bindings::kunit_status_KUNIT_SUCCESS,
    log: core::ptr::null_mut(),
};


#[macro_export]
macro_rules! kunit_test_suite {
    ($name:ident, suite_init $suite_init:ident, suite_exit $suite_exit:ident, init $init:ident, exit $exit:ident, $($test_cases:ident),+) => {
        static mut testsuite : $crate::bindings::kunit_suite = unsafe { $crate::bindings::kunit_suite {
            name: c_str!(core::stringify!($name)).as_char_ptr(),
            suite_init: $suite_init,
            suite_exit: $suite_exit,
            init: $init,
            exit: $exit,
            test_cases: unsafe {  static mut testcases : &mut[$crate::bindings::kunit_case] = &mut[ $($test_cases,)+ $crate::kunit::kunit_null_test_case ]; testcases.as_mut_ptr() },
            status_comment: [0; 256usize],
            debugfs: core::ptr::null_mut(),
            log: core::ptr::null_mut(),
            suite_init_err: 0,
        }};

        #[used]
        #[link_section = ".kunit_test_suites"]
        static mut test_suite_entry : *const kernel::bindings::kunit_suite = unsafe { &testsuite };
    };

    ($name:ident, $($test_cases:ident),+) => {
        $crate::kunit_test_suite!{$name, suite_init None, suite_exit None, init None, exit None $(,$test_cases)+}
    }

}
