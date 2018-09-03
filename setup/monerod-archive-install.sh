#!/bin/sh
# ---
# monerod-archive Installer
# ---
# Version 6
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
echo - Archive Output Directory installed
echo +----------------------------------------------------
echo
echo +----------------------------------------------------
echo - Your glibc version is: 
echo +----------------------------------------------------
ldd --version
echo
echo          !!!  If you have less than 2.27 above,  !!!
echo       !!!  please follow the "Glibc Update" section  !!!
echo           !!!  in the monerod-archive README.  !!!
echo
echo +----------------------------------------------------
echo - monerod-archive install complete
echo +----------------------------------------------------