#ifndef SESSION_CONTEXT_H
#define SESSION_CONTEXT_H

#include <string>
#include <mutex>
#include <memory>

class SessionContext {
public:
  // ---- Singleton Access
  static SessionContext& instance() {
    static SessionContext inst;
    return inst;
  }

  // ---- Thread-Safe Setters
  void setWsKey(const std::string& key) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_wsKey = key;
  }

  void setUsername(const std::string& user) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_username = user;
  }

  // ---- Thread-Safe Getters
  std::string getWsKey() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_wsKey;
  }

  std::string getUsername() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_username;
  }

  // Reset session (misal saat logout/disconnect total)
  void clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_wsKey.clear();
    // m_username biasanya tidak di-clear kalau cuma disconnect socket
  }

private:
  SessionContext() {} // Private Constructor
  
  // ---- Disable Copy/Move
  SessionContext(const SessionContext&) = delete;
  SessionContext& operator=(const SessionContext&) = delete;

  std::string m_wsKey;
  std::string m_username;
  std::mutex m_mutex;
};

#endif // SESSION_CONTEXT_H