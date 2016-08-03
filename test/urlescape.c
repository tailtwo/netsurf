/*
 * Copyright 2016 Vincent Sanders <vince@netsurf-browser.org>
 *
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 *
 * NetSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * NetSurf is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * \file
 * Test url percent encoding operations.
 *
 * Percent encoding of URI is subject to RFC3986 however this is not
 * testing URI behaviour purely the percent encoding so only the
 * unreserved set is not encoded.
 *
 * \note Earlier RFC (2396, 1738 and 1630) list the tilde ~ character
 * as reserved so its handling is ambiguious
 *
 * \todo The url_escape should be tested for application/x-www-form-urlencoded type operation
 */

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <check.h>

#include "utils/url.h"

#define NELEMS(x)  (sizeof(x) / sizeof((x)[0]))
#define SLEN(x) (sizeof((x)) - 1)

struct test_pairs {
	const char* test;
	const size_t test_len;
	const char* res;
	const size_t res_len;
};

const char all_chars[] =
	    "\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f"
	"\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c\x1d\x1e\x1f"
	"\x20\x21\x22\x23\x24\x25\x26\x27\x28\x29\x2a\x2b\x2c\x2d\x2e\x2f"
	"\x30\x31\x32\x33\x34\x35\x36\x37\x38\x39\x3a\x3b\x3c\x3d\x3e\x3f"
	"\x40\x41\x42\x43\x44\x45\x46\x47\x48\x49\x4a\x4b\x4c\x4d\x4e\x4f"
	"\x50\x51\x52\x53\x54\x55\x56\x57\x58\x59\x5a\x5b\x5c\x5d\x5e\x5f"
	"\x60\x61\x62\x63\x64\x65\x66\x67\x68\x69\x6a\x6b\x6c\x6d\x6e\x6f"
	"\x70\x71\x72\x73\x74\x75\x76\x77\x78\x79\x7a\x7b\x7c\x7d\x7e\x7f"
	"\x80\x81\x82\x83\x84\x85\x86\x87\x88\x89\x8a\x8b\x8c\x8d\x8e\x8f"
	"\x90\x91\x92\x93\x94\x95\x96\x97\x98\x99\x9a\x9b\x9c\x9d\x9e\x9f"
	"\xa0\xa1\xa2\xa3\xa4\xa5\xa6\xa7\xa8\xa9\xaa\xab\xac\xad\xae\xaf"
	"\xb0\xb1\xb2\xb3\xb4\xb5\xb6\xb7\xb8\xb9\xba\xbb\xbc\xbd\xbe\xbf"
	"\xc0\xc1\xc2\xc3\xc4\xc5\xc6\xc7\xc8\xc9\xca\xcb\xcc\xcd\xce\xcf"
	"\xd0\xd1\xd2\xd3\xd4\xd5\xd6\xd7\xd8\xd9\xda\xdb\xdc\xdd\xde\xdf"
	"\xe0\xe1\xe2\xe3\xe4\xe5\xe6\xe7\xe8\xe9\xea\xeb\xec\xed\xee\xef"
	"\xf0\xf1\xf2\xf3\xf4\xf5\xf6\xf7\xf8\xf9\xfa\xfb\xfc\xfd\xfe\xff";

const char most_escaped_upper[] =
	   "%01%02%03%04%05%06%07%08%09%0A%0B%0C%0D%0E%0F"
	"%10%11%12%13%14%15%16%17%18%19%1A%1B%1C%1D%1E%1F"
	"%20%21%22%23%24%25%26%27%28%29%2A%2B%2C-.%2F"
	"0123456789%3A%3B%3C%3D%3E%3F"
	"%40ABCDEFGHIJKLMNO"
	"PQRSTUVWXYZ%5B%5C%5D%5E_"
	"%60abcdefghijklmno"
	"pqrstuvwxyz%7B%7C%7D%7E%7F"
	"%80%81%82%83%84%85%86%87%88%89%8A%8B%8C%8D%8E%8F"
	"%90%91%92%93%94%95%96%97%98%99%9A%9B%9C%9D%9E%9F"
	"%A0%A1%A2%A3%A4%A5%A6%A7%A8%A9%AA%AB%AC%AD%AE%AF"
	"%B0%B1%B2%B3%B4%B5%B6%B7%B8%B9%BA%BB%BC%BD%BE%BF"
	"%C0%C1%C2%C3%C4%C5%C6%C7%C8%C9%CA%CB%CC%CD%CE%CF"
	"%D0%D1%D2%D3%D4%D5%D6%D7%D8%D9%DA%DB%DC%DD%DE%DF"
	"%E0%E1%E2%E3%E4%E5%E6%E7%E8%E9%EA%EB%EC%ED%EE%EF"
	"%F0%F1%F2%F3%F4%F5%F6%F7%F8%F9%FA%FB%FC%FD%FE%FF";

