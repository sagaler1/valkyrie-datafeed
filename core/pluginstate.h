#ifndef PLUGIN_STATE_H
#define PLUGIN_STATE_H

// This enum is now accessible to all files that include this header
enum PluginState {
    STATE_IDLE,
    STATE_CONNECTING,
    STATE_CONNECTED,
    STATE_DISCONNECTED
};

#endif // PLUGIN_STATE_H
