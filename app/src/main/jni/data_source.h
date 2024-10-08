#pragma once

#include <mpv/stream_cb.h>

int mpv_open_data_source_fn(void *user_data, char *uri,
                        mpv_stream_cb_info *info);