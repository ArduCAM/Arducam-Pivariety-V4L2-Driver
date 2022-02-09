#!/bin/bash
 
###   -c --clear       : Clean up the drivers of IMX519 and arducam (restore the official camera)
###   -a --add name     : Specify the driver to be loaded, available options: imx519, arducam
###   examples: 
###     arducam_camera_selector.sh --add imx519
###     arducam_camera_selector.sh --add arducam
###     arducam_camera_selector.sh --clear

CONFIG_FILE=/boot/config.txt

ARGS=`getopt -o a:chb:: --long clear,add:,long,help -- "$@"`
if [ $? != 0 ]; then
    echo "Terminating..."
    exit 1
fi

eval set -- "${ARGS}"

help() {
    sed -rn 's/^### ?//;T;p;' "$0"
}
# echo $#
if [[ $# == 1 ]] || [[ "$1" == "-h" ]] || [[ "$1" == "--help" ]]; then
	help
	exit 1
fi
      

while true      
do
    case $1 in
        # add Annotation
        -c|--clear) 
            # Add notes   
            sudo sed 's/^\s*dtoverlay=imx519/#dtoverlay=imx519/g' -i $CONFIG_FILE
            sudo sed 's/^\s*dtoverlay=arducam/#dtoverlay=arducam/g' -i $CONFIG_FILE
            shift 
            ;;

        # Clear the annotation, if the required content is not exist, add it
        -a|--add)

            function get_search() {
                awk 'END {print}' $CONFIG_FILE
            }
            SEARCH="$(get_search)"
            NEED="[all]"
            NA=$2

            function get_all() {
                grep -n '\[[all]*\]' $CONFIG_FILE | tail -n 1 | cut -d ":" -f 1
            }
            function get_imx519() {
                grep -n "imx519" $CONFIG_FILE | cut -d ":" -f 1
            }
            function get_arducam() {
                grep -n "arducam" $CONFIG_FILE | cut -d ":" -f 1
            }
            CHICKIMX="$(get_imx519)"
            CHICKARD="$(get_arducam)"
            # printf "${CHICKALL}\n"

            if [ "$NA" = "imx519" ]; then
                    sudo sed 's/^\s*#\s*dtoverlay=imx519/dtoverlay=imx519/g' -i $CONFIG_FILE
                    sudo sed 's/^\s*dtoverlay=arducam/#dtoverlay=arducam/g' -i $CONFIG_FILE
                if [ "$CHICKIMX" = "" ];
                    then
                        # if [ "$SEARCH" != "$NEED" ]; then 
                        #     sudo sed '$a[all]' -i $CONFIG_FILE
                        # fi
                        # CHICKimx="$(get_all)"
                        # sed -i "$a dtoverlay=imx519" $CONFIG_FILE
                        sudo sed '$adtoverlay=imx519' -i $CONFIG_FILE
                fi

            elif [ "$NA" = "arducam" ]; then 
                    sudo sed 's/^\s*#\s*dtoverlay=arducam/dtoverlay=arducam/g' -i $CONFIG_FILE
                    sudo sed 's/^\s*dtoverlay=imx519/#dtoverlay=imx519/g' -i $CONFIG_FILE
                if [ "$CHICKARD" = "" ];
                    then
                        # if [ "$SEARCH" != "$NEED" ]; then  
                        #     sudo sed '$a[all]' -i $CONFIG_FILE
                        # fi
                        # CHICKard="$(get_all)"
                        # sed -i "$CHICKard a dtoverlay=arducam" $CONFIG_FILE
                        sudo sed '$adtoverlay=arducam' -i $CONFIG_FILE
                fi
            fi
            shift 
            ;;
        --)
            # echo 1
            shift
            break
            ;;
        *)
            # echo "Internal error!"
            exit 1
            ;;
    esac 
done  