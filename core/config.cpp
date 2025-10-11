#include "config.h"
#include "dotenv.h"   // Butuh ini untuk init()
#include <stdexcept>  // Butuh ini untuk throw error
#include <cstdlib>    // Butuh ini untuk std::getenv

// Implementasi method static getInstance()
Config& Config::getInstance() {
    // C++11 menjamin bahwa inisialisasi static local variable
    // hanya terjadi sekali dan thread-safe. Ini cara modern bikin Singleton.
    static Config instance;
    return instance;
}

// Implementasi Constructor
// Di sinilah kita memuat semua variabel dari .env
Config::Config() {
    dotenv::init(); // Cukup panggil init di sini!

    // Panggil helper untuk memuat setiap variabel
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

// Implementasi helper function
std::string Config::getEnvVar(const std::string& key) {
    const char* value = std::getenv(key.c_str());
    if (value == nullptr) {
        // Jika env var tidak ada, program akan berhenti dengan error
        // Ini lebih baik daripada crash di tempat lain secara acak
        throw std::runtime_error("Environment variable not found: " + key);
    }
    return std::string(value);
}