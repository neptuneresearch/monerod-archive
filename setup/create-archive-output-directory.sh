#!/bin/sh
# ---
# monerod-archive Installer
# ---
# Version 17
# (c) 2018-2020 Neptune Research (https://github.com/neptuneresearch)
# ---
echo +----------------------------------------------------
echo - monerod-archive installing
echo +----------------------------------------------------
# 1. make folder
sudo mkdir /opt/monerodarchive
# 2. set folder access to full access for everyone
sudo chmod 755 /opt/monerodarchive
echo 
echo +----------------------------------------------------
echo - Archive Output Directory installed.
echo - monerod-archive install complete
echo +----------------------------------------------------