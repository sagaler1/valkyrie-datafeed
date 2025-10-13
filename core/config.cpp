#include "config.h"
#include "dotenv.h"   // init()
#include <stdexcept>  // throw error
#include <cstdlib>    // std::getenv

// Implementasi method static getInstance()
Config& Config::getInstance() {
    // C++11 menjamin bahwa inisialisasi static local variable
    // hanya terjadi sekali dan thread-safe. Ini cara modern bikin Singleton.
    static Config instance;
    return instance;
}

// Constructor implementation
// Load variables form .env
Config::Config() {
    dotenv::init(); // Init call

    // Call helper for variable loads
    host = getEnvVar("PLUGIN_HOST");
    username = getEnvVar("PLUGIN_USERNAME");
    socket_url = getEnvVar("PLUGIN_SOCKET");
}

// Implementasi Getters
std::string Config::getHost() const {
    return host;
}

std::string Config::getUsername() const {
    return username;
}

std::string Config::getSocketUrl() const {
    return socket_url;
}

// Helper function implementation
std::string Config::getEnvVar(const std::string& key) {
    const char* value = std::getenv(key.c_str());
    if (value == nullptr) {
        // If the env variable does not exist, the program will stop with an error message.
        // This is better than the program crashing randomly elsewhere.
        throw std::runtime_error("Environment variable not found: " + key);
    }
    return std::string(value);
}