#!/bin/sh

make

VMLATENCY_DRIVER="vmlatency.ko"

[ -e $VMLATENCY_DRIVER ] || { echo "$VMLATENCY_DRIVER does not exist" ; exit 1 ; }

SUDO=""
if [ ! -z `id -u` ]; then
   SUDO="sudo"
fi

$SUDO /sbin/insmod $VMLATENCY_DRIVER
$SUDO /sbin/rmmod $VMLATENCY_DRIVER

cpu_name=`cat /proc/cpuinfo |grep "model name" |uniq |sed s/"^.*: "//`

dmesg | grep "\[vmlatency\]" | sed s/^"\[ *"[0-9]*\.[0-9]*"\] \[vmlatency\] "// > "$cpu_name".txt
