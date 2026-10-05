---
title: Collect USB Logs
description: Capture Espframe startup logs over USB in a desktop browser and share them safely for troubleshooting.
---

# Collect USB Logs

USB logs show what Espframe is doing during startup, Wi-Fi setup, Home Assistant connection, and photo loading. You can capture them in Chrome or Edge without installing a serial-monitor program.

## What you need

- A desktop computer running **Chrome or Edge**.
- A **USB-C data cable** connected to the display's **bottom USB-C port**.
- The display powered on. Close other serial monitors or flashing tools that may already be using the port.

## Capture a startup log

1. Connect the display to your computer with the USB-C cable.
2. Click **Connect to USB** below and select the serial port that appeared when you connected the display.
3. Leave the log viewer open, then press the display's reset button or disconnect and reconnect its power.
4. Wait until the issue happens or the display finishes starting.
5. Click **Stop listening**, then **Copy logs**.

<USBSerialLogs />

The viewer only opens a serial log connection; it does not install or erase firmware. If the port is missing, try another data-capable cable, close other programs using USB serial, and reconnect the display before opening the port chooser again.

## Review logs before sharing

Logs can include device names, Wi-Fi network names, local IP addresses, and server URLs. Review the text and remove anything you consider private. Never post passwords, API keys, tokens, or other credentials.

Open an [Espframe GitHub issue](https://github.com/jtenniswood/espframe/issues/new) and include the display revision, firmware version if known, what you expected, what happened, and the relevant log lines. Keep nearby lines around an error so the startup sequence remains useful.

Related: [Troubleshooting](/troubleshooting) · [USB Flashing Help](/usb-flashing)
