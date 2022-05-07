#!/usr/bin/env python3
# -*- coding: UTF-8 -*-
# 文件名：client.py

import curses
from curses import wrapper
from socket import *


# Rendering status bar
def RenderStatusBar(stdscr):
    height, width = stdscr.getmaxyx()
    statusbarstr = "Press 'q' to exit"
    stdscr.attron(curses.color_pair(3))
    stdscr.addstr(height - 1, 0, statusbarstr)
    stdscr.addstr(height - 1, len(statusbarstr), " " * (width - len(statusbarstr) - 1))
    stdscr.attroff(curses.color_pair(3))


# Rendering description
def RenderDescription(stdscr):
    focus_desc    = "Focus        : 'f' Key"
    scale_desc    = "Digital Zoom : 'w'/'s' Key"
    max_desc      = "Max Zoom     : 'm' Key"
    reset_desc    = "Reset Zoom   : 'r' Key"
    move_lr_desc  = "Move L/R     : Left / Right Key"
    move_ud_desc  = "Move Up/Down : Up / Down Key"

    desc_y = 1

    stdscr.addstr(desc_y + 1, 0, focus_desc, curses.color_pair(1))
    stdscr.addstr(desc_y + 2, 0, scale_desc, curses.color_pair(1))
    stdscr.addstr(desc_y + 3, 0, max_desc, curses.color_pair(1))
    stdscr.addstr(desc_y + 4, 0, reset_desc, curses.color_pair(1))
    stdscr.addstr(desc_y + 5, 0, move_lr_desc, curses.color_pair(1))
    stdscr.addstr(desc_y + 6, 0, move_ud_desc, curses.color_pair(1))


# Rendering  middle text
def RenderMiddleText(stdscr, k, focuser):
    # get height and width of the window.
    height, width = stdscr.getmaxyx()
    # Declaration of strings
    title = "Arducam Controller"[: width - 1]
    subtitle = ""[: width - 1]
    keystr = "Last key pressed: {}".format(k)[: width - 1]

    # # Obtain device infomation
    # focus_value = "Focus    : {}".format(focuser.get(Focuser.OPT_FOCUS))[:width-1]

    if k == 0:
        keystr = "No key press detected..."[: width - 1]

    # Centering calculations
    start_x_title = int((width // 2) - (len(title) // 2) - len(title) % 2)
    start_x_subtitle = int((width // 2) - (len(subtitle) // 2) - len(subtitle) % 2)
    start_x_keystr = int((width // 2) - (len(keystr) // 2) - len(keystr) % 2)
    start_x_device_info = int(
        (width // 2) - (len("Focus    : 00000") // 2) - len("Focus    : 00000") % 2
    )
    start_y = int((height // 2) - 6)

    # Turning on attributes for title
    stdscr.attron(curses.color_pair(2))
    stdscr.attron(curses.A_BOLD)

    # Rendering title
    stdscr.addstr(start_y, start_x_title, title)

    # Turning off attributes for title
    stdscr.attroff(curses.color_pair(2))
    stdscr.attroff(curses.A_BOLD)

    # Print rest of text
    stdscr.addstr(start_y + 1, start_x_subtitle, subtitle)
    stdscr.addstr(start_y + 3, (width // 2) - 2, "-" * 4)
    stdscr.addstr(start_y + 5, start_x_keystr, keystr)
    # Print device info
    # stdscr.addstr(start_y + 6, start_x_device_info, focus_value)


def main(stdscr):
    # Clear and refresh the screen for a blank canvas
    stdscr.clear()
    stdscr.refresh()
    # Create a socket object
    s = socket(family=AF_INET, type=SOCK_DGRAM, proto=0)
    # Get localhost name
    # host = gethostname()
    host = "127.0.0.1"
    # Set port number
    port = 8080

    # Start colors in curses
    curses.start_color()
    curses.init_pair(1, curses.COLOR_CYAN, curses.COLOR_BLACK)
    curses.init_pair(2, curses.COLOR_RED, curses.COLOR_BLACK)
    curses.init_pair(3, curses.COLOR_BLACK, curses.COLOR_WHITE)
    k = 0
    words = []
    send_words = ""
    # Loop where k is the last character pressed
    while True:
        # Initialization
        stdscr.clear()
        # Flush all input buffers.
        curses.flushinp()
        # get height and width of the window.
        height, width = stdscr.getmaxyx()
        # Rendering some text
        whstr = "Width: {}, Height: {}".format(width, height)
        stdscr.addstr(0, 0, whstr, curses.color_pair(1))
        # if len(words) == height:
        #     words.pop(0)
        # stdscr.addstr(1, 0, "\n".join(words), curses.color_pair(3))

        # stdscr.addstr(1, int(width / 2), f"{send_words = }", curses.color_pair(3))
        if k == ord("q"):
            break
        elif k in [ord("x"), ord("X")]:
            s.sendto(b"X", (host, port))
        elif k in [ord("f"), ord("F")]:
            s.sendto(b"F", (host, port))

        elif k in [ord("w"), ord("W")]:
            s.sendto(b"W", (host, port))
        elif k in [ord("s"), ord("S")]:
            s.sendto(b"S", (host, port))

        elif k in [curses.KEY_RIGHT, ord("l"), ord("L")]:
            s.sendto(b"L", (host, port))
        elif k in [curses.KEY_LEFT, ord("j"), ord("J")]:
            s.sendto(b"J", (host, port))
        elif k in [curses.KEY_UP, ord("i"), ord("I")]:
            s.sendto(b"I", (host, port))
        elif k in [curses.KEY_DOWN, ord("k"), ord("K")]:
            s.sendto(b"K", (host, port))

        elif k in [ord("m"), ord("M")]:
            s.sendto(b"M", (host, port))
        elif k in [ord("r"), ord("r")]:
            s.sendto(b"R", (host, port))

        else:
            pass
            # send_words = send_words + chr(k)

        # render key description
        RenderDescription(stdscr)
        # render status bar
        RenderStatusBar(stdscr)
        # render middle text
        focuser = None
        RenderMiddleText(stdscr, k, focuser)

        # Refresh the screen
        stdscr.refresh()
        # Wait for next input
        k = stdscr.getch()
        # k = stdscr.getkey()

    s.close()


wrapper(main)
