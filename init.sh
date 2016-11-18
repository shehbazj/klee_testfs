rm -rf /tmp/file
dd if=/dev/zero of=/tmp/file bs=4096 count=2560
./mktestfs /tmp/file
