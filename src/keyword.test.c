#include <platform.h>
#include "kernel/types.h"
#include "keyword.h"
#include "util/language.h"
#include "tests.h"

#include <CuTest.h>

static void test_init_keywords(CuTest *tc) {
    struct locale *lang;

    test_cleanup();
    lang = get_or_create_locale("en");
    locale_setstring(lang, "NACH", "MOVE");
    init_keywords(lang);
    CuAssertIntEquals(tc, K_MOVE, get_keyword("move", lang));
    test_cleanup();
}

static void test_init_keyword(CuTest *tc) {
    struct locale *lang;
    test_cleanup();

    lang = get_or_create_locale("de");
    init_keyword(lang, K_MOVE, "nach");
    init_keyword(lang, K_STUDY, "lernen");
    init_keyword(lang, K_DESTROY, "ZERSTOEREN");
    CuAssertIntEquals(tc, K_MOVE, get_keyword("nach", lang));
    CuAssertIntEquals(tc, K_STUDY, get_keyword("lerne", lang));
    CuAssertIntEquals(tc, K_DESTROY, get_keyword("ZERSTÖREN", lang));
    CuAssertIntEquals(tc, NOKEYWORD, get_keyword("potato", lang));
    test_cleanup();
}

static void test_findkeyword(CuTest *tc) {
    test_cleanup();
    CuAssertIntEquals(tc, K_MOVE, findkeyword("NACH"));
    CuAssertIntEquals(tc, K_STUDY, findkeyword("LERNEN"));
    CuAssertIntEquals(tc, NOKEYWORD, findkeyword(""));
    CuAssertIntEquals(tc, NOKEYWORD, findkeyword("potato"));
}

static void test_get_keyword_default(CuTest *tc) {
    struct locale *lang;
    test_cleanup();
    lang = get_or_create_locale("en");
    CuAssertIntEquals(tc, NOKEYWORD, get_keyword("potato", lang));
    CuAssertIntEquals(tc, K_MOVE, get_keyword("NACH", lang));
    CuAssertIntEquals(tc, K_STUDY, get_keyword("LERNEN", lang));
}

#define SUITE_DISABLE_TEST(suite, test) (void)test

CuSuite *get_keyword_suite(void)
{
  CuSuite *suite = CuSuiteNew();
  SUITE_ADD_TEST(suite, test_init_keyword);
  SUITE_ADD_TEST(suite, test_init_keywords);
  SUITE_ADD_TEST(suite, test_findkeyword);
  SUITE_DISABLE_TEST(suite, test_get_keyword_default);
  return suite;
}

