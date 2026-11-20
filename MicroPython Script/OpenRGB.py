# Galactic Unicorn USB-serial receiver for the OpenRGB native plugin.
# Pico 2 W / Pimoroni Galactic Unicorn MicroPython firmware.
#
# Wire protocol (over the Pico's USB CDC serial port):
#   host -> Pico : 4-byte ASCII header "GU53" + 583 RGB triplets, row-major
#                  (R,G,B). Frame size = 4 + 53*11*3 = 1753 bytes.
#   Pico -> host : single byte 0xA5 after each rendered frame, and every
#                  ~0.5 s while idle. The host keeps at most 2 frames in
#                  flight, so frames never queue up and lag.
#
# Rendering converts RGB888 -> RGB565 straight into the display framebuffer
# with a viper-compiled loop (~100x faster than create_pen()/pixel() per LED).
# Press Galactic Unicorn button B to leave this mode and return to main.py.

import sys
import time
import select
import micropython

from galactic import GalacticUnicorn
from picographics import PicoGraphics, DISPLAY_GALACTIC_UNICORN, PEN_RGB565

WIDTH = 53
HEIGHT = 11
LED_COUNT = WIDTH * HEIGHT
PIXEL_BYTES = LED_COUNT * 3
FRAME_BYTES = 4 + PIXEL_BYTES
HEADER = b"GU53"
ACK = b"\xa5"

gu = GalacticUnicorn()
display = PicoGraphics(display=DISPLAY_GALACTIC_UNICORN, pen_type=PEN_RGB565)
gu.set_brightness(0.45)

stdin = sys.stdin.buffer
stdout = sys.stdout.buffer
poll = select.poll()
poll.register(sys.stdin, select.POLLIN)

def show_status(text, colour=(255, 255, 255)):
    display.set_pen(display.create_pen(0, 0, 0))
    display.clear()
    display.set_pen(display.create_pen(colour[0], colour[1], colour[2]))
    display.text(text, 0, 2, 53, 0.5)
    gu.update(display)

fb = None
BIG_ENDIAN = True

def detect_framebuffer():
    global fb, BIG_ENDIAN
    try:
        buf = memoryview(display)
        if len(buf) < LED_COUNT * 2:
            return
        display.set_pen(display.create_pen(0, 0, 0))
        display.clear()
        display.set_pen(display.create_pen(255, 0, 0))
        display.pixel(0, 0)
        first = (buf[0], buf[1])
        if first == (0xF8, 0x00):
            BIG_ENDIAN = True
        elif first == (0x00, 0xF8):
            BIG_ENDIAN = False
        else:
            return
        fb = buf
    except Exception:
        fb = None


@micropython.viper
def convert_be(src: ptr8, dst: ptr8, n: int):
    for i in range(n):
        s = 4 + i * 3
        r = int(src[s])
        g = int(src[s + 1])
        b = int(src[s + 2])
        dst[i * 2] = (r & 0xF8) | (g >> 5)
        dst[i * 2 + 1] = ((g & 0x1C) << 3) | (b >> 3)


@micropython.viper
def convert_le(src: ptr8, dst: ptr8, n: int):
    for i in range(n):
        s = 4 + i * 3
        r = int(src[s])
        g = int(src[s + 1])
        b = int(src[s + 2])
        dst[i * 2] = ((g & 0x1C) << 3) | (b >> 3)
        dst[i * 2 + 1] = (r & 0xF8) | (g >> 5)
def render_frame(frame):
    if fb is not None:
        if BIG_ENDIAN:
            convert_be(frame, fb, LED_COUNT)
        else:
            convert_le(frame, fb, LED_COUNT)
    else:
        offset = 4
        for y in range(HEIGHT):
            for x in range(WIDTH):
                display.set_pen(display.create_pen(frame[offset], frame[offset + 1], frame[offset + 2]))
                display.pixel(x, y)
                offset += 3
    gu.update(display)

def send_ack():
    try:
        stdout.write(ACK)
    except Exception:
        pass

def resync():
    idx = 0
    while idx < 4:
        if not poll.poll(100):
            return None
        c = stdin.read(1)
        if not c:
            return None
        if c[0] == HEADER[idx]:
            idx += 1
        else:
            idx = 1 if c[0] == HEADER[0] else 0
    if not poll.poll(500):
        return None
    body = stdin.read(PIXEL_BYTES)
    if body is None or len(body) != PIXEL_BYTES:
        return None
    return HEADER + body


def read_frame():
    """Fast path: one read of a whole frame; falls back to resync()."""
    frame = stdin.read(FRAME_BYTES)
    if frame is not None and len(frame) == FRAME_BYTES and frame[0:4] == HEADER:
        return frame
    return resync()


def serve():
    micropython.kbd_intr(-1)
    detect_framebuffer()
    show_status("USB WAIT", (40, 130, 255))
    last_ping = time.ticks_ms()

    try:
        while not gu.is_pressed(GalacticUnicorn.SWITCH_B):
            if not poll.poll(50):
                if time.ticks_diff(time.ticks_ms(), last_ping) > 500:
                    send_ack()
                    last_ping = time.ticks_ms()
                continue

            frame = read_frame()
            if frame is None:
                continue

            render_frame(frame)
            send_ack()
            last_ping = time.ticks_ms()
    finally:
        micropython.kbd_intr(3)

    show_status("MENU", (255, 255, 255))
    time.sleep_ms(300)
serve()
