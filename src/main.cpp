#include <exception>
#include <filesystem>
#include <iostream>

#include "app/Application.h"

int main(int argc, char** argv) {
    try {
        const std::filesystem::path initialModel = argc > 1
            ? std::filesystem::path(argv[1])
            : std::filesystem::path{};
        Application application;
        return application.run(initialModel);
    } catch (const std::exception& error) {
        std::cerr << "Fatal error: " << error.what() << '\n';
        return 1;
    }
}
