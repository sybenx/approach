# Approach

A Pebble watchface that shows the time, a few small info slots, and a bar that counts down to the next half-hour mark (:00 and :30, with an optional minutes offset).

## What it runs on

- Pebble watches: aplite, basalt, diorite and emery (Pebble SDK 3 project, version 1.3.2).
- Watch side is C (`src/c/main.c`). Phone side is JavaScript (`src/pkjs/`) with the `pebble-clay` settings page.
- Weather comes from the Open-Meteo API (no key), using the phone's location. Heart rate and steps use the watch's health service where the watch has it.

## Install and run

There is no packaged release or store listing in the repo. To use it you build it yourself with the Pebble SDK (the `pebble` tool, `wscript` is the standard waf build) and install the resulting watchface on a watch or emulator. Settings (theme, colours, slots, buzz options, units) are on the Clay page in the Pebble phone app.

## Status

Built for my own use. It works on my setup. No support promised.
