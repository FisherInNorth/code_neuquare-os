make -j4
sudo make install
sudo cp /usr/local/musl/bin/musl-gcc /usr/bin/riscv64-musl-gcc
cd ../../testsuits-for-oskernel/busybox
make clean
make CC="riscv64-musl-gcc" STRIP=riscv64-linux-gnu-strip -j4
cp busybox ../../myos/busybox/
