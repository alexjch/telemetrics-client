/*
 * This program is part of the Clear Linux Project
 *
 * Copyright 2025 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms and conditions of the GNU Lesser General Public License, as
 * published by the Free Software Foundation; either version 2.1 of the License,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
 * details.
 */

#include <check.h>


START_TEST(test_add) {
    int result = add(2, 3);
    ck_assert_int_eq(result, 5); // Check if the result is as expected
}
END_TEST

Suite *example_suite(void) {
    Suite *s;
    TCase *tc_core;

    s = suite_create("Example");
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_add); // Add the test case to the test suite
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void) {
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = example_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? 0 : 1; // Return 0 if all tests pass
}