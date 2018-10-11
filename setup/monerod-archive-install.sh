#!/bin/sh
# ---
# monerod-archive Installer
# ---
# Version 8
# (c) 2018 Neptune Research (https://github.com/neptuneresearch)
# ---
# This will let monerod-archive write the archive file: /opt/monerodarchive/archive.log
# ---
echo +----------------------------------------------------
echo - monerod-archive installing
echo +----------------------------------------------------
# 1. make folder
sudo mkdir /opt/monerodarchive
# 2. set folder access to full access for everyone
sudo chmod 666 /opt/monerodarchive
# 3. optional instructions for glibc
echo 
echo +----------------------------------------------------
echo - Archive Output Directory installed.
echo - monerod-archive install complete
echo +----------------------------------------------------