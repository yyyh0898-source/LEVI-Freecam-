package dev.freecam.levi

import com.google.gson.Gson
import com.google.gson.GsonBuilder
import com.google.gson.JsonObject
import com.google.gson.JsonParser
import com.google.gson.stream.JsonWriter
import java.io.File
import java.io.IOException

/**
 * Reads and writes the Freecam mod's `config/config.json` inside an installed
 * mod directory (for example `<launcher mods dir>/Freecam`).
 *
 * Edits made here are picked up the next time the game starts. Fields this class
 * does not know about are kept when saving.
 */
class FreecamConfigStore(private val modDirectory: File) {

    /** `<modDirectory>/config/config.json` */
    val configFile: File
        get() = File(File(modDirectory, CONFIG_DIR), CONFIG_FILE)

    fun exists(): Boolean = configFile.isFile

    /**
     * Returns the stored settings. A missing, unreadable or corrupt file, or a
     * field of the wrong type, yields the default for that value; this never
     * throws because of file content.
     */
    fun load(): FreecamSettings {
        val root = readRoot() ?: return FreecamSettings()

        val showNameTags = root.get(KEY_SHOW_NAME_TAGS)
            ?.takeIf { it.isJsonPrimitive && it.asJsonPrimitive.isBoolean }
            ?.asBoolean
            ?: FreecamSettings.DEFAULT_SHOW_NAME_TAGS
        val moveSpeed = root.get(KEY_MOVE_SPEED)
            ?.takeIf { it.isJsonPrimitive && it.asJsonPrimitive.isNumber }
            ?.asDouble
            ?: FreecamSettings.DEFAULT_MOVE_SPEED

        return FreecamSettings(showNameTags, moveSpeed).normalized()
    }

    /**
     * Writes [settings] (clamped to the supported range) to config.json,
     * creating the config directory if needed.
     *
     * @throws IOException if the file cannot be written.
     */
    @Throws(IOException::class)
    fun save(settings: FreecamSettings) {
        val normalized = settings.normalized()

        val root = readRoot() ?: JsonObject()
        root.addProperty(KEY_VERSION, FreecamSettings.CONFIG_VERSION)
        root.addProperty(KEY_SHOW_NAME_TAGS, normalized.showNameTags)
        root.addProperty(KEY_MOVE_SPEED, normalized.moveSpeed)
        writeAtomically(root)
    }

    private fun readRoot(): JsonObject? {
        val file = configFile
        if (!file.isFile) return null
        return try {
            val element = file.reader(Charsets.UTF_8).use { JsonParser.parseReader(it) }
            if (element.isJsonObject) element.asJsonObject else null
        } catch (_: Exception) {
            // Corrupt or unreadable: callers fall back to defaults / start a fresh file.
            null
        }
    }

    private fun writeAtomically(root: JsonObject) {
        val target = configFile
        val parent = target.parentFile ?: throw IOException("No parent directory for $target")
        if (!parent.isDirectory && !parent.mkdirs() && !parent.isDirectory) {
            throw IOException("Cannot create directory $parent")
        }

        val temp = File(parent, target.name + ".tmp")
        try {
            temp.bufferedWriter(Charsets.UTF_8).use { out ->
                val writer = JsonWriter(out)
                writer.setIndent(INDENT)
                GSON.toJson(root, writer)
                writer.flush()
                out.write("\n")
            }
            if (!temp.renameTo(target)) {
                // Some file systems refuse to rename over an existing file.
                if (target.exists() && !target.delete()) {
                    throw IOException("Cannot replace $target")
                }
                if (!temp.renameTo(target)) {
                    throw IOException("Cannot move $temp to $target")
                }
            }
        } finally {
            if (temp.exists()) temp.delete()
        }
    }

    private companion object {
        const val CONFIG_DIR = "config"
        const val CONFIG_FILE = "config.json"
        const val KEY_VERSION = "version"
        const val KEY_SHOW_NAME_TAGS = "showNameTags"
        const val KEY_MOVE_SPEED = "moveSpeed"
        const val INDENT = "    "

        val GSON: Gson = GsonBuilder().disableHtmlEscaping().create()
    }
}
