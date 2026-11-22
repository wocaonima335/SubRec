#pragma once
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#define CHECK(expr) do { if (!(expr)) throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " #expr); } while (false)
struct Temp {
    std::filesystem::path dir;
    explicit Temp(const std::string &name) {
        dir = std::filesystem::temp_directory_path() / ("subrec-b-" + name + "-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        CHECK(std::filesystem::create_directory(dir));
    }
    ~Temp() { std::error_code ec; std::filesystem::remove_all(dir, ec); }
};
inline std::string read(const std::filesystem::path &path)
{
    std::ifstream s(path, std::ios::binary);
    CHECK(s.good());
    return {std::istreambuf_iterator<char>(s), std::istreambuf_iterator<char>()};
}
