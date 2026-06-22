#!/bin/bash
# detect-display.sh

# prefer environment variable
if [ -n "$XDG_SESSION_TYPE" ]; then
  echo "XDG_SESSION_TYPE=$XDG_SESSION_TYPE"
  exit 0
fi

# loginctl
if command -v loginctl >/dev/null 2>&1; then
  sid=$(loginctl --no-legend | awk -v u="$USER" '$0 ~ u {print $1; exit}')
  if [ -n "$sid" ]; then
    t=$(loginctl show-session "$sid" -p Type 2>/dev/null)
    if [ -n "$t" ]; then echo "$t"; exit 0; fi
  fi
fi

# check wayland socket
if ls /run/user/$(id -u)/wayland-* >/dev/null 2>&1; then
  echo "likely: wayland (found /run/user/$(id -u)/wayland-*)"
  exit 0
fi

# check DISPLAY
if [ -n "$DISPLAY" ]; then
  echo "DISPLAY=$DISPLAY (likely X11 or Xwayland)"
fi

# process checks
if ps -ef | egrep -q 'kwin_wayland|sway|weston|gnome-shell'; then
  echo "process indicates Wayland compositor running"
  exit 0
fi
if ps -ef | egrep -q 'Xorg|Xwayland'; then
  echo "process indicates X server (Xorg/Xwayland) running"
  exit 0
fi

# fallback: check drm devices (no compositor)
if [ -d /dev/dri ]; then
  echo "No X/Wayland detected; /dev/dri exists — may be DRM/KMS display (direct rendering)"
else
  echo "No graphical display server detected"
fi
