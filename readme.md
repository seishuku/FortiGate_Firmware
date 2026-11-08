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
