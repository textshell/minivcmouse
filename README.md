minivcmouse
===========

minivcmouse is a simple linux console mouse daemon.

It allows using the mouse on a raw (ctrl-alt-F4 etc) virtual console on linux. It does nothing when X11 or Wayland are active in a virtual console.

It uses libinput for all input device detection and interfacing. Currently it has no configuration options itself, so only defaults of libinput or configuration libinput picks up from udev are supported.

Features:

* Mouse pointer
* Selection with left dragging.
* double click yields word selection.
* tripple click yields line selection.
* middle button click pastes.
* mouse interface for terminal applications using mouse modes ESC [ 9h or ESC [ 1000h using basic (ESC M B X Y non CSI formating). This is not the GPM mouse protocol.

Status
======
This is a weekend prove of concept project to explore the selection and mouse tracking ioctls in mainline kernels.

Maybe it's useful, maybe not really. It's future is unsure, but contributions are welcome. Maybe if enough users find it useful it could get some minimal configuration and actual error handling, etc.
