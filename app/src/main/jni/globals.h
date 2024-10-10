#pragma once

#include <atomic>

extern mpv_handle *g_mpv;
extern std::atomic<bool> g_event_thread_request_exit;
