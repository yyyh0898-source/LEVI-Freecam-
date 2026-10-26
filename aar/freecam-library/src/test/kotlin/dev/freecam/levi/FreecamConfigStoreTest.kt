package dev.freecam.levi

import com.google.gson.JsonObject
import com.google.gson.JsonParser
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File
import java.io.IOException

class FreecamConfigStoreTest {

    @get:Rule
    val folder = TemporaryFolder()

    private fun store(): FreecamConfigStore = FreecamConfigStore(folder.root)

    private fun writeConfig(text: String) {
        val file = File(folder.root, "config/config.json")
        file.parentFile!!.mkdirs()
        file.writeText(text)
    }

    private fun readConfig(): JsonObject =
        JsonParser.parseString(File(folder.root, "config/config.json").readText()).asJsonObject

    @Test
    fun configFileLivesInConfigFolderOfTheModDirectory() {
        assertEquals(File(folder.root, "config/config.json"), store().configFile)
        assertFalse(store().exists())
    }

    @Test
    fun missingFileGivesDefaults() {
        assertEquals(FreecamSettings(), store().load())
        assertEquals(10.0, store().load().moveSpeed, 0.0)
        assertTrue(store().load().showNameTags)
    }

    @Test
    fun loadsStoredValues() {
        writeConfig("""{"version": 1, "showNameTags": false, "moveSpeed": 25.5}""")
        assertEquals(FreecamSettings(showNameTags = false, moveSpeed = 25.5), store().load())
    }

    @Test
    fun integerSpeedIsAccepted() {
        writeConfig("""{"version": 1, "showNameTags": true, "moveSpeed": 20}""")
        assertEquals(20.0, store().load().moveSpeed, 0.0)
    }

    @Test
    fun loadClampsOutOfRangeSpeed() {
        writeConfig("""{"moveSpeed": 9999}""")
        assertEquals(FreecamSettings.MAX_MOVE_SPEED, store().load().moveSpeed, 0.0)
        writeConfig("""{"moveSpeed": -3}""")
        assertEquals(FreecamSettings.MIN_MOVE_SPEED, store().load().moveSpeed, 0.0)
    }

    @Test
    fun wrongTypesFallBackPerField() {
        writeConfig("""{"showNameTags": "yes", "moveSpeed": "fast"}""")
        assertEquals(FreecamSettings(), store().load())

        writeConfig("""{"showNameTags": false, "moveSpeed": "fast"}""")
        assertEquals(FreecamSettings(showNameTags = false), store().load())
    }

    @Test
    fun corruptOrUnexpectedFilesGiveDefaults() {
        writeConfig("{ not json")
        assertEquals(FreecamSettings(), store().load())
        writeConfig("[1, 2, 3]")
        assertEquals(FreecamSettings(), store().load())
        writeConfig("")
        assertEquals(FreecamSettings(), store().load())
    }

    @Test
    fun saveCreatesFolderAndWritesTheNativeFieldNames() {
        store().save(FreecamSettings(showNameTags = false, moveSpeed = 7.5))

        val json = readConfig()
        assertEquals(1, json.get("version").asInt)
        assertFalse(json.get("showNameTags").asBoolean)
        assertEquals(7.5, json.get("moveSpeed").asDouble, 0.0)
        assertTrue(File(folder.root, "config/config.json").readText().endsWith("\n"))
    }

    @Test
    fun saveRoundTrips() {
        val settings = FreecamSettings(showNameTags = false, moveSpeed = 33.25)
        store().save(settings)
        assertEquals(settings, store().load())
        assertTrue(store().exists())
    }

    @Test
    fun saveClampsAndRepairsBadSpeeds() {
        store().save(FreecamSettings(moveSpeed = 1000.0))
        assertEquals(50.0, readConfig().get("moveSpeed").asDouble, 0.0)

        store().save(FreecamSettings(moveSpeed = Double.NaN))
        assertEquals(10.0, readConfig().get("moveSpeed").asDouble, 0.0)

        store().save(FreecamSettings(moveSpeed = Double.NEGATIVE_INFINITY))
        assertEquals(10.0, readConfig().get("moveSpeed").asDouble, 0.0)
    }

    @Test
    fun saveKeepsUnknownFieldsAndFixesTheVersion() {
        writeConfig("""{"version": 7, "extra": {"a": 1}, "showNameTags": true, "moveSpeed": 10}""")
        store().save(FreecamSettings(showNameTags = false, moveSpeed = 12.0))

        val json = readConfig()
        assertEquals(1, json.get("version").asInt)
        assertEquals(1, json.getAsJsonObject("extra").get("a").asInt)
        assertFalse(json.get("showNameTags").asBoolean)
        assertEquals(12.0, json.get("moveSpeed").asDouble, 0.0)
    }

    @Test
    fun saveReplacesACorruptFile() {
        writeConfig("{ not json")
        store().save(FreecamSettings(moveSpeed = 15.0))
        assertEquals(15.0, store().load().moveSpeed, 0.0)
    }

    @Test
    fun saveLeavesNoTemporaryFileBehind() {
        store().save(FreecamSettings())
        store().save(FreecamSettings(moveSpeed = 20.0))
        val names = File(folder.root, "config").list()!!.sorted()
        assertEquals(listOf("config.json"), names)
    }

    @Test
    fun failedSaveThrowsAndCleansUpItsTemporaryFile() {
        // A non-empty folder where config.json belongs makes the final move impossible.
        val blocker = File(folder.root, "config/config.json")
        val keep = File(blocker, "keep.txt")
        keep.parentFile!!.mkdirs()
        keep.writeText("x")

        try {
            store().save(FreecamSettings())
            fail("expected an IOException")
        } catch (expected: IOException) {
            // expected
        }

        assertEquals(listOf("config.json"), File(folder.root, "config").list()!!.sorted())
        assertTrue(keep.isFile)
    }

    @Test
    fun settingsNormalizeAndClamp() {
        assertEquals(0.5, FreecamSettings.clampMoveSpeed(0.0), 0.0)
        assertEquals(50.0, FreecamSettings.clampMoveSpeed(51.0), 0.0)
        assertEquals(7.0, FreecamSettings.clampMoveSpeed(7.0), 0.0)
        assertEquals(10.0, FreecamSettings.clampMoveSpeed(Double.NaN), 0.0)
        assertEquals(FreecamSettings(moveSpeed = 50.0), FreecamSettings(moveSpeed = 80.0).normalized())
    }
}
