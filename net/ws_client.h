#ifndef WS_CLIENT_H
#define WS_CLIENT_H

#include <string>
#include <thread>
#include <atomic>
#include <memory>
#include <vector>
#include "ixwebsocket/IXWebSocket.h"
#include <windows.h> // Diperlukan untuk HWND

// Forward declaration
class DataStore;

class WsClient {
private:
  std::unique_ptr<ix::WebSocket> m_ws;
  std::thread m_thread;
  std::thread m_pingThread;
  
  std::atomic<bool> m_run;
  std::atomic<bool> m_isConnected;

  // Data yang diperlukan untuk koneksi
  std::string m_userId;
  std::string m_wsKeyUrl;
  HWND m_hAmiBrokerWnd;
  std::atomic<int>* m_pStatus;                    // Pointer untuk update status global
  std::vector<std::string> m_subscribedSymbols;   // State management untuk subscribe

  // Metode internal
  void run();
  void pingLoop();

  // Fungsi helper Protobuf
  std::string fetchWsKey(const std::string& url);
  std::string buildHandshakeBinary(const std::string& userId, const std::string& key);
  std::string buildPingBinary();
  std::string buildSubscribeBinary(const std::string& userId, const std::string& key, const std::vector<std::string>& symbols);

  //std::vector<std::string> loadSymbols(const std::string& path = "symbols.txt");    // Subscribe symbols berbasis file
  std::vector<std::string> getDBSymbols();                                            // Subscribe berbasis DB


public:
  WsClient();
  ~WsClient();

  void start(const std::string& userId, const std::string& wsKeyUrl);
  void stop();
  bool isConnected() const;

  // Metode untuk menghubungkan dengan plugin
  void setAmiBrokerWindow(HWND hWnd, std::atomic<int>* pStatus);
};

#endif // WS_CLIENT_H