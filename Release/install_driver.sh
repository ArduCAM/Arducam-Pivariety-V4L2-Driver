#!/bin/bash
echo "Installing Arducam-Pivariety-V4L2-Driver..."
echo "--------------------------------------"
sudo install -p -m 755 ./arducam_camera_selector.sh /usr/bin/
sudo install -p -m 644 ./bin/$(uname -r)/arducam.ko  /lib/modules/$(uname -r)/kernel/drivers/media/i2c/
sudo install -p -m 644 ./bin/$(uname -r)/arducam.dtbo /boot/overlays/
sudo /sbin/depmod -a $(uname -r)
awk 'BEGIN{ count=0 }       \
{                           \
    if($1 == "dtoverlay=arducam"){       \
        count++;            \
    }                       \
}END{                       \
    if(count <= 0){         \
        system("sudo sh -c '\''echo dtoverlay=arducam >> /boot/config.txt'\''"); \
    }                       \
}' /boot/config.txt
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

        
