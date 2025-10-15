#ifndef CONFIG_H
#define CONFIG_H

#include <string>

// ---- Singleton Class
class Config {
public:
    // 1. Metode static untuk mendapatkan satu-satunya instance dari class ini
    static Config& getInstance();

    // 2. Getters untuk setiap variabel yang diperlukan
    // Didefinisikan sebagai ‘const’ karena tidak mengubah keadaan objek
    std::string getHost() const;
    std::string getUsername() const;
    std::string getSocketUrl() const;

private:
    // 3. Constructor dibuat private sehingga objek tidak dapat dibuat dari luar
    Config();

    // 4. Hapus copy constructor dan assignment operator
    // Hal ini mencegah singleton untuk disalin, yang akan merubah pola tersebut
    Config(const Config&) = delete;
    void operator=(const Config&) = delete;

    // 5. Variabel untuk menyimpan value dari .env
    std::string host;
    std::string username;
    std::string socket_url;

    // Fungsi helper untuk retrieve .env var secara aman
    std::string getEnvVar(const std::string& key);
};

#endif // CONFIG_H
