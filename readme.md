# Custom firmware for Fortinet FortiGate 60 Firewall/Router

This is an attempt at creating some custom payload loading firmware for a Fortinet Fortigate 60 (FGT-60), an early 2000's hardware router/firewall.<br><br>
Specs are:
 - VIA C3 (Samuel 2) 400MHz CPU (speed unverified)
 - VIA VT8601 Apollo ProMedia Northbridge
 - VIA VT8231 Southbridge (uses internal SuperIO functions)
 - 3x VIA VT6105M Ethernet interfaces (one attached to a 4 port switch)
 - 128MB of PC133 CL3 RAM (soldered)

Basically a standard PC, including:
- VGA header (pinout unverified)
- DE9 Serial port (used for console access)
- 40 pin IDE interface
- CF card slot
- Possibly a header for PS2 keyboard/mouse (unverified)

Doing initial testing using a VIA EPIA ITX board as a base showed that it could bring up the basic hardware, however the EPIA uses DIMMs for memory and are configured via SPD chip over the SMBus.<br>
The FortiGate does have a I2C EEPROM on board, but I'm unsure what it contains, it might be memory config, but could just be ethernet interface MACs.<br><br>
After playing around with old LinuxBIOS/Coreboot code trying to get it to properly set up the RAM correctly, I ended up just reverse engineering the original firmware code.<br>
It would seem the original is possibly some kind of customized bring-up code that runs a GRUB based payload, but I didn't go any farther than the basic southbrige/northbridge config to get working RAM.<br><br>
That's where this code is mostly at this point. It configures the RAM, it's been tested with a simple RAM test (not in this code).<br>
Currently I'm attempting to load a FILO (an embedded bootloader) payload from Coreboot. FILO will load in QEMU, but I've hit several roadblocks on real hardware, mainly something weird with the programmable interrupt timer (PIT) and exception interrupts.<br>
The code doesn't set up the interrupts at all right now, so I think that might be why... But I've never done x86 hardware bring-up before, so I'm a little out of my element.<br><br>

## This is just a personal project
This will likely never help anyone, this is just for fun and learning... Plus it should be able to play DOOM, so why not!

## Update (11/8/2026)
I've added some basic (crude?) polling IDE interface code, as well as an MBR parser and some code to probe an EXT2 file system.<br>
Let's see if I can load a payload myself from an ext2 FS.<br><br>

## Update (11/8/2026)
Looks like reading from EXT2 is working well, read in a test payload elf from other attempts and it's executing that now... So in theory I should just be able to load a Linux kernel and initrd and it'll "Just work"?

## Update (11/9/2026)
Well, that wasn't nearly as bad as I though it was going to be! I have a live kernel booting now.<br>
This project is now more or less done, aside from a little tweaking here and whatever might pop up with getting a real rootfs on there.<br>
Next step will probably be swapping out the 32MB CF card with one of the 1GB cards I have and get a full root FS on there.

