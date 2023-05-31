#!/bin/sh

VMLATENCY=vmlatency.ko
CPU_NAME=`cat /proc/cpuinfo |grep "model name" |uniq |sed s/"^.*: "//`

[ -e $VMLATENCY ] || { echo "$VMLATENCY does not exist" ; exit 1 ; }

for i in $(seq 1 20)
do
    insmod $VMLATENCY
    rmmod $VMLATENCY
done

dmesg | grep "\[vmlatency\]" | sed s/^"\[ *"[0-9]*\.[0-9]*"\] "// |sed s/"\[vmlatency\] "// > "$CPU_NAME".txt
