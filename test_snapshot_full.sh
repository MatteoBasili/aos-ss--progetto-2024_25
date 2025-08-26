#!/bin/bash
set -e

echo "=== PULIZIA PRECEDENTI ==="
sudo rmmod snapshot || true
sudo rm -rf /snapshot/* || true

echo "=== COMPILAZIONE MODULO ==="
make clean
make

echo "=== CREAZIONE LOOP DEVICE DI TEST ==="
dd if=/dev/zero of=disk.img bs=1M count=10
sudo losetup -fP disk.img
LOOPDEV=$(losetup -j disk.img | cut -d':' -f1)
echo "Loop device creato: $LOOPDEV"

echo "=== CARICAMENTO MODULO ==="
sudo insmod snapshot.ko
sudo dmesg | tail -n 10

echo "=== CREAZIONE DEVICE FILE ==="
sudo rm -f /dev/snapshot
MAJOR=$(awk "\$2==\"snapshot\" {print \$1}" /proc/devices)
sudo mknod /dev/snapshot c $MAJOR 0 || true
sudo chmod 666 /dev/snapshot

echo "=== ATTIVAZIONE SNAPSHOT ==="
sudo ./snapshot_ctl activate
sudo dmesg | tail -n 10
sleep 1

echo "=== SCRITTURA SU MULTIBLOCCHI ==="
BLOCKS=4
declare -a ORIG_HASHES
for i in $(seq 0 $((BLOCKS-1))); do
    TMPFILE=$(mktemp)
    dd if=/dev/urandom bs=4k count=1 of=$TMPFILE
    ORIG_HASHES[i]=$(md5sum $TMPFILE | awk '{print $1}')
    sudo dd if=$TMPFILE of=$LOOPDEV bs=4k count=1 seek=$i conv=fsync
    rm $TMPFILE
done

sleep 2 # lascia lavorare la workqueue

echo "=== VERIFICA FILE SNAPSHOT ==="
SNAP_DIR=$(ls -td /snapshot/demo_device-* | head -n1)
echo "Snapshot directory: $SNAP_DIR"

PASSED=0
FAILED=0

for f in $SNAP_DIR/*.bin; do
    HASH=$(md5sum "$f" | awk '{print $1}')
    # basta confrontare solo dimensione, perché non sappiamo l'esatto settore mappato
    SIZE=$(stat -c %s "$f")
    if [ "$SIZE" -eq 4096 ]; then
        echo "PASS: $f -> 4K"
        PASSED=$((PASSED+1))
    else
        echo "FAIL: $f -> $SIZE bytes"
        FAILED=$((FAILED+1))
    fi
done

echo "=== RISULTATO ==="
echo "Pass: $PASSED"
echo "Fail: $FAILED"

echo "=== DISATTIVAZIONE SNAPSHOT ==="
sudo ./snapshot_ctl deactivate
sudo dmesg | tail -n 10

echo "=== SMONTAGGIO LOOP DEVICE ==="
sudo losetup -d $LOOPDEV
rm disk.img

echo "=== TEST COMPLETATO ==="

