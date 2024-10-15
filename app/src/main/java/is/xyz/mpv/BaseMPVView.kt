package `is`.xyz.mpv

import android.content.Context
import android.util.AttributeSet
import android.util.Log
import android.view.SurfaceHolder
import android.view.SurfaceView
import com.lingoplay.module.mpv.FileMPVDataSource
import com.lingoplay.module.mpv.MPVLib

// Contains only the essential code needed to get a picture on the screen

abstract class BaseMPVView(context: Context, attrs: AttributeSet) : SurfaceView(context, attrs), SurfaceHolder.Callback {
    lateinit var mpvLib: MPVLib

    /**
     * Initialize libmpv.
     *
     * Call this once before the view is shown.
     */
    fun initialize(configDir: String, cacheDir: String) {
        MPVLib.setDataSourceFactory(FileMPVDataSource.Factory())
        mpvLib = MPVLib(context)

        /* set normal options (user-supplied config can override) */
        mpvLib.setOptionString("config", "yes")
        mpvLib.setOptionString("config-dir", configDir)
        for (opt in arrayOf("gpu-shader-cache-dir", "icc-cache-dir"))
            mpvLib.setOptionString(opt, cacheDir)
        initOptions()

        mpvLib.init()

        /* set hardcoded options */
        postInitOptions()
        // would crash before the surface is attached
        mpvLib.setOptionString("force-window", "no")
        // need to idle at least once for playFile() logic to work
        mpvLib.setOptionString("idle", "once")

        mpvLib.setOptionString("seekbarkeyframes", "no")

        holder.addCallback(this)
        observeProperties()
    }

    /**
     * Deinitialize libmpv.
     *
     * Call this once before the view is destroyed.
     */
    fun destroy() {
        // Disable surface callbacks to avoid using unintialized mpv state
        holder.removeCallback(this)
        Log.d("MPVView", "mpvLib.destroy")
        mpvLib.destroy()
    }

    protected abstract fun initOptions()
    protected abstract fun postInitOptions()

    protected abstract fun observeProperties()

    private var filePath: String? = null

    /**
     * Set the first file to be played once the player is ready.
     */
    fun playFile(filePath: String) {
        this.filePath = filePath
    }

    private var voInUse: String = "gpu"

    /**
     * Sets the VO to use.
     * It is automatically disabled/enabled when the surface dis-/appears.
     */
    fun setVo(vo: String) {
        voInUse = vo
        mpvLib.setOptionString("vo", vo)
    }

    // Surface callbacks

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        mpvLib.setPropertyString("android-surface-size", "${width}x$height")
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        Log.w(TAG, "attaching surface")
        mpvLib.attachSurface(holder.surface)
        // This forces mpv to render subs/osd/whatever into our surface even if it would ordinarily not
        mpvLib.setOptionString("force-window", "yes")

        if (filePath != null) {
            mpvLib.command(arrayOf("loadfile", "datasource://" + (filePath as String)))
//            mpvLib.command(arrayOf("loadfile", filePath as String))
            filePath = null
        } else {
            // We disable video output when the context disappears, enable it back
            mpvLib.setPropertyString("vo", voInUse)
        }
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        Log.w(TAG, "detaching surface")
        mpvLib.setPropertyString("vo", "null")
        mpvLib.setOptionString("force-window", "no")
        mpvLib.detachSurface()
        // FIXME: race condition here because detachSurface just sets a property and that is async
    }

    companion object {
        private const val TAG = "BaseMPVView"
    }
}