const char all_escaped_lower[] =
	   "%01%02%03%04%05%06%07%08%09%0A%0B%0C%0D%0E%0F"
	"%10%11%12%13%14%15%16%17%18%19%1a%1b%1c%1d%1e%1f"
	"%20%21%22%23%24%25%26%27%28%29%2a%2b%2c%2d%2e%2f"
	"%30%31%32%33%34%35%36%37%38%39%3a%3b%3c%3d%3e%3f"
	"%40%41%42%43%44%45%46%47%48%49%4a%4b%4c%4d%4e%4f"
	"%50%51%52%53%54%55%56%57%58%59%5a%5b%5c%5d%5e%5f"
	"%60%61%62%63%64%65%66%67%68%69%6a%6b%6c%6d%6e%6f"
	"%70%71%72%73%74%75%76%77%78%79%7a%7b%7c%7d%7e%7f"
	"%80%81%82%83%84%85%86%87%88%89%8a%8b%8c%8d%8e%8f"
	"%90%91%92%93%94%95%96%97%98%99%9a%9b%9c%9d%9e%9f"
	"%a0%a1%a2%a3%a4%a5%a6%a7%a8%a9%aa%ab%ac%ad%ae%af"
	"%b0%b1%b2%b3%b4%b5%b6%b7%b8%b9%ba%bb%bc%bd%be%bf"
	"%c0%c1%c2%c3%c4%c5%c6%c7%c8%c9%ca%cb%cc%cd%ce%cf"
	"%d0%d1%d2%d3%d4%d5%d6%d7%d8%d9%da%db%dc%dd%de%df"
	"%e0%e1%e2%e3%e4%e5%e6%e7%e8%e9%ea%eb%ec%ed%ee%ef"
	"%f0%f1%f2%f3%f4%f5%f6%f7%f8%f9%fa%fb%fc%fd%fe%ff";

/** simple string that needs no escaping nor has escape in it */
const char simple_string[] = "A.string.that.does.not.need.escaping";

static const struct test_pairs url_escape_test_vec[] = {
	{ "", 0, "" , 0 },
	{ &simple_string[0], SLEN(simple_string), &simple_string[0], 0 },
	{ " ", 1, "%20" , 0 },
	{ &all_chars[0], SLEN(all_chars), &most_escaped_upper[0], 0 },
};


START_TEST(url_escape_test)
{
	nserror err;
	char *esc_str;
	const struct test_pairs *tst = &url_escape_test_vec[_i];

	err = url_escape(tst->test, false, "", &esc_str);
	ck_assert(err == NSERROR_OK);

	/* ensure result data is correct */
	ck_assert_str_eq(esc_str, tst->res);

	free(esc_str);
}
END_TEST

START_TEST(url_escape_api_nullparam_test)
{
	nserror err;
	char *esc_str;

	err = url_escape(NULL, false, NULL, &esc_str);
	ck_assert(err == NSERROR_BAD_PARAMETER);

	err = url_escape(&simple_string[0], false, NULL, NULL);
	ck_assert(err == NSERROR_BAD_PARAMETER);

	err = url_escape(&simple_string[0], false, NULL, &esc_str);
	ck_assert(err == NSERROR_OK);

	free(esc_str);
}
END_TEST

TCase *url_escape_case_create(void)
{
	TCase *tc;
	tc = tcase_create("Escape");

	tcase_add_test(tc, url_escape_api_nullparam_test);

	tcase_add_loop_test(tc, url_escape_test,
			    0, NELEMS(url_escape_test_vec));

	return tc;
}

