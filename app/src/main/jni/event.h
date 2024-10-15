#pragma once

void start_event_thread();
void stop_event_thread();
void event_enqueue_cb(void *d);
void destroy_events(mpv_lib *lib);
