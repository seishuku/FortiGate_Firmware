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

## Update
I've added some basic (crude?) polling IDE interface code, as well as an MBR parser and some code to probe an EXT2 file system.<br>
Let's see if I can load a payload myself from an ext2 FS.<br><br>

## Update
Looks like reading from EXT2 is working well, read in a test payload elf from other attempts and it's executing that now... So in theory I should just be able to load a Linux kernel and initrd and it'll "Just work"?

## Update
Well, that wasn't nearly as bad as I though it was going to be! I have a live kernel booting now.<br>
This project is now more or less done, aside from a little tweaking here and whatever might pop up with getting a real rootfs on there.<br>
Next step will probably be swapping out the 32MB CF card with one of the 1GB cards I have and get a full root FS on there.

## Current console output (this is from real hardware):
```FGT-60 early init
Stage 1: VT8231/UART up, VT8601 + SDRAM init...
Found 128MB

Stage 2:
C runtime: .bss zeroed, .data intact, stack ok
Total RAM: 00000080 MB (from as reported by northbridge)
Heap initialized successfully.
Heap available: 07effff0 bytes, largest free block: 07effff0 bytes
IDE device: present, LBA28, SanDisk SDCFB-32, 0000f500 sectors

Reading MBR...
MBR signature valid.
Primary partitions:
  Partition 00000001: type=00000083 start=00000800 sectors=0000ed00 status=00000080

Reading EXT superblock...
EXT filesystem mounted.

Reading root inode...
Root directory size: 00000400

Root directory entries:
  inode=00000002 type=00000002 name=.
  inode=00000002 type=00000002 name=..
  inode=0000000b type=00000002 name=lost+found
  inode=0000000c type=00000001 name=hello.elf
  inode=0000000d type=00000001 name=vmlinuz
  inode=0000000e type=00000001 name=initrd
Directory listing complete.
Linux version 2.4.33.3 (root@tree) (gcc version 3.4.6) #6 Fri Sep 1 04:15:45 CDT 2006
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
Kernel command line: console=ttyS0,115200
Initializing CPU#0
Detected 400.916 MHz processor.
Console: colour dummy device 80x25
Calibrating delay loop... 799.53 BogoMIPS
Memory: 125544k/131072k available (1867k kernel code, 4504k reserved, 562k data, 120k init, 0k highmem)
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
Floppy drive(s): fd0 is unknown type 14 (usb?), fd1 is unknown type 15 (usb?)
floppy0: no floppy controllers found
RAMDISK driver initialized: 16 RAM disks of 7777K size 1024 blocksize
loop: loaded (max 8 devices)
Uniform Multi-Platform E-IDE driver Revision: 7.00beta4-2.4
ide: Assuming 33MHz system bus speed for PIO modes; override with idebus=xx
VP_IDE: IDE controller at PCI slot 00:11.1
VP_IDE: chipset revision 6
VP_IDE: not 100% native mode: will probe irqs later
ide: Assuming 33MHz system bus speed for PIO modes; override with idebus=xx
VP_IDE: VIA vt8231 (rev 10) IDE UDMA100 controller on pci00:11.1
    ide0: BM-DMA at 0xcc00-0xcc07, BIOS settings: hda:pio, hdb:pio
    ide1: BM-DMA at 0xcc08-0xcc0f, BIOS settings: hdc:pio, hdd:pio
hda: C/H/S=0/0/0 from BIOS ignored
hdb: C/H/S=0/0/0 from BIOS ignored
hda: SanDisk SDCFB-32, CFA DISK drive
ide0 at 0x1f0-0x1f7,0x3f6 on irq 14
hda: attached ide-disk driver.
hda: 62720 sectors (32 MB) w/1KiB Cache, CHS=490/4/32
Partition check:
 hda: hda1
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
   32regs    :   177.200 MB/sec
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
 hda: hda1
 hda: hda1
hda7: bad access: block=2, count=2
end_request: I/O error, dev 03:07 (hda), sector 2
EXT3-fs: unable to read superblock
 hda: hda1
 hda: hda1
hda7: bad access: block=2, count=2
end_request: I/O error, dev 03:07 (hda), sector 2
EXT2-fs: unable to read superblock
 hda: hda1
 hda: hda1
hda7: bad access: block=0, count=1
end_request: I/O error, dev 03:07 (hda), sector 0
FAT: unable to read boot sector
UMSDOS: msdos_read_super failed, mount aborted.
 hda: hda1
 hda: hda1
hda7: bad access: block=0, count=1
end_request: I/O error, dev 03:07 (hda), sector 0
FAT: unable to read boot sector
 hda: hda1
 hda: hda1
hda7: bad access: block=0, count=1
end_request: I/O error, dev 03:07 (hda), sector 0
FAT: unable to read boot sector
 hda: hda1
 hda: hda1
hda7: bad access: block=64, count=2
end_request: I/O error, dev 03:07 (hda), sector 64
isofs_read_super: bread failed, dev=03:07, iso_blknum=16, block=32
 hda: hda1
 hda: hda1
hda7: bad access: block=16, count=2
end_request: I/O error, dev 03:07 (hda), sector 16
sh-2006: reiserfs read_super_block: bread failed (dev 03:07, block 8, size 1024)
hda7: bad access: block=128, count=2
end_request: I/O error, dev 03:07 (hda), sector 128
sh-2006: reiserfs read_super_block: bread failed (dev 03:07, block 64, size 1024)
sh-2021: reiserfs_read_super: can not find reiserfs on ide0(3,7)
Kernel panic: VFS: Unable to mount root fs on 03:07
