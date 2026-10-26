package dev.freecam.levi

import android.content.Context
import java.io.File
import java.io.IOException

/**
 * Helpers around the prebuilt `freecam.levipack` that the library build can embed
 * as an asset, so an app can hand the pack to LeviLauncher without shipping a
 * second file.
 */
object FreecamPackage {

    /**
     * Folder name LeviLauncher normally gives the installed mod: the `name` in the
     * pack's manifest.json (unless it was renamed during import). Use it to locate
     * the mod directory for [FreecamConfigStore].
     */
    const val MOD_ID = "Freecam"

    /** Asset name the build embeds the pack under. */
    const val LEVIPACK_ASSET = "freecam.levipack"

    /** True when this build of the library contains the pack asset. */
    @JvmStatic
    fun isLevipackBundled(context: Context): Boolean =
        try {
            context.assets.open(LEVIPACK_ASSET).close()
            true
        } catch (_: IOException) {
            false
        }

    /**
     * Copies the bundled pack to [destination] (parent folders are created) and
     * returns it. Open the result with LeviLauncher, or import it from its Mods
     * screen.
     *
     * @throws IOException if the pack is not bundled or the copy fails.
     */
    @JvmStatic
    @Throws(IOException::class)
    fun exportLevipack(context: Context, destination: File): File {
        destination.parentFile?.let { parent ->
            if (!parent.isDirectory && !parent.mkdirs() && !parent.isDirectory) {
                throw IOException("Cannot create directory $parent")
            }
        }
        context.assets.open(LEVIPACK_ASSET).use { input ->
            destination.outputStream().use { output -> input.copyTo(output) }
        }
        return destination
    }
}
