#!/bin/sh
# ---
# monerod-archive install
# ---
# Version 6
# Written by Neptune Research
# ---
# This is a run-once setup script to install the necessary libraries for monerod-archive,
# and to prepare for monerod-archive to write the output file to /opt/monerodarchive/archive.log
# ---

# 1. make folder for archive file
sudo mkdir /opt/monerodarchive

# 2. set folder access to full access for everyone
sudo chmod 777 /opt/monerodarchive

# 3. install necessary libraries
sudo apt update && sudo apt install build-essential cmake pkg-config libboost-all-dev libssl-dev libzmq3-dev libunbound-dev libsodium-dev libunwind8-dev liblzma-dev libreadline6-dev libldns-dev libexpat1-dev doxygen graphviz libpcsclite-dev

sudo apt-get install libgtest-dev && cd /usr/src/gtest && sudo cmake . && sudo make && sudo mv libg* /usr/lib/
# ^^ IsthmusCrypto mass added these from monero repo README. 

#4. Give instructions for startup
echo
echo %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
echo %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
echo Everything is prepared, so you can now switch over to the archival daemon. To do so:
echo Move monerod-archive to your regular monero binaries folder, 
echo then run monerod-archive instead of vanilla monerod.
echo \(shut down regular monerod first\)
echo
echo The standard daemon logs still written to \~/.bitmonero/bitmonero.log
echo The new archival logs are written to /opt/monerodarchive/archive.log
echo Please enjoy! 
echo
echo Created by Neptune Research, https://github.com/NeptuneResearch
echo to enable data collection for the Monero Archival Project
echo \(a product of \#Noncesense-Research-Lab on Freenode\)

