package `is`.xyz.mpv

import android.content.Context
import android.os.Build
import android.os.Environment
import android.preference.PreferenceManager
import android.util.AttributeSet
import android.util.Log
import android.view.*
import androidx.core.content.ContextCompat
import com.lingoplay.module.mpv.MPVLib
import com.lingoplay.module.mpv.MPVLib.mpvFormat.*
import kotlin.reflect.KProperty

internal class MPVView(context: Context, attrs: AttributeSet) : BaseMPVView(context, attrs) {
    override fun initOptions() {
        val sharedPreferences = PreferenceManager.getDefaultSharedPreferences(context)

        // apply phone-optimized defaults
        mpvLib.setOptionString("profile", "fast")

        // vo
        setVo(if (sharedPreferences.getBoolean("gpu_next", false))
            "gpu-next"
        else
            "gpu")

        // hwdec
        val hwdec = if (sharedPreferences.getBoolean("hardware_decoding", true))
            "auto"
        else
            "no"

        // vo: set display fps as reported by android
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            val disp = ContextCompat.getDisplayOrDefault(context)
            val refreshRate = disp.mode.refreshRate

            Log.v(TAG, "Display ${disp.displayId} reports FPS of $refreshRate")
            mpvLib.setOptionString("display-fps-override", refreshRate.toString())
        } else {
            Log.v(TAG, "Android version too old, disabling refresh rate functionality " +
                       "(${Build.VERSION.SDK_INT} < ${Build.VERSION_CODES.M})")
        }

        // set non-complex options
        data class Property(val preference_name: String, val mpv_option: String)

        val opts = arrayOf(
                Property("default_audio_language", "alang"),
                Property("default_subtitle_language", "slang"),

                // vo-related
                Property("video_scale", "scale"),
                Property("video_scale_param1", "scale-param1"),
                Property("video_scale_param2", "scale-param2"),

                Property("video_downscale", "dscale"),
                Property("video_downscale_param1", "dscale-param1"),
                Property("video_downscale_param2", "dscale-param2"),

                Property("video_tscale", "tscale"),
                Property("video_tscale_param1", "tscale-param1"),
                Property("video_tscale_param2", "tscale-param2")
        )

        for ((preference_name, mpv_option) in opts) {
            val preference = sharedPreferences.getString(preference_name, "")
            if (!preference.isNullOrBlank())
                mpvLib.setOptionString(mpv_option, preference)
        }

        val debandMode = sharedPreferences.getString("video_debanding", "")
        if (debandMode == "gradfun") {
            // lower the default radius (16) to improve performance
            mpvLib.setOptionString("vf", "gradfun=radius=12")
        } else if (debandMode == "gpu") {
            mpvLib.setOptionString("deband", "yes")
        }

        val vidsync = sharedPreferences.getString("video_sync", resources.getString(R.string.pref_video_interpolation_sync_default))
        mpvLib.setOptionString("video-sync", vidsync!!)

        if (sharedPreferences.getBoolean("video_interpolation", false))
            mpvLib.setOptionString("interpolation", "yes")

        if (sharedPreferences.getBoolean("gpudebug", false))
            mpvLib.setOptionString("gpu-debug", "yes")

        if (sharedPreferences.getBoolean("video_fastdecode", false)) {
            mpvLib.setOptionString("vd-lavc-fast", "yes")
            mpvLib.setOptionString("vd-lavc-skiploopfilter", "nonkey")
        }

