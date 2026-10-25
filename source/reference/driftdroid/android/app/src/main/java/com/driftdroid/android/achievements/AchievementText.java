package com.driftdroid.android.achievements;

import android.graphics.*;

/**
 * Text for achievement surfaces. The app supplies the launcher's own outlined menu font
 * (ThorAchievementText); [Plain] is the readable fallback when that art is unavailable.
 */
public interface AchievementText {
    /** [y] is the top of the line; [height] its full line height. */
    void draw(Canvas canvas, String value, float x, float y, float height, float maxWidth, boolean center);
    float width(String value, float height);

    final class Plain implements AchievementText {
        private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG), outline = new Paint(Paint.ANTI_ALIAS_FLAG);
        public Plain() {
            fill.setTypeface(Typeface.DEFAULT_BOLD); fill.setColor(Color.WHITE);
            outline.setTypeface(Typeface.DEFAULT_BOLD); outline.setColor(0xff101820);
            outline.setStyle(Paint.Style.STROKE); outline.setStrokeJoin(Paint.Join.ROUND);
        }
        @Override public void draw(Canvas canvas, String value, float x, float y, float height, float maxWidth, boolean center) {
            float size = height * 0.72f;
            fill.setTextSize(size);
            float width = fill.measureText(value);
            if (width > maxWidth && width > 0) { size *= maxWidth / width; fill.setTextSize(size); width = maxWidth; }
            outline.setTextSize(size); outline.setStrokeWidth(size * 0.16f);
            float left = center ? x - width / 2f : x, base = y + height * 0.76f;
            canvas.drawText(value, left, base, outline);
            canvas.drawText(value, left, base, fill);
        }
        @Override public float width(String value, float height) {
            fill.setTextSize(height * 0.72f);
            return fill.measureText(value);
        }
    }

    /** Greedy word wrap to [maxWidth] at [height]; a word too long for one line is kept whole. */
    static java.util.List<String> wrap(AchievementText text, String value, float height, float maxWidth) {
        java.util.List<String> lines = new java.util.ArrayList<>();
        StringBuilder line = new StringBuilder();
        for (String word : value.trim().split("\\s+")) {
            if (word.isEmpty()) continue;
            String candidate = line.length() == 0 ? word : line + " " + word;
            if (line.length() > 0 && text.width(candidate, height) > maxWidth) { lines.add(line.toString()); line = new StringBuilder(word); }
            else { line.setLength(0); line.append(candidate); }
        }
        if (line.length() > 0) lines.add(line.toString());
        return lines;
    }
}
