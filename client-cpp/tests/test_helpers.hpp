#ifndef TEST_HELPERS_HPP
#define TEST_HELPERS_HPP

#include <iostream>
#include <string>

// CHECK_EQ: prints both actual and expected values on failure
#define CHECK_EQ(actual, expected) \
    do { \
        if ((actual) != (expected)) { \
            std::cerr << "  FAIL [" << __FILE__ << ":" << __LINE__ << "]\n" \
                      << "    expected: " << (expected) << "\n" \
                      << "    actual:   " << (actual)   << "\n"; \
            failed++; \
        } else { \
            std::cout << "  PASS: " #actual " == " #expected "\n"; \
        } \
    } while(0)

// CHECK_TRUE: for boolean/predicate checks where printing values is not meaningful
#define CHECK_TRUE(expr) \
    do { \
        if (!(expr)) { \
            std::cerr << "  FAIL [" << __FILE__ << ":" << __LINE__ << "]\n" \
                      << "    expected true: " #expr "\n"; \
            failed++; \
        } else { \
            std::cout << "  PASS: " #expr "\n"; \
        } \
    } while(0)

#endif // TEST_HELPERS_HPP
