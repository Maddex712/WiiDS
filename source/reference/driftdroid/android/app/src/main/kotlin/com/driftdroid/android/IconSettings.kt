package com.driftdroid.android

import android.app.Activity
import android.app.AlertDialog
import android.content.Intent
import android.content.pm.ShortcutInfo
import android.content.pm.ShortcutManager
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Rect
import android.graphics.drawable.Icon
import android.net.Uri
import android.widget.Toast

/**
 * Custom home-screen icon, from a picture in the user's gallery.
 *
 * Android has no way for an app to change its own launcher icon to an arbitrary image - launcher
 * icons must be compiled into the APK. What it does allow is a pinned shortcut with any bitmap,
 * so that is what this is: a second home-screen entry that opens the same launcher screen, whose
 * picture can be switched between the user's image and the normal icon at any time. The real app
 * icon in the app drawer always stays the normal one.
 */
object IconSettings {
    const val REQUEST_CODE_PICK_IMAGE = 0x49434F4E // "ICON"

    private const val SHORTCUT_ID = "custom_icon"

    // Adaptive icons are 108dp with only the inner 72dp guaranteed visible after the launcher's
    // mask. 432px is 108dp at xxxhdpi, the largest density a launcher will ask for.
    private const val ICON_PX = 432
    private const val SAFE_FRACTION = 72f / 108f

    private fun shortcutManager(activity: Activity) = activity.getSystemService(ShortcutManager::class.java)

    private fun isPinned(activity: Activity): Boolean =
        shortcutManager(activity)?.pinnedShortcuts?.any { it.id == SHORTCUT_ID } == true

    fun show(activity: Activity) {
        val pinned = isPinned(activity)
        val message =
            if (pinned) {
                "Your custom shortcut is on the home screen. Change its picture, or switch it back " +
                    "to the normal icon."
            } else {
                "Pick a picture from your gallery and DriftDroid will add a home-screen shortcut " +
                    "with it. Android doesn't let apps replace their own icon, so the normal icon " +
                    "stays in the app drawer."
            }
        val builder =
            ThorDialogs.builder(activity)
                .setTitle("App icon")
                .setMessage(message)
                .setPositiveButton("Pick a picture") { _, _ -> pickImage(activity) }
                .setNegativeButton("Cancel", null)
        if (pinned) {
            builder.setNeutralButton("Use normal icon") { _, _ -> useNormalIcon(activity) }
        }
        builder.show()
    }

    private fun pickImage(activity: Activity) {
        val intent =
            Intent(Intent.ACTION_GET_CONTENT).apply {
                type = "image/*"
                addCategory(Intent.CATEGORY_OPENABLE)
            }
        activity.startActivityForResult(Intent.createChooser(intent, "Pick a picture"), REQUEST_CODE_PICK_IMAGE)
    }

    /** Called from ModePickerActivity.onActivityResult. */
    fun onImagePicked(activity: Activity, uri: Uri?) {
        if (uri == null) {
            return
        }
        val bitmap =
            runCatching {
                activity.contentResolver.openInputStream(uri)?.use { decodeScaled(it.readBytes()) }
            }.getOrNull()
        if (bitmap == null) {
            Toast.makeText(activity, "Couldn't read that picture.", Toast.LENGTH_LONG).show()
            return
        }
        // The launcher stores the shortcut's bitmap itself, so nothing needs keeping on our side.
        applyIcon(activity, Icon.createWithAdaptiveBitmap(toAdaptiveBitmap(bitmap)))
    }

    private fun useNormalIcon(activity: Activity) {
        applyIcon(activity, Icon.createWithResource(activity, R.mipmap.ic_launcher))
    }

    private fun applyIcon(activity: Activity, icon: Icon) {
        val manager = shortcutManager(activity) ?: return
        val shortcut =
            ShortcutInfo.Builder(activity, SHORTCUT_ID)
                .setShortLabel(activity.getString(R.string.app_name))
                .setIcon(icon)
                .setIntent(Intent(activity, ModePickerActivity::class.java).setAction(Intent.ACTION_MAIN))
                .build()

        if (isPinned(activity)) {
            // Already on the home screen: swap its picture in place rather than adding another.
            manager.updateShortcuts(listOf(shortcut))
            Toast.makeText(activity, "Shortcut icon updated.", Toast.LENGTH_SHORT).show()
            return
        }
        if (!manager.isRequestPinShortcutSupported) {
            Toast.makeText(activity, "Your home screen doesn't support adding shortcuts.", Toast.LENGTH_LONG).show()
            return
        }
        // Android shows its own "Add to home screen?" confirmation for this.
        manager.requestPinShortcut(shortcut, null)
    }

    /** Decodes at no more than twice the icon size, so a 50-megapixel photo can't run the app out of memory. */
    private fun decodeScaled(bytes: ByteArray): Bitmap? {
        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        BitmapFactory.decodeByteArray(bytes, 0, bytes.size, bounds)
        var sample = 1
        while (minOf(bounds.outWidth, bounds.outHeight) / (sample * 2) >= ICON_PX * 2) {
            sample *= 2
        }
        return BitmapFactory.decodeByteArray(bytes, 0, bytes.size, BitmapFactory.Options().apply { inSampleSize = sample })
    }

    /**
     * Center-crops to a square and places it inside the adaptive icon's safe zone, filling the
     * margin with the picture's own corner colour. Drawing it full-bleed instead would let the
     * launcher's mask cut off the edges - the title across the top of most artwork, typically.
     */
    private fun toAdaptiveBitmap(source: Bitmap): Bitmap {
        val side = minOf(source.width, source.height)
        val crop = Rect((source.width - side) / 2, (source.height - side) / 2, 0, 0).apply {
            right = left + side
            bottom = top + side
        }
        val out = Bitmap.createBitmap(ICON_PX, ICON_PX, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(out)
        val corner = source.getPixel(crop.left, crop.top)
        canvas.drawColor(if (Color.alpha(corner) == 0) Color.WHITE else corner)
        val inner = (ICON_PX * SAFE_FRACTION).toInt()
        val offset = (ICON_PX - inner) / 2
        canvas.drawBitmap(
            source,
            crop,
            Rect(offset, offset, offset + inner, offset + inner),
            Paint(Paint.FILTER_BITMAP_FLAG or Paint.ANTI_ALIAS_FLAG),
        )
        return out
    }
}
