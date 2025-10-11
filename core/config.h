#ifndef CONFIG_H
#define CONFIG_H

#include <string>

// Ini adalah Singleton Class kita
class Config {
public:
    // 1. Method static untuk mendapatkan satu-satunya instance dari class ini
    static Config& getInstance();

    // 2. Getters untuk setiap variabel yang kita butuhkan
    // Dibuat 'const' karena tidak mengubah state object
    std::string getHost() const;
    std::string getUsername() const;
    std::string getSocketUrl() const;

private:
    // 3. Constructor dibuat private agar tidak bisa dibuat objectnya dari luar
    Config();

    // 4. Hapus copy constructor dan assignment operator
    // Ini mencegah Singleton-nya di-copy, yang akan merusak polanya.
    Config(const Config&) = delete;
    void operator=(const Config&) = delete;

    // 5. Member variables untuk menyimpan nilai dari .env
    std::string host;
    std::string username;
    std::string socket_url;

    // Helper function untuk mengambil env var dengan aman
    std::string getEnvVar(const std::string& key);
};

#endif // CONFIG_H
