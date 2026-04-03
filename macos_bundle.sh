#!/bin/bash
cp -r bundle dist/TLEscope.app
cp -r dist/TLEscope-macOS-Portable/* dist/TLEscope.app/Contents/Resources
mv dist/TLEscope.app/Contents/Resources/TLEscope dist/TLEscope.app/Contents/MacOS/TLEscope.bin
chmod +x dist/TLEscope.app/Contents/MacOS/TLEscope # jus to be safe
