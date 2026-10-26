package dev.freecam.levi

/**
 * Freecam settings as stored in the mod's `config/config.json`.
 *
 * Mirrors `mod/src/Settings.hpp`: same field names, same limits, same clamping,
 * so a value written here is read back unchanged by the native mod.
 */
data class FreecamSettings @JvmOverloads constructor(
    /** Draw entity name tags while the camera is detached. */
    val showNameTags: Boolean = DEFAULT_SHOW_NAME_TAGS,
    /** Camera speed in blocks per second, within [MIN_MOVE_SPEED]..[MAX_MOVE_SPEED]. */
    val moveSpeed: Double = DEFAULT_MOVE_SPEED,
) {
    /** Returns a copy with [moveSpeed] clamped the way the native mod clamps it. */
    fun normalized(): FreecamSettings = copy(moveSpeed = clampMoveSpeed(moveSpeed))

    companion object {
        const val MIN_MOVE_SPEED = 0.5
        const val MAX_MOVE_SPEED = 50.0
        const val DEFAULT_MOVE_SPEED = 10.0
        const val DEFAULT_SHOW_NAME_TAGS = true

        /** The `version` field written to config.json; the native mod rewrites files with another value. */
        const val CONFIG_VERSION = 1

        /** NaN/infinity fall back to the default, everything else is clamped into range. */
        @JvmStatic
        fun clampMoveSpeed(value: Double): Double =
            if (value.isFinite()) value.coerceIn(MIN_MOVE_SPEED, MAX_MOVE_SPEED) else DEFAULT_MOVE_SPEED
    }
}
