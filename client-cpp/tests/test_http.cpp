#include "../include/HttpClient.hpp"
#include <stdio.h>
#include <string>

// All tests require network access — run with: test_http --network
// Without the flag, the test exits as a no-op so CI doesn't fail offline.

static int passed = 0;
static int failed = 0;

static void check(const char* label, bool condition, const char* detail = "") {
    if (condition) {
        printf("[PASS] %s\n", label);
        passed++;
    } else {
        printf("[FAIL] %s  %s\n", label, detail);
        failed++;
    }
}

int main(int argc, char* argv[]) {
    bool networkTests = false;
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--network") networkTests = true;
    }

    if (!networkTests) {
        printf("test_http: pass --network to run (requires internet)\n");
        return 0;
    }

    printf("=== HTTPS GET Tests ===\n\n");

    HttpClient http;

    // Test 1: valid HTTPS request returns 200 and non-empty body
    printf("Test 1: GET https://httpbin.org/get\n");
    {
        auto r = http.get("https://httpbin.org/get");
        check("ok == true",          r.ok_,                     r.error_.c_str());
        check("statusCode == 200",   r.statusCode_ == 200,      std::to_string(r.statusCode_).c_str());
        check("body non-empty",      !r.body_.empty());
    }

    // Test 2: server returns 404 — connection succeeded, status reflects server response
    printf("\nTest 2: GET https://httpbin.org/status/404\n");
    {
        auto r = http.get("https://httpbin.org/status/404");
        check("ok == true",          r.ok_,                     r.error_.c_str());
        check("statusCode == 404",   r.statusCode_ == 404,      std::to_string(r.statusCode_).c_str());
    }

    // Test 3: expired cert must be rejected when verifyCert=true
    printf("\nTest 3: GET https://expired.badssl.com/ (verifyCert=true, expect failure)\n");
    {
        auto r = http.get("https://expired.badssl.com/", true);
        check("ok == false",         !r.ok_,                    "cert should have been rejected");
        check("error non-empty",     !r.error_.empty());
    }

    // Test 4: expired cert accepted when verifyCert=false
    printf("\nTest 4: GET https://expired.badssl.com/ (verifyCert=false, expect success)\n");
    {
        auto r = http.get("https://expired.badssl.com/", false);
        check("ok == true",          r.ok_,                     r.error_.c_str());
        check("statusCode > 0",      r.statusCode_ > 0,         std::to_string(r.statusCode_).c_str());
    }

    // Test 5: hostname mismatch must be rejected
    printf("\nTest 5: GET https://wrong.host.badssl.com/ (hostname mismatch, expect failure)\n");
    {
        auto r = http.get("https://wrong.host.badssl.com/", true);
        check("ok == false",         !r.ok_,                    "hostname mismatch should be rejected");
        check("error non-empty",     !r.error_.empty());
    }

    printf("\n=== Results: %d passed, %d failed ===\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
