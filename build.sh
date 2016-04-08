#!/bin/bash
export PATH=/work/workspace/sourcecode/2440/toolschain/4.4.3/bin:$PATH
cp config_mini2440_n35 .config
make uImage
cp -r /home/jhf/sourcecode/2440/mini2440-linux-2.6.32.2/arch/arm/boot/uImage ./
make modules

