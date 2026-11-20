# axp22x-soft-battery

The battery level of the ClockworkPi uConsole with a CM5 and the 21700
battery board of HackerGadgets, counted in software. On this board the
gauge of the PMIC AXP228 is wrong; the module shows a level counted by a
model of the cells instead.

## 1. The module: build and install

Needs the headers of the kernel and `dtc`. The kernel must have
`CONFIG_BATTERY_AXP20X` as a module or off.

```sh
make -C module
sudo make -C module modules_install
dtc -@ -I dts -O dtb -o axp22x-soft-battery.dtbo module/axp22x-soft-battery.dts
sudo cp axp22x-soft-battery.dtbo /boot/overlays/   # /boot/firmware/overlays on Raspberry Pi OS
sudo cp module/axp22x-soft-battery.conf /etc/modprobe.d/
```

Or by DKMS, built again for each new kernel, in place of the two `make`:

```sh
sudo cp -r module /usr/src/axp22x-soft-battery-0.1.0
sudo dkms install axp22x-soft-battery/0.1.0
```

In `config.txt`, after the overlay of the uConsole, then reboot:

```
dtoverlay=axp22x-soft-battery
```

## 2. The service: why

When the cells run low, the AXP228 turns itself off, but the system keeps
running from the board: nothing powers it off, it just dies. The service
`axp22x-soft-battery` watches the battery of the module and powers the
system off when the AXP goes silent. On each run from a full charge down
to that point it learns the cells; `gen-dts` writes what it learned as an
overlay, so the level fits your cells.

## 3. The service: build and install

Needs Rust (`cargo`).

```sh
cargo build --release --manifest-path daemon/Cargo.toml
sudo install -m755 daemon/target/release/axp22x-soft-battery /usr/bin/
sudo cp daemon/axp22x-soft-battery.service /etc/systemd/system/
sudo systemctl enable --now axp22x-soft-battery.service
```

After one or more runs down to the power off, the curves of your cells:

```sh
axp22x-soft-battery gen-dts > ocv.dts
sudo dtc -@ -I dts -O dtb -o /boot/overlays/axp22x-soft-battery-ocv.dtbo ocv.dts
```

and `dtoverlay=axp22x-soft-battery-ocv` after the first overlay in
`config.txt`.

## License

GPL-2.0 (`LICENSE`).