static const struct test_pairs url_unescape_test_vec[] = {
	{ "", 0, "" , 0 }, /* empty string */
	{ "%20", 3, " ", 1 }, /* single character properly escaped */
	{ "%0G", 3, "%0G", 3 }, /* single character with bad hex value */
	{ "%20%0G%20", 9, " %0G ", 5 }, /* three src chars with bad hex value */
	{ "%20%00%20", 9, " ", 3 }, /* three src chars with null hex value */
	{ &simple_string[0], SLEN(simple_string),
	  &simple_string[0], SLEN(simple_string) }, /* normal string with no percent encoded characters */
	{ &most_escaped_upper[0], SLEN(most_escaped_upper),
	  &all_chars[0], SLEN(all_chars) }, /* all characters that must be percent encoded */
	{ &all_escaped_lower[0], SLEN(all_escaped_lower),
	  &all_chars[0], SLEN(all_chars) }, /* all characters percent encoded */
};

/**
 * test that unescapes a test vector of strings and checks the result
 *
 * The source data length is not given. The result must be the correct
 * length and match the expected output.
 */
START_TEST(url_unescape_simple_test)
{
	nserror err;
	char *unesc_str;
	size_t unesc_length;

	const struct test_pairs *tst = &url_unescape_test_vec[_i];

	err = url_unescape(tst->test, 0 , &unesc_length, &unesc_str);
	ck_assert(err == NSERROR_OK);

	/* ensure length */
	ck_assert_uint_eq(unesc_length, tst->res_len);

	/* ensure contents */
	ck_assert_str_eq(unesc_str, tst->res);
}
END_TEST


/**
 * test that unescapes a test vector of strings and checks the result
 *
 * The source data length is specified. The result must be the correct
 * length and match the expected output.
 */
START_TEST(url_unescape_length_test)
{
	nserror err;
	char *unesc_str;
	size_t unesc_length;

	const struct test_pairs *tst = &url_unescape_test_vec[_i];

	err = url_unescape(tst->test, tst->test_len , &unesc_length, &unesc_str);

	ck_assert(err == NSERROR_OK);

	/* ensure length */
	ck_assert_uint_eq(unesc_length, tst->res_len);

	/* ensure contents */
	ck_assert_str_eq(unesc_str, tst->res);

	free(unesc_str);
}
END_TEST


/**
 * Test to ensure unescape works as expected when no returned length
 * is provided.
 */
START_TEST(url_unescape_api_retlen_test)
{
	nserror err;
	char *unesc_str;

	err = url_unescape(&most_escaped_upper[0], 0, NULL, &unesc_str);

	ck_assert(err == NSERROR_OK);

	/* ensure length */
	ck_assert_uint_eq(strlen(unesc_str), SLEN(all_chars));

	/* ensure contents */
	ck_assert_str_eq(unesc_str, &all_chars[0]);

	free(unesc_str);
}
END_TEST

START_TEST(url_unescape_api_nullparam_test)
{
	nserror err;
	char *unesc_str;

	err = url_unescape(NULL, 0, NULL, &unesc_str);
	ck_assert(err == NSERROR_BAD_PARAMETER);

	err = url_unescape(&simple_string[0], 0, NULL, NULL);
	ck_assert(err == NSERROR_BAD_PARAMETER);
}
END_TEST


TCase *url_unescape_case_create(void)
{
	TCase *tc;
	tc = tcase_create("Unescape");

	tcase_add_test(tc, url_unescape_api_nullparam_test);

	tcase_add_test(tc, url_unescape_api_retlen_test);

	tcase_add_loop_test(tc, url_unescape_simple_test,
			    0, NELEMS(url_unescape_test_vec));

	tcase_add_loop_test(tc, url_unescape_length_test,
			    0, NELEMS(url_unescape_test_vec));

	return tc;
}


Suite *urlescape_suite_create(void)
{
	Suite *s;
	s = suite_create("Percent escaping");

	suite_add_tcase(s, url_escape_case_create());
	suite_add_tcase(s, url_unescape_case_create());

	return s;
}



int main(int argc, char **argv)
{
	int number_failed;
	SRunner *sr;

	sr = srunner_create(urlescape_suite_create());
	//srunner_add_suite(sr, bar_suite_create());

	srunner_run_all(sr, CK_ENV);

	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
