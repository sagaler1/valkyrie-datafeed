#ifndef PLUGIN_STATE_H
#define PLUGIN_STATE_H

// Enum ini sekarang bisa diakses oleh semua file yang meng-include header ini
enum PluginState {
    STATE_IDLE,
    STATE_CONNECTING,
    STATE_CONNECTED,
    STATE_DISCONNECTED
};

#endif // PLUGIN_STATE_H