## Update (11/10/2026)
Got it mostly booting a real Linux install (Slackware 11.0 I think? It was a CF card that happened to have that already installed), it's currently hanging at "Updating shared library links:  /sbin/ldconfig".<br>
Not sure why though, unless it's just the abysmally slow IDE performance that's causing it to take forever to update, but I've left it sitting for quite some time and it doesn't get any further.<br>
Bypassing the system init (init=bash), I can get a usable system to invesigate, but obviously you have to do some more system init manually, which is annoying.<br>
Changes in firmware here are some tweaks to the UART, apparently the Linux kernel is pretty lazy at UART init and you must enable IRQs for it in hardware bring-up. Otherwise the kernel will fall back to polling the UART and it takes AGES to do anything.<br>
Other changes is a simple RAM test to actually count the amount of RAM rather than relying on the northbridge RAM registers to figure it out. And some minor tweaks to the EXT2 FS code to allow an FS that needs checking (otherwise you can't boot!)<br>

## Update (11/11/2026)
I'm an <b><i>idiot</i></b>. 😂<br>
It's actually booting just fine.<br>
After disabling the AGP GART module, I just didn't realize that I needed more than just "console=ttyS0", I also need to add the an agetty spawn for the serial line as well.<br>
Now I get a login prompt and mostly usable system!<br>
There is still more work to be done with the firmware however, the interrupts need more setup (as you can see from the boot log, probably also the reason the AGP GART module hangs) and ethernet is important on a router/firewall.<br>
USB is something I want to get working as well.<br>
Probably should set up the RTC as well, correct times/dates are nice to have for a working system.

## Current console output (this is from real hardware):
```FGT-60 early init
Stage 1: VT8231/UART up, VT8601 + SDRAM init...
................................................................................................................................
Counted RAM: 128MB

Stage 2:
C runtime: .bss zeroed, .data intact, stack ok
Total RAM: 00000080 MB (from as reported by northbridge)

Heap initialized successfully.
Heap available: 07effff0 bytes, largest free block: 07effff0 bytes
IDE device: present, LBA28, CactusFlashCard, 001f3920 sectors

Reading MBR...
MBR signature valid.
Primary partitions:
  Partition 00000001: type=00000083 start=00000038 sectors=001cd120 status=00000080
  Partition 00000002: type=00000082 start=001cd158 sectors=00025aa8 status=00000000

Reading EXT superblock...
EXT filesystem mounted.

Reading root inode...
Root directory size: 00001000

Root directory entries:
  inode=00000002 type=00000002 name=.
  inode=00000002 type=00000002 name=..
  inode=0000000b type=00000002 name=lost+found
  inode=0000e6c1 type=00000002 name=var
  inode=0001cd82 type=00000002 name=dev
  inode=0002b444 type=00000002 name=bin
  inode=00015a23 type=00000002 name=etc
  inode=000240e3 type=00000002 name=lib
  inode=0002b445 type=00000002 name=mnt
  inode=00007366 type=00000002 name=tmp
  inode=00015a26 type=00000002 name=sys
  inode=0002b44b type=00000002 name=usr
  inode=00007375 type=00000002 name=boot
  inode=00015a35 type=00000002 name=home
  inode=000240f5 type=00000002 name=proc
  inode=000327aa type=00000002 name=sbin
  inode=000327ab type=00000002 name=root
  inode=00000016 type=00000002 name=media
  inode=00000b92 type=00000001 name=vmlinuz
Directory listing complete.
Linux version 2.4.33.3 (root@tree) (gcc version 3.4.6) #1 Fri Sep 1 01:48:52 CDT 2006
BIOS-provided physical RAM map:
 BIOS-e820: 0000000000000000 - 0000000000100000 (reserved)
 BIOS-e820: 0000000000100000 - 0000000008000000 (usable)
0MB HIGHMEM available.
128MB LOWMEM available.
hm, page 00000000 reserved twice.
On node 0 totalpages: 32768
zone(0): 4096 pages.
zone(1): 28672 pages.
zone(2): 0 pages.
DMI not present.
Kernel command line: console=ttyS0,115200 root=/dev/hda1 ro rootwait ide=nodma
ide_setup: ide=nodma : Prevented DMA
Initializing CPU#0
Detected 400.916 MHz processor.
Console: colour dummy device 80x25
Calibrating delay loop... 799.53 BogoMIPS
Memory: 125468k/131072k available (1926k kernel code, 4580k reserved, 577k data, 124k init, 0k highmem)
Checking if this processor honours the WP bit even in supervisor mode... Ok.
Dentry cache hash table entries: 16384 (order: 5, 131072 bytes)
Inode cache hash table entries: 8192 (order: 4, 65536 bytes)
Mount cache hash table entries: 512 (order: 0, 4096 bytes)
Buffer cache hash table entries: 8192 (order: 3, 32768 bytes)
Page-cache hash table entries: 32768 (order: 5, 131072 bytes)
CPU: L1 I Cache: 64K (32 bytes/line), D cache 64K (32 bytes/line)
CPU: L2 Cache: 64K (32 bytes/line)
CPU: Centaur VIA Samuel 2 stepping 03
Checking 'hlt' instruction... OK.
POSIX conformance testing by UNIFIX
mtrr: v1.40 (20010327) Richard Gooch (rgooch@atnf.csiro.au)
mtrr: detected mtrr type: Intel
PCI: Using configuration type 1
PCI: Probing PCI hardware
PCI: Probing PCI hardware (bus 00)
PCI: Via IRQ fixup
Linux NET4.0 for Linux 2.4
Based upon Swansea University Computer Society NET3.039
Initializing RT netlink socket
Starting kswapd
VFS: Disk quotas vdquot_6.5.1
Journalled Block Device driver loaded
pty: 512 Unix98 ptys configured
Serial driver version 5.05c (2001-07-08) with HUB-6 MANY_PORTS MULTIPORT SHARE_IRQ SERIAL_PCI enabled
keyboard: Timeout - AT keyboard not present?(ed)
keyboard: Timeout - AT keyboard not present?(f4)
ttyS00 at 0x03f8 (irq = 4) is a 16550A
Real Time Clock Driver v1.10f
Floppy drive(s): fd0 is unknown type 15 (usb?), fd1 is unknown type 15 (usb?)
floppy0: no floppy controllers found
RAMDISK driver initialized: 16 RAM disks of 7777K size 1024 blocksize
loop: loaded (max 8 devices)
Uniform Multi-Platform E-IDE driver Revision: 7.00beta4-2.4
ide: Assuming 33MHz system bus speed for PIO modes; override with idebus=xx
VP_IDE: IDE controller at PCI slot 00:11.1
VP_IDE: chipset revision 6
VP_IDE: not 100% native mode: will probe irqs later
VP_IDE: VIA vt8231 (rev 10) IDE UDMA100 controller on pci00:11.1
    ide0: BM-DMA at 0xcc00-0xcc07, BIOS settings: hda:pio, hdb:pio
    ide1: BM-DMA at 0xcc08-0xcc0f, BIOS settings: hdc:pio, hdd:pio
hda: C/H/S=0/0/0 from BIOS ignored
hdb: C/H/S=0/0/0 from BIOS ignored
hda: CactusFlashCard, ATA DISK drive
ide0 at 0x1f0-0x1f7,0x3f6 on irq 14
hda: attached ide-disk driver.
hda: 2046240 sectors (1048 MB) w/0KiB Cache, CHS=2030/16/63
Partition check:
 hda: hda1 hda2
SCSI subsystem driver Revision: 1.00
kmod: failed to exec /sbin/modprobe -s -k scsi_hostadapter, errno = 2
kmod: failed to exec /sbin/modprobe -s -k scsi_hostadapter, errno = 2
kmod: failed to exec /sbin/modprobe -s -k scsi_hostadapter, errno = 2
md: linear personality registered as nr 1
md: raid0 personality registered as nr 2
md: raid1 personality registered as nr 3
md: raid5 personality registered as nr 4
raid5: measuring checksumming speed
   8regs     :   310.400 MB/sec
   32regs    :   155.600 MB/sec
   pII_mmx   :   480.400 MB/sec
   p5_mmx    :   420.800 MB/sec
raid5: using function: pII_mmx (480.400 MB/sec)
md: md driver 0.90.0 MAX_MD_DEVS=256, MD_SB_DISKS=27
md: Autodetecting RAID arrays.
md: autorun ...
md: ... autorun DONE.
LVM version 1.0.8(17/11/2003)
Initializing Cryptographic API
NET4: Linux TCP/IP 1.0 for NET4.0
IP Protocols: ICMP, UDP, TCP, IGMP
IP: routing cache hash table of 1024 buckets, 8Kbytes
TCP: Hash tables configured (established 8192 bind 16384)
Linux IP multicast router 0.06 plus PIM-SM
NET4: Unix domain sockets 1.0/SMP for Linux NET4.0.
VFS: Mounted root (ext2 filesystem) readonly.
Freeing unused kernel memory: 124k freed
INIT: version 2.84 booting
proc on /proc type proc (rw)
Adding Swap: 77132k swap-space (priority -1)
Testing root filesystem status:  read-only filesystem
Checking root filesystem:
fsck 1.38 (30-Jun-2005)
/dev/hda1 has gone 44358 days without being checked, check forced.
/dev/hda1: 25931/236288 files (0.4% non-contiguous), 64095/236068 blocks       
Remounting root device with read-write enabled.
/dev/hda1 on / type ext2 (rw)
Setting system time from the hardware clock (localtime).
select() to /dev/rtc to wait for clock tick timed out
Running /etc/rc.d/rc.modules-2.4.33.3:
Module dependencies up to date (no new kernel modules found).
scsi0 : SCSI host adapter emulation for IDE ATAPI devices
Checking non-root filesystems:
fsck 1.38 (30-Jun-2005)
Mounting non-root local filesystems:
devpts on /dev/pts type devpts (rw,gid=5,mode=620)
Using /etc/random-seed to initialize /dev/urandom.
INIT: Entering runlevel: 3
Going multiuser...
cannot (un)set powersave mode
/etc/rc.d/rc.inet1:  /sbin/ifconfig lo 127.0.0.1
/etc/rc.d/rc.inet1:  /sbin/route add -net 127.0.0.0 netmask 255.0.0.0 lo
via-rhine.c:v1.10-LK1.1.19  July-12-2003  Written by Donald Becker
  http://www.scyld.com/network/via-rhine.html
PCI: Enabling device 00:0e.0 (0080 -> 0083)
PCI: No IRQ known for interrupt pin A of device 00:0e.0. Please try using pci=biosirq.
eth0: VIA VT6105M Rhine-III at 0x1400, 00:09:0f:0a:23:3b, IRQ 0.
eth0: MII PHY found at address 1, status 0x7869 advertising 05e1 Link cde1.
PCI: Enabling device 00:0f.0 (0080 -> 0083)
PCI: No IRQ known for interrupt pin A of device 00:0f.0. Please try using pci=biosirq.
eth1: VIA VT6105M Rhine-III at 0x1800, 00:09:0f:0a:23:3a, IRQ 0.
eth1: MII PHY found at address 1, status 0x7849 advertising 05e1 Link 0000.
PCI: Enabling device 00:10.0 (0080 -> 0083)
PCI: No IRQ known for interrupt pin A of device 00:10.0. Please try using pci=biosirq.
eth2: VIA VT6105M Rhine-III at 0x1c00, 00:09:0f:0a:23:39, IRQ 0.
eth2: MII PHY found at address 1, status 0x7849 advertising 05e1 Link 0000.
PCI: Enabling device 00:12.0 (0080 -> 0083)
PCI: No IRQ known for interrupt pin A of device 00:12.0. Please try using pci=biosirq.
eth3: VIA VT6102 Rhine-II at 0x2000, 00:09:0f:0a:23:38, IRQ 0.
dhcpcd[299]: dhcpStart: ioctl SIOCSIFFLAGS: Device or resource busy

pci_hotplug: PCI Hot Plug PCI Core version: 0.5
shpchp: shpc_init : shpc_cap_offset == 0
shpchp: Standard Hot Plug PCI Controller Driver version: 0.4
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/shpchp.o.gz: init_module: No such device
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/shpchp.o.gz: Hint: insmod errors can be caused by incorrect module parameters, including invalid I.
      You may find more information in syslog or the output from dmesg
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/shpchp.o.gz: insmod shpchp failed
pci_hotplug: PCI Hot Plug PCI Core version: 0.5
pciehp: PCI Express Hot Plug Controller Driver version: 0.5
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/pciehp.o.gz: init_module: No such device
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/pciehp.o.gz: Hint: insmod errors can be caused by incorrect module parameters, including invalid I.
      You may find more information in syslog or the output from dmesg
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/pciehp.o.gz: insmod pciehp failed
pci_hotplug: PCI Hot Plug PCI Core version: 0.5
shpchp: shpc_init : shpc_cap_offset == 0
shpchp: Standard Hot Plug PCI Controller Driver version: 0.4
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/shpchp.o.gz: init_module: No such device
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/shpchp.o.gz: Hint: insmod errors can be caused by incorrect module parameters, including invalid I.
      You may find more information in syslog or the output from dmesg
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/shpchp.o.gz: insmod shpchp failed
pci_hotplug: PCI Hot Plug PCI Core version: 0.5
pciehp: PCI Express Hot Plug Controller Driver version: 0.5
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/pciehp.o.gz: init_module: No such device
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/pciehp.o.gz: Hint: insmod errors can be caused by incorrect module parameters, including invalid I.
      You may find more information in syslog or the output from dmesg
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/pciehp.o.gz: insmod pciehp failed
pci_hotplug: PCI Hot Plug PCI Core version: 0.5
shpchp: shpc_init : shpc_cap_offset == 0
shpchp: Standard Hot Plug PCI Controller Driver version: 0.4
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/shpchp.o.gz: init_module: No such device
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/shpchp.o.gz: Hint: insmod errors can be caused by incorrect module parameters, including invalid I.
      You may find more information in syslog or the output from dmesg
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/shpchp.o.gz: insmod shpchp failed
pci_hotplug: PCI Hot Plug PCI Core version: 0.5
pciehp: PCI Express Hot Plug Controller Driver version: 0.5
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/pciehp.o.gz: init_module: No such device
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/pciehp.o.gz: Hint: insmod errors can be caused by incorrect module parameters, including invalid I.
      You may find more information in syslog or the output from dmesg
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/pciehp.o.gz: insmod pciehp failed
pci_hotplug: PCI Hot Plug PCI Core version: 0.5
shpchp: shpc_init : shpc_cap_offset == 0
shpchp: Standard Hot Plug PCI Controller Driver version: 0.4
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/shpchp.o.gz: init_module: No such device
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/shpchp.o.gz: Hint: insmod errors can be caused by incorrect module parameters, including invalid I.
      You may find more information in syslog or the output from dmesg
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/shpchp.o.gz: insmod shpchp failed
pci_hotplug: PCI Hot Plug PCI Core version: 0.5
pciehp: PCI Express Hot Plug Controller Driver version: 0.5
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/pciehp.o.gz: init_module: No such device
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/pciehp.o.gz: Hint: insmod errors can be caused by incorrect module parameters, including invalid I.
      You may find more information in syslog or the output from dmesg
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/pciehp.o.gz: insmod pciehp failed
pci_hotplug: PCI Hot Plug PCI Core version: 0.5
shpchp: shpc_init : shpc_cap_offset == 0
shpchp: Standard Hot Plug PCI Controller Driver version: 0.4
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/shpchp.o.gz: init_module: No such device
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/shpchp.o.gz: Hint: insmod errors can be caused by incorrect module parameters, including invalid I.
      You may find more information in syslog or the output from dmesg
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/shpchp.o.gz: insmod shpchp failed
pci_hotplug: PCI Hot Plug PCI Core version: 0.5
pciehp: PCI Express Hot Plug Controller Driver version: 0.5
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/pciehp.o.gz: init_module: No such device
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/pciehp.o.gz: Hint: insmod errors can be caused by incorrect module parameters, including invalid I.
      You may find more information in syslog or the output from dmesg
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/pciehp.o.gz: insmod pciehp failed
pci_hotplug: PCI Hot Plug PCI Core version: 0.5
shpchp: shpc_init : shpc_cap_offset == 0
shpchp: Standard Hot Plug PCI Controller Driver version: 0.4
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/shpchp.o.gz: init_module: No such device
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/shpchp.o.gz: Hint: insmod errors can be caused by incorrect module parameters, including invalid I.
      You may find more information in syslog or the output from dmesg
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/shpchp.o.gz: insmod shpchp failed
pci_hotplug: PCI Hot Plug PCI Core version: 0.5
pciehp: PCI Express Hot Plug Controller Driver version: 0.5
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/pciehp.o.gz: init_module: No such device
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/pciehp.o.gz: Hint: insmod errors can be caused by incorrect module parameters, including invalid I.
      You may find more information in syslog or the output from dmesg
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/pciehp.o.gz: insmod pciehp failed
pci_hotplug: PCI Hot Plug PCI Core version: 0.5
shpchp: shpc_init : shpc_cap_offset == 0
shpchp: Standard Hot Plug PCI Controller Driver version: 0.4
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/shpchp.o.gz: init_module: No such device
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/shpchp.o.gz: Hint: insmod errors can be caused by incorrect module parameters, including invalid I.
      You may find more information in syslog or the output from dmesg
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/shpchp.o.gz: insmod shpchp failed
pci_hotplug: PCI Hot Plug PCI Core version: 0.5
pciehp: PCI Express Hot Plug Controller Driver version: 0.5
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/pciehp.o.gz: init_module: No such device
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/pciehp.o.gz: Hint: insmod errors can be caused by incorrect module parameters, including invalid I.
      You may find more information in syslog or the output from dmesg
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/pciehp.o.gz: insmod pciehp failed
pci_hotplug: PCI Hot Plug PCI Core version: 0.5
shpchp: shpc_init : shpc_cap_offset == 0
shpchp: Standard Hot Plug PCI Controller Driver version: 0.4
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/shpchp.o.gz: init_module: No such device
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/shpchp.o.gz: Hint: insmod errors can be caused by incorrect module parameters, including invalid I.
      You may find more information in syslog or the output from dmesg
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/shpchp.o.gz: insmod shpchp failed
pci_hotplug: PCI Hot Plug PCI Core version: 0.5
pciehp: PCI Express Hot Plug Controller Driver version: 0.5
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/pciehp.o.gz: init_module: No such device
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/pciehp.o.gz: Hint: insmod errors can be caused by incorrect module parameters, including invalid I.
      You may find more information in syslog or the output from dmesg
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/pciehp.o.gz: insmod pciehp failed
pci_hotplug: PCI Hot Plug PCI Core version: 0.5
shpchp: shpc_init : shpc_cap_offset == 0
shpchp: Standard Hot Plug PCI Controller Driver version: 0.4
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/shpchp.o.gz: init_module: No such device
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/shpchp.o.gz: Hint: insmod errors can be caused by incorrect module parameters, including invalid I.
      You may find more information in syslog or the output from dmesg
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/shpchp.o.gz: insmod shpchp failed
pci_hotplug: PCI Hot Plug PCI Core version: 0.5
pciehp: PCI Express Hot Plug Controller Driver version: 0.5
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/pciehp.o.gz: init_module: No such device
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/pciehp.o.gz: Hint: insmod errors can be caused by incorrect module parameters, including invalid I.
      You may find more information in syslog or the output from dmesg
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/pciehp.o.gz: insmod pciehp failed
pci_hotplug: PCI Hot Plug PCI Core version: 0.5
shpchp: shpc_init : shpc_cap_offset == 0
shpchp: Standard Hot Plug PCI Controller Driver version: 0.4
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/shpchp.o.gz: init_module: No such device
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/shpchp.o.gz: Hint: insmod errors can be caused by incorrect module parameters, including invalid I.
      You may find more information in syslog or the output from dmesg
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/shpchp.o.gz: insmod shpchp failed
pci_hotplug: PCI Hot Plug PCI Core version: 0.5
pciehp: PCI Express Hot Plug Controller Driver version: 0.5
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/pciehp.o.gz: init_module: No such device
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/pciehp.o.gz: Hint: insmod errors can be caused by incorrect module parameters, including invalid I.
      You may find more information in syslog or the output from dmesg
insmod: /lib/modules/2.4.33.3/kernel/drivers/hotplug/pciehp.o.gz: insmod pciehp failed
usb.c: registered new driver usbdevfs
usb.c: registered new driver hub
uhci.c: USB Universal Host Controller Interface driver v1.1
usb-uhci.c: $Revision: 1.275 $ time 02:01:41 Sep  1 2006
usb-uhci.c: High bandwidth mode enabled
usb-uhci.c: v1.275:USB Universal Host Controller Interface driver
Updating shared library links:  /sbin/ldconfig


Welcome to Linux 2.4.33.3 (ttyS0)

darkstar login: root
Password: 
Linux 2.4.33.3.
Last login: Fri Dec  3 03:01:35 -0600 1999 on ttyS0.
You have mail.
root@darkstar:~# ls /
bin/   dev/  home/  lost+found/  mnt/   root/  sys/  usr/  vmlinuz
boot/  etc/  lib/   media/       proc/  sbin/  tmp/  var/
root@darkstar:~# 
