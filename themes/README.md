# Theming Guide

Welcome to the `themes/` directory! Creating a custom theme is straightforward. 

## How it works

Every theme is just a folder inside this directory. The name of the folder becomes the name of the theme in the app's settings dropdown. 

A complete theme can contain the following files:
* `theme.json` (defines all the UI and element colors using hex codes)
* `font.ttf` (custom UI font)
* `earth.png` (daytime Earth texture)
* `earth_night.png` (nighttime Earth texture)
* `clouds.png` (cloud layer texture)
* `moon.png` (lunar texture)
* `sat_icon.png` (icon for satellites)
* `marker_icon.png` (icon for ground markers)
* `smallmark.png` (icon for apoapsis/periapsis markers)

## The Default Fallback

You **do not** need to include every single file to make a theme. 

TLEscope uses a fallback system. If your theme folder only contains a `theme.json` and a custom `earth.png`, the app will automatically load all the missing textures, fonts, and even missing color definitions from the `default` theme. 

The easiest way to start is to create a new folder, copy the `theme.json` from the `default` folder, and start tweaking the hex colors.

## Hot Reloading

You don't need to restart the app to see your changes. Just open the Settings menu and select your theme from the dropdown. If you overwrite an image or update `theme.json` while the theme is active, simply select the theme again in the dropdown to instantly hot-reload your new assets.
