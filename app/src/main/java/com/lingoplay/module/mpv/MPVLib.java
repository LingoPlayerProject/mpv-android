package com.lingoplay.module.mpv;

// Wrapper for native library

import android.content.Context;
import android.graphics.Bitmap;
import android.util.Log;
import android.view.Surface;

import androidx.annotation.NonNull;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.concurrent.CopyOnWriteArrayList;

@SuppressWarnings("unused")
public class MPVLib {

    public static final String DATA_SOURCE_SCHEME = "datasource";

    static {
        String[] libs = {"mpv", "player"};
        for (String lib : libs) {
            System.loadLibrary(lib);
        }
    }

    private static MPVDataSource.Factory DATA_SOURCE_FACTORY = null;

    public static void setDataSourceFactory(MPVDataSource.Factory factory) {
        Objects.requireNonNull(factory);
        MPVLib.DATA_SOURCE_FACTORY = factory;
    }

    /**
     * Called from native
     */
    private static MPVDataSource openDataSource(String uri) throws IOException {
        if (DATA_SOURCE_FACTORY == null) {
            throw new RuntimeException("Call MPVLib.setFactory first!");
        }
        return DATA_SOURCE_FACTORY.open(uri);
    }

    public static native void create(Context appctx);

    public static native void init();

    public static native void destroy();

    public static native void attachSurface(Surface surface);

    public static native void detachSurface();

    public static native void command(@NonNull String[] cmd);

    public static native int setOptionString(@NonNull String name, @NonNull String value);

    public static native Bitmap grabThumbnail(int dimension);

    public static native Integer getPropertyInt(@NonNull String property);

    public static native void setPropertyInt(@NonNull String property, @NonNull Integer value);

    public static native Double getPropertyDouble(@NonNull String property);

    public static native void setPropertyDouble(@NonNull String property, @NonNull Double value);

    public static native Boolean getPropertyBoolean(@NonNull String property);

    public static native void setPropertyBoolean(@NonNull String property, @NonNull Boolean value);

    public static native String getPropertyString(@NonNull String property);

    public static native void setPropertyString(@NonNull String property, @NonNull String value);

    public static native void observeProperty(@NonNull String property, int format, long opaqueData);

    private static final List<EventObserver> observers = new CopyOnWriteArrayList<>();

    public static void addObserver(EventObserver o) {
        observers.add(o);
    }

    public static void removeObserver(EventObserver o) {
        observers.remove(o);
    }

    // Called from native
    private static void eventProperty(String property, int format, long opaqueData, long longVal, boolean boolVal,
                                      double doubleVal, String strVal) {
        for (EventObserver o : observers) {
            try {
                o.eventProperty(property, format, opaqueData, longVal, boolVal, doubleVal, strVal);
            } catch (Exception e) {
                Log.e("MPVLib", "eventProperty callback err", e);
            }
        }
    }

    // Called from native
    private static void event(int eventId, long opaqueData) {
        for (EventObserver o : observers) {
            try {
                o.event(eventId, opaqueData);
            } catch (Exception e) {
                Log.e("MPVLib", "event callback err", e);
            }
        }
    }

    private static final List<LogObserver> log_observers = new CopyOnWriteArrayList<>();

    public static void addLogObserver(LogObserver o) {
        log_observers.add(o);
    }

    public static void removeLogObserver(LogObserver o) {
        log_observers.remove(o);
    }

    public static void logMessage(String prefix, int level, String text) {
        for (LogObserver o : log_observers) {
            o.logMessage(prefix, level, text);
        }
    }

    public interface EventObserver {
        void eventProperty(String property, int format, long opaqueData, long longVal, boolean boolVal,
                           double doubleVal, String strVal);

        void event(int eventId, long opaqueData);
    }

    public interface LogObserver {
        void logMessage(@NonNull String prefix, int level, @NonNull String text);
    }

    public static class mpvFormat {
        public static final int MPV_FORMAT_NONE = 0;
        public static final int MPV_FORMAT_STRING = 1;
        public static final int MPV_FORMAT_OSD_STRING = 2;
        public static final int MPV_FORMAT_FLAG = 3;
        public static final int MPV_FORMAT_INT64 = 4;
        public static final int MPV_FORMAT_DOUBLE = 5;
        public static final int MPV_FORMAT_NODE = 6;
        public static final int MPV_FORMAT_NODE_ARRAY = 7;
        public static final int MPV_FORMAT_NODE_MAP = 8;
        public static final int MPV_FORMAT_BYTE_ARRAY = 9;
    }

    public static class mpvEventId {
        public static final int MPV_EVENT_NONE = 0;
        public static final int MPV_EVENT_SHUTDOWN = 1;
        public static final int MPV_EVENT_LOG_MESSAGE = 2;
        public static final int MPV_EVENT_GET_PROPERTY_REPLY = 3;
        public static final int MPV_EVENT_SET_PROPERTY_REPLY = 4;
        public static final int MPV_EVENT_COMMAND_REPLY = 5;
        public static final int MPV_EVENT_START_FILE = 6;
        public static final int MPV_EVENT_END_FILE = 7;
        public static final int MPV_EVENT_FILE_LOADED = 8;
        public static final @Deprecated int MPV_EVENT_IDLE = 11;
        public static final @Deprecated int MPV_EVENT_TICK = 14;
        public static final int MPV_EVENT_CLIENT_MESSAGE = 16;
        public static final int MPV_EVENT_VIDEO_RECONFIG = 17;
        public static final int MPV_EVENT_AUDIO_RECONFIG = 18;
        public static final int MPV_EVENT_SEEK = 20;
        public static final int MPV_EVENT_PLAYBACK_RESTART = 21;
        public static final int MPV_EVENT_PROPERTY_CHANGE = 22;
        public static final int MPV_EVENT_QUEUE_OVERFLOW = 24;
        public static final int MPV_EVENT_HOOK = 25;
    }

    public static class mpvLogLevel {
        public static final int MPV_LOG_LEVEL_NONE = 0;
        public static final int MPV_LOG_LEVEL_FATAL = 10;
        public static final int MPV_LOG_LEVEL_ERROR = 20;
        public static final int MPV_LOG_LEVEL_WARN = 30;
        public static final int MPV_LOG_LEVEL_INFO = 40;
        public static final int MPV_LOG_LEVEL_V = 50;
        public static final int MPV_LOG_LEVEL_DEBUG = 60;
        public static final int MPV_LOG_LEVEL_TRACE = 70;
    }
}