        mpvLib.setOptionString("gpu-context", "android")
        mpvLib.setOptionString("opengl-es", "yes")
        mpvLib.setOptionString("hwdec", hwdec)
        mpvLib.setOptionString("hwdec-codecs", "h264,hevc,mpeg4,mpeg2video,vp8,vp9,av1")
        mpvLib.setOptionString("ao", "audiotrack,opensles")
        mpvLib.setOptionString("tls-verify", "yes")
        mpvLib.setOptionString("tls-ca-file", "${this.context.filesDir.path}/cacert.pem")
        mpvLib.setOptionString("input-default-bindings", "yes")
        // Limit demuxer cache since the defaults are too high for mobile devices
        val cacheMegs = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O_MR1) 64 else 32
        mpvLib.setOptionString("demuxer-max-bytes", "${cacheMegs * 1024 * 1024}")
        mpvLib.setOptionString("demuxer-max-back-bytes", "${cacheMegs * 1024 * 1024}")
        //
        val screenshotDir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_PICTURES)
        screenshotDir.mkdirs()
        mpvLib.setOptionString("screenshot-directory", screenshotDir.path)
        // workaround for <https://github.com/mpv-player/mpv/issues/14651>
        mpvLib.setOptionString("vd-lavc-film-grain", "cpu")
    }

    override fun postInitOptions() {
        // we need to call write-watch-later manually
        mpvLib.setOptionString("save-position-on-quit", "no")
    }

    fun onPointerEvent(event: MotionEvent): Boolean {
        assert (event.isFromSource(InputDevice.SOURCE_CLASS_POINTER))
        if (event.actionMasked == MotionEvent.ACTION_SCROLL) {
            val h = event.getAxisValue(MotionEvent.AXIS_HSCROLL)
            val v = event.getAxisValue(MotionEvent.AXIS_VSCROLL)
            if (h > 0)
                mpvLib.command(arrayOf("keypress", "WHEEL_RIGHT", "$h"))
            else if (h < 0)
                mpvLib.command(arrayOf("keypress", "WHEEL_LEFT", "${-h}"))
            if (v > 0)
                mpvLib.command(arrayOf("keypress", "WHEEL_UP", "$v"))
            else if (v < 0)
                mpvLib.command(arrayOf("keypress", "WHEEL_DOWN", "${-v}"))
            return true
        }
        return false
    }

    fun onKey(event: KeyEvent): Boolean {
        if (event.action == KeyEvent.ACTION_MULTIPLE)
            return false
        if (KeyEvent.isModifierKey(event.keyCode))
            return false

        var mapped = KeyMapping.map.get(event.keyCode)
        if (mapped == null) {
            // Fallback to produced glyph
            if (!event.isPrintingKey) {
                if (event.repeatCount == 0)
                    Log.d(TAG, "Unmapped non-printable key ${event.keyCode}")
                return false
            }

            val ch = event.unicodeChar
            if (ch.and(KeyCharacterMap.COMBINING_ACCENT) != 0)
                return false // dead key
            mapped = ch.toChar().toString()
        }

        if (event.repeatCount > 0)
            return true // eat event but ignore it, mpv has its own key repeat

        val mod: MutableList<String> = mutableListOf()
        event.isShiftPressed && mod.add("shift")
        event.isCtrlPressed && mod.add("ctrl")
        event.isAltPressed && mod.add("alt")
        event.isMetaPressed && mod.add("meta")

        val action = if (event.action == KeyEvent.ACTION_DOWN) "keydown" else "keyup"
        mod.add(mapped)
        mpvLib.command(arrayOf(action, mod.joinToString("+")))

        return true
    }

    override fun observeProperties() {
        // This observes all properties needed by MPVView, MPVActivity or other classes
        data class Property(val name: String, val format: Int = MPV_FORMAT_NONE)
        val p = arrayOf(
            Property("time-pos", MPV_FORMAT_DOUBLE),
            Property("duration/full", MPV_FORMAT_DOUBLE),
            Property("pause", MPV_FORMAT_FLAG),
            Property("paused-for-cache", MPV_FORMAT_FLAG),
            Property("speed", MPV_FORMAT_STRING),
            Property("track-list"),
            Property("video-params/aspect", MPV_FORMAT_DOUBLE),
            Property("video-params/rotate", MPV_FORMAT_DOUBLE),
            Property("playlist-pos", MPV_FORMAT_INT64),
            Property("playlist-count", MPV_FORMAT_INT64),
            Property("video-format"),
            Property("media-title", MPV_FORMAT_STRING),
            Property("metadata"),
            Property("loop-playlist"),
            Property("loop-file"),
            Property("shuffle", MPV_FORMAT_FLAG),
            Property("hwdec-current")
        )

        for ((name, format) in p)
            mpvLib.observeProperty(name, format, 0)
    }

    fun addObserver(o: MPVLib.EventObserver) {
        mpvLib.addObserver(o)
    }
    fun removeObserver(o: MPVLib.EventObserver) {
        mpvLib.removeObserver(o)
    }

    data class Track(val mpvId: Int, val name: String)
    var tracks = mapOf<String, MutableList<Track>>(
            "audio" to arrayListOf(),
            "video" to arrayListOf(),
            "sub" to arrayListOf())

    fun loadTracks() {
        for (list in tracks.values) {
            list.clear()
            // pseudo-track to allow disabling audio/subs
            list.add(Track(-1, context.getString(R.string.track_off)))
        }
        val count = mpvLib.getPropertyInt("track-list/count")!!
        // Note that because events are async, properties might disappear at any moment
        // so use ?: continue instead of !!
        for (i in 0 until count) {
            val type = mpvLib.getPropertyString("track-list/$i/type") ?: continue
            if (!tracks.containsKey(type)) {
                Log.w(TAG, "Got unknown track type: $type")
                continue
            }
            val mpvId = mpvLib.getPropertyInt("track-list/$i/id") ?: continue
            val lang = mpvLib.getPropertyString("track-list/$i/lang")
            val title = mpvLib.getPropertyString("track-list/$i/title")

            val trackName = if (!lang.isNullOrEmpty() && !title.isNullOrEmpty())
                context.getString(R.string.ui_track_title_lang, mpvId, title, lang)
            else if (!lang.isNullOrEmpty() || !title.isNullOrEmpty())
                context.getString(R.string.ui_track_text, mpvId, (lang ?: "") + (title ?: ""))
            else
                context.getString(R.string.ui_track, mpvId)
            tracks.getValue(type).add(Track(
                    mpvId=mpvId,
                    name=trackName
            ))
        }
    }

    data class PlaylistItem(val index: Int, val filename: String, val title: String?)

    fun loadPlaylist(): MutableList<PlaylistItem> {
        val playlist = mutableListOf<PlaylistItem>()
        val count = mpvLib.getPropertyInt("playlist-count")!!
        for (i in 0 until count) {
            val filename = mpvLib.getPropertyString("playlist/$i/filename")!!
            val title = mpvLib.getPropertyString("playlist/$i/title")
            playlist.add(PlaylistItem(index=i, filename=filename, title=title))
        }
        return playlist
    }

    data class Chapter(val index: Int, val title: String?, val time: Double)

    fun loadChapters(): MutableList<Chapter> {
        val chapters = mutableListOf<Chapter>()
        val count = mpvLib.getPropertyInt("chapter-list/count")!!
        for (i in 0 until count) {
            val title = mpvLib.getPropertyString("chapter-list/$i/title")
            val time = mpvLib.getPropertyDouble("chapter-list/$i/time")!!
            chapters.add(Chapter(
                    index=i,
                    title=title,
                    time=time
            ))
        }
        return chapters
    }

    // Property getters/setters

    var paused: Boolean?
        get() = mpvLib.getPropertyBoolean("pause")
        set(paused) = mpvLib.setPropertyBoolean("pause", paused!!)

    var timePos: Double?
        get() = mpvLib.getPropertyDouble("time-pos/full")
        set(progress) {
            Log.d("MPVView", "trigger seek")
            mpvLib.command(arrayOf("seek", progress.toString(), "exact+absolute"))
        }

    /** name of currently active hardware decoder or "no" */
    val hwdecActive: String
        get() = mpvLib.getPropertyString("hwdec-current") ?: "no"

    var playbackSpeed: Double?
        get() = mpvLib.getPropertyDouble("speed")
        set(speed) = mpvLib.setPropertyDouble("speed", speed!!)

    var subDelay: Double?
        get() = mpvLib.getPropertyDouble("sub-delay")
        set(speed) = mpvLib.setPropertyDouble("sub-delay", speed!!)

    var secondarySubDelay: Double?
        get() = mpvLib.getPropertyDouble("secondary-sub-delay")
        set(speed) = mpvLib.setPropertyDouble("secondary-sub-delay", speed!!)

    val estimatedVfFps: Double?
        get() = mpvLib.getPropertyDouble("estimated-vf-fps")

    /**
     * Returns the video aspect ratio. Rotation is taken into account.
     */
    fun getVideoAspect(): Double? {
        return mpvLib.getPropertyDouble("video-params/aspect")?.let {
            if (it < 0.001)
                return 0.0
            val rot = mpvLib.getPropertyInt("video-params/rotate") ?: 0
            if (rot % 180 == 90)
                1.0 / it
            else
                it
        }
    }

    inner class TrackDelegate(private val name: String) {
        operator fun getValue(thisRef: Any?, property: KProperty<*>): Int {
            val v = mpvLib.getPropertyString(name)
            // we can get null here for "no" or other invalid value
            return v?.toIntOrNull() ?: -1
        }
        operator fun setValue(thisRef: Any?, property: KProperty<*>, value: Int) {
            if (value == -1)
                mpvLib.setPropertyString(name, "no")
            else
                mpvLib.setPropertyInt(name, value)
        }
    }

    var vid: Int by TrackDelegate("vid")
    var sid: Int by TrackDelegate("sid")
    var secondarySid: Int by TrackDelegate("secondary-sid")
    var aid: Int by TrackDelegate("aid")

    // Commands

    fun cyclePause() = mpvLib.command(arrayOf("cycle", "pause"))
    fun cycleAudio() = mpvLib.command(arrayOf("cycle", "audio"))
    fun cycleSub() = mpvLib.command(arrayOf("cycle", "sub"))
    fun cycleHwdec() = mpvLib.command(arrayOf("cycle-values", "hwdec", "auto", "no"))

    fun cycleSpeed() {
        val speeds = arrayOf(0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0)
        val currentSpeed = playbackSpeed ?: 1.0
        val index = speeds.indexOfFirst { it > currentSpeed }
        playbackSpeed = speeds[if (index == -1) 0 else index]
    }

    fun getRepeat(): Int {
        return when (mpvLib.getPropertyString("loop-playlist") +
                mpvLib.getPropertyString("loop-file")) {
            "noinf" -> 2
            "infno" -> 1
            else -> 0
        }
    }

    fun cycleRepeat() {
        val state = getRepeat()
        when (state) {
            0, 1 -> {
                mpvLib.setPropertyString("loop-playlist", if (state == 1) "no" else "inf")
                mpvLib.setPropertyString("loop-file", if (state == 1) "inf" else "no")
            }
            2 -> mpvLib.setPropertyString("loop-file", "no")
        }
    }

    fun getShuffle(): Boolean {
        return mpvLib.getPropertyBoolean("shuffle")
    }

    fun changeShuffle(cycle: Boolean, value: Boolean = true) {
        // Use the 'shuffle' property to store the shuffled state, since changing
        // it at runtime doesn't do anything.
        val state = getShuffle()
        val newState = if (cycle) state.xor(value) else value
        if (state == newState)
            return
        mpvLib.command(arrayOf(if (newState) "playlist-shuffle" else "playlist-unshuffle"))
        mpvLib.setPropertyBoolean("shuffle", newState)
    }

    companion object {
        private const val TAG = "MPVView"
    }
}
