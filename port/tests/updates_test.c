/* khdays-updates-test: the release versions the update check compares, and the latest release's
 * tag read from GitHub's answer (host/app/release_version.h). */
#include "../host/app/release_version.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(c)                                                                \
    do {                                                                        \
        if (!(c)) {                                                             \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #c); \
            ++failures;                                                         \
        }                                                                       \
    } while (0)

static void test_versions(void)
{
    CHECK(khdays_version_valid("0.1.0") && khdays_version_valid("v0.1.0") && khdays_version_valid("V2"));
    CHECK(khdays_version_valid("1.2") && khdays_version_valid("1.2.3-rc1") && khdays_version_valid("1.2.3+abc"));
    CHECK(!khdays_version_valid("") && !khdays_version_valid("v") && !khdays_version_valid("latest"));
    CHECK(!khdays_version_valid("1.2.3.4") && !khdays_version_valid("1..2") && !khdays_version_valid("1.2.3-"));
    CHECK(!khdays_version_valid(NULL));

    CHECK(khdays_version_compare("v0.1.0", "0.1.0") == 0); /* the tag's v and the build's none */
    CHECK(khdays_version_compare("v0.1.1", "0.1.0") > 0);
    CHECK(khdays_version_compare("v0.2.0", "0.1.9") > 0);
    CHECK(khdays_version_compare("v0.10.0", "0.9.0") > 0); /* numbers, not letters */
    CHECK(khdays_version_compare("v1", "0.9.9") > 0);
    CHECK(khdays_version_compare("v0.1", "0.1.0") == 0);  /* missing parts count 0 */
    CHECK(khdays_version_compare("v0.1.0", "0.1.1") < 0);
    CHECK(khdays_version_compare("v0.2.0-rc1", "0.2.0") < 0); /* a pre-release is the older */
    CHECK(khdays_version_compare("v0.2.0-rc1", "0.1.0") > 0);
    CHECK(khdays_version_compare("v0.1.0+abc", "0.1.0") == 0);
    CHECK(khdays_version_compare("nightly", "0.1.0") < 0); /* no version: older than any */
    CHECK(khdays_version_compare("0.1.0", "nightly") > 0);
}

static void test_answer(void)
{
    char tag[64];
    /* GitHub's answer for the latest release, shortened: the tag among the other fields */
    static const char answer[] =
        "{\n  \"url\": \"https://api.github.com/repos/x/y/releases/1\",\n"
        "  \"html_url\": \"https://github.com/x/y/releases/tag/v0.1.1\",\n  \"id\": 1,\n"
        "  \"author\": {\"login\": \"x\"},\n  \"tag_name\" : \"v0.1.1\",\n  \"draft\": false\n}";
    CHECK(khdays_release_tag(answer, tag, sizeof(tag)) && strcmp(tag, "v0.1.1") == 0);
    CHECK(khdays_release_tag("{\"tag_name\":\"v2.0.0-rc1\"}", tag, sizeof(tag)) && strcmp(tag, "v2.0.0-rc1") == 0);

    /* none, or none that is a version's characters (it goes into text and a web address) */
    CHECK(!khdays_release_tag("{\"name\":\"v0.1.1\"}", tag, sizeof(tag)));
    CHECK(!khdays_release_tag("{\"tag_name\":\"v0.1.1 <b>\"}", tag, sizeof(tag)));
    CHECK(!khdays_release_tag("{\"tag_name\":\"../../evil\"}", tag, sizeof(tag)));
    CHECK(!khdays_release_tag("{\"tag_name\":\"latest\"}", tag, sizeof(tag)));
    CHECK(!khdays_release_tag("{\"tag_name\":\"\"}", tag, sizeof(tag)));
    CHECK(!khdays_release_tag("{\"tag_name\":\"v0.1.1", tag, sizeof(tag))); /* cut short */
    CHECK(!khdays_release_tag("{\"tag_name\": 7}", tag, sizeof(tag)));
    CHECK(!khdays_release_tag(NULL, tag, sizeof(tag)));
    CHECK(!khdays_release_tag("{\"tag_name\":\"v0.1.1\"}", tag, 6)); /* it does not fit */
    CHECK(khdays_release_tag("{\"tag_name\":\"v0.1.1\"}", tag, 7) && strcmp(tag, "v0.1.1") == 0);
}

int main(void)
{
    test_versions();
    test_answer();
    if (failures != 0) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("updates: all checks passed\n");
    return 0;
}
