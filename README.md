android-vnc-server
==================
android-vnc-server for kitkat
This is a vnc server which pushes framebuffer of mobile device to vnc clients.
This server accepts mouse drag as touch events and key inputs.

Build
=====
1. download `libvncserver` and `libjpeg-turbo` from web, and put them to aosp source code tree
2. copy the related Android.mk to the related dir
3. run `cd libvncserver; ./autogen.sh && ./configure --without-crypto`
4. run `cd libjpeg-turbo; ./configure`
5. build the system image

Usage
=====
1. run `./android-vnc-server` on mobile device
2. connect mobile device to pc through adb
3. run `adb forward tcp:5901 tcp:5901` on pc
4. run vnc client(e.g. remmina, gvncviewer, novnc ...) to connect to 127.0.0.1:5901
5. bang.... a mirror of your mobile device, touch with your mouse

Note
====
You may need to debug it yourself.
I've tested with an aosp rom, works fine.
But didn't work with cyanogenmod of HTC ONE S.
