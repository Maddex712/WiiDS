// Thor draw-time HUD placement. No pane state or animation control points change.
#ifndef PORT_THOR_HUD_LAYOUT_H
#define PORT_THOR_HUD_LAYOUT_H

static inline int port_thor_hud_side_padding(float wide)
{
	if (wide <= 1.0f) return 0;
	// Match sms_gx's even EFB width, including its rounding at 16:9 (854).
	const int width = (int(640.0f * wide + 1.0f) & ~1);
	return (width - 640) / 2;
}

static inline int port_thor_hud_lives_offset(float wide, int left, int contentLeft, bool edges)
{
	const int padding = edges ? 0 : port_thor_hud_side_padding(wide);
	// The MARIO lettering extends left of its 10-pixel container. Inset the
	// artwork, rather than the container's origin, by six native pixels.
	return 6 - contentLeft - padding - left;
}

static inline int port_thor_hud_life_offset(float wide, bool edges)
{
	// The stock damage animation remains unchanged; translate its entire path
	// into the widened right-side coordinate system from its first frame.
	return edges ? 0 : port_thor_hud_side_padding(wide);
}

static inline bool port_thor_hud_anchor_right(unsigned int tag, bool split)
{
	// '\0l_0', the authored life-meter root. Its moving centre cannot decide
	// which screen edge owns it while the stock Bezier animation is running.
	return split && tag == 0x006c5f30u;
}

#endif
