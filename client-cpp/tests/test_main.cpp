#include <iostream>
#include <string>

void testMessage(int& failed);
void testUser(int& failed);
void testStore(int& failed);

int main() {
    int failed = 0;

    std::cout << "\n=== Message tests ===\n";
    testMessage(failed);

    std::cout << "\n=== User tests ===\n";
    testUser(failed);

    std::cout << "\n=== MessageStore tests ===\n";
    testStore(failed);

    std::cout << "\n";
    if (failed == 0) {
        std::cout << "All tests passed.\n";
    } else {
        std::cerr << failed << " test(s) FAILED.\n";
    }
    return failed == 0 ? 0 : 1;
}
