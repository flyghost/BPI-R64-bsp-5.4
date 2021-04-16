#!/bin/bash
# (c) 2015, 2016, Leo Xu <otakunekop@banana-pi.org.cn>
# Build script for BPI-M2U-BSP 2016.09.10

TARGET_PRODUCT="bpi-r64"
ALL_SOC="bpi-r64 bpi-r2"
BOARD=BPI-R64-console
board="bpi-r64"
kernel="5.4.0-BPI-R64-Kernel"
MODE=$1
BPILINUX=linux-mt
BPIPACK=mt-pack
BPISOC=mtk
service=linux-5.4
RET=0

cp_download_files()
{
T="$TOPDIR"
SD="$T/SD"
U="${SD}/100MB"
B="${SD}/BPI-BOOT"
R="${SD}/BPI-ROOT"
	#
	## clean SD dir.
	#
	rm -rf $SD
	#
	## create SD dirs (100MB, BPI-BOOT, BPI-ROOT) 
	#
	mkdir -p $SD
	mkdir -p $U
	mkdir -p $B
	mkdir -p $R
	#
	## copy files to 100MB
	#
	cp -a $T/out/${TARGET_PRODUCT}/100MB/* $U
	#
	## copy files to BPI-BOOT
	#
	mkdir -p $B/bananapi/${board}
	cp -a $T/${BPIPACK}/${BPISOC}/${TARGET_PRODUCT}/configs/default/$service $B/bananapi/${board}/
	cp -a $T/${BPILINUX}/arch/arm64/boot/uImage $B/bananapi/${board}/$service/uImage
	cp -a $T/${BPILINUX}/arch/arm64/boot/dts/mediatek/*.dtb $B/bananapi/${board}/$service/dtb

	#
	## copy files to BPI-ROOT
	#
	mkdir -p $R/usr/lib/u-boot/bananapi/${board}
	cp -a $U/*.gz $R/usr/lib/u-boot/bananapi/${board}/
	rm -rf $R/lib/modules
	mkdir -p $R/lib/modules
	cp -a $T/${BPILINUX}/output/lib/modules/${kernel} $R/lib/modules
	#
	## create files for bpi-tools & bpi-migrate
	#
	(cd $B ; tar czvf $SD/BPI-BOOT-${board}-${service}.tgz .)
	(cd $R ; tar czvf $SD/${kernel}-net.tgz lib/modules/${kernel}/kernel/net)
	(cd $R ; mv lib/modules/${kernel}/kernel/net $R/net)
	(cd $R ; tar czvf $SD/${kernel}.tgz lib/modules)
	(cd $R ; mv $R/net lib/modules/${kernel}/kernel/net)
	(cd $R ; tar czvf $SD/BOOTLOADER-${board}-${service}.tgz usr/lib/u-boot/bananapi)

	return #SKIP
}

list_boards() {
	cat <<-EOT
	NOTICE:
	new build.sh default select $BOARD and pack all boards
	supported boards:
	EOT
	for IN in ${ALL_SOC} ; do
	(
		if [ -d ${BPIPACK}/${BPISOC}/${IN}/configs ] ; then
			cd ${BPIPACK}/${BPISOC}/${IN}/configs ; ls -1d BPI* 
		fi
	)
	done
	echo
}

list_boards

./configure $BOARD

if [ -f env.sh ] ; then
	. env.sh
fi

echo "This tool support following building mode(s):"
echo "--------------------------------------------------------------------------------"
echo "	1. Build all, uboot and kernel and pack to download images."
echo "	2. Build uboot only."
echo "	3. Build kernel only."
echo "	4. kernel configure."
echo "	5. Pack the builds to target download image, this step must execute after u-boot,"
echo "	   kernel and rootfs build out"
echo "	6. update files for SD"
echo "	7. Clean all build."
echo "--------------------------------------------------------------------------------"

if [ -z "$MODE" ]; then
	read -p "Please choose a mode(1-7): " mode
	echo
else
	mode=1
fi

if [ -z "$mode" ]; then
        echo -e "\033[31m No build mode choose, using Build all default   \033[0m"
        mode=1
fi

echo -e "\033[31m Now building...\033[0m"
echo
case $mode in
	1) RET=1;make && 
	   make pack && 
	   cp_download_files &&
           RET=0
           ;;
	2) make u-boot;;
	3) make kernel;;
	4) make kernel-config;;
	5) make pack;;
	6) cp_download_files;;
	7) make clean;;
esac
echo

if [ "$RET" -eq "0" ];
then
  echo -e "\033[32m Build success!\033[0m"
else
  echo -e "\033[31m Build failed!\033[0m"
fi
echo
