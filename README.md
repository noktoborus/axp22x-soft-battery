# axp22x-soft-battery

The battery level of the ClockworkPi uConsole with a CM5 and the 21700
battery board of HackerGadgets, counted in software.

On this board the gauge of the PMIC AXP228 is wrong, and when the cells
run low the AXP228 turns itself off while the system keeps running from
the board: nothing powers the system off, it just dies. The module shows
a level counted by a model of the cells; the daemon learns the cells
down to the moment the AXP goes silent and powers the system off there.

## Build

Needs the headers of the kernel, `dtc`, Rust (`cargo`). The kernel must
have `CONFIG_BATTERY_AXP20X` as a module or off.

```sh
make -C module KDIR=/lib/modules/$(uname -r)/build
dtc -@ -I dts -O dtb -o axp22x-soft-battery.dtbo module/axp22x-soft-battery.dts
cargo build --release --manifest-path daemon/Cargo.toml
```

With DKMS, built again for each new kernel:

```sh
sudo cp -r module /usr/src/axp22x-soft-battery-0.1.0
sudo dkms install axp22x-soft-battery/0.1.0
```

## Install

```sh
sudo cp axp22x-soft-battery.dtbo /boot/overlays/   # /boot/firmware/overlays on Raspberry Pi OS
sudo cp module/axp22x-soft-battery.conf /etc/modprobe.d/
sudo install -m755 daemon/target/release/axp22x-soft-battery /usr/bin/
sudo cp daemon/axp22x-soft-battery.service /etc/systemd/system/
```

In `config.txt`, after the overlay of the uConsole:

```
dtoverlay=axp22x-soft-battery
```

Reboot, then start the daemon:

```sh
sudo systemctl enable --now axp22x-soft-battery.service
```

## The curves of your cells

After one or more runs from a full charge down to the power off:

```sh
axp22x-soft-battery gen-dts > ocv.dts
sudo dtc -@ -I dts -O dtb -o /boot/overlays/axp22x-soft-battery-ocv.dtbo ocv.dts
```

and `dtoverlay=axp22x-soft-battery-ocv` after the first overlay in
`config.txt`.

## License

GPL-2.0 (`LICENSE`).
