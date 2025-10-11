#ifndef CONFIG_H
#define CONFIG_H

#include <string>

// This is our Singleton Class
class Config {
public:
    // 1. Static method to get the only instance of this class
    static Config& getInstance();

    // 2. Getters for each variable we need
    // Made ‘const’ because it does not change the object state
    std::string getHost() const;
    std::string getUsername() const;
    std::string getSocketUrl() const;

private:
    // 3. Constructor made private so that objects cannot be created from outside
    Config();

    // 4. Remove copy constructor and assignment operator
    // This prevents the Singleton from being copied, which would break the pattern.
    Config(const Config&) = delete;
    void operator=(const Config&) = delete;

    // 5. Member variables to store values from .env
    std::string host;
    std::string username;
    std::string socket_url;

    // Helper function to safely retrieve env var
    std::string getEnvVar(const std::string& key);
};

#endif // CONFIG_H
