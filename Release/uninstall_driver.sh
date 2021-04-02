#!/bin/bash
echo "uninstall arducam driver"
sudo sed 's/^dtoverlay=arducam/#dtoverlay=arducam/g' -i /boot/config.txt
echo "reboot now?(y/n):"
read USER_INPUT
case $USER_INPUT in
'y'|'Y')
    echo "reboot"
    sudo reboot
;;
*)
    echo "cancel"
;;
esac

