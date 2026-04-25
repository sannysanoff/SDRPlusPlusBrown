#pragma once

#ifdef BUILD_TESTS
#include <string>
#include <map>
#include <functional>
#include <utils/flog.h>

namespace sdrpp {
namespace test {

// Test function type
using TestFunction = std::function<void()>;

// Test registry class
class TestRegistry {
public:
    // Register a test function
    static void registerTest(const std::string& name, TestFunction func) {
        getRegistry()[name] = func;
    }

    // Run a test by name
    static void runTest(const std::string& name) {
        auto& registry = getRegistry();
        auto it = registry.find(name);
        if (it == registry.end()) {
            flog::error("Test '{}' not found", name);
            exit(1);
        }
        
        flog::info("Running test: {}", name);
        it->second();
    }

    // List all available tests
    static void listTests() {
        auto& registry = getRegistry();
        flog::info("Available tests:");
        for (const auto& [name, _] : registry) {
            flog::info("  - {}", name);
        }
    }

private:
    // Get the registry singleton
    static std::map<std::string, TestFunction>& getRegistry() {
        static std::map<std::string, TestFunction> registry;
        return registry;
    }
};

// Macro to register a test
#define REGISTER_TEST(name, func) \
    namespace { \
        struct TestRegistration_##name { \
            TestRegistration_##name() { \
                sdrpp::test::TestRegistry::registerTest(#name, func); \
            } \
        }; \
        TestRegistration_##name registration_##name; \
    }

    extern bool failed;
} // namespace test
} // namespace sdrpp
#endif // BUILD_TESTS
