// license:BSD-3-Clause
// copyright-holders:Guru, Angelo Salese
/**************************************************************************************************

X Tom 3D

Customized i440bx Award based BIOS, "OKSAN MK III /EVATE Ver99.04.20"
"04/20/1999-i440BX-2A69KEIC-00"

TODO:
- DIR texture folder will throw mangled file structure the second time around (when
  ENABLE_VOODOO is 0 and SVGA is used instead). PAM[5] and [6] areas are written to but
  they are locked for write, the flash ROM interface looks good now so it's trying to read
  from conventional memory instead;
- Voodoo Banshee doesn't handle VGA text modes correctly, it will set the screen to 80 x 25
  at POST (making MAME UI host to be unusable) without any drawing.
- pumpit1: video runs at 34 Hz, tons of flickers when song starts;
- Fix EEPROM hookup (i/o $2ac r/w), throws bad in Pump It Up service mode
- Hookup ISA sound board (YMZ280B + YAC516 + 3550A DAC);
- pumpit1: MSCDEX hangs often when Voodoo is disabled;
- pumpit1: flickers a lot during attract mode demo play, may need better host clock;
- Pump it Up: every CD after pumpit1 are really multisession disks, which is unsupported
  by chdman at the time of this writing (and doesn't seem worth converting atm);
- Pump it Up: CAT702 ZN protection for later games;
- MAS 3507D MP3 decoder for pumpito and beyond;

Notes:
- Oksan is the old company name that became Andamiro.
- Pump It Up refs:
  https://github.com/pumpitupdev/pumptools/blob/master/doc/hook/mk3hook.md
  https://github.com/Shizmob/arcade-docs/blob/main/andamiro/board.md#mk3

===================================================================================================

This game runs on PC-based hardware.
Major components are....

MAIN BOARD
----------
    CPU: Intel Celeron (socket 370) 333MHz
Chipset: Intel AGPset FW82443ZX, PCIset FW82371EB
    RAM: Samsung KMM366S823CTS 8M x 64-bit SDRAM DIMM
  Video: 3DFX 500-0013-04 PCB-mounted BGA
         EliteMT M32L1632512A video RAM (x4)
         14.31818MHz XTAL
   BIOS: Atmel 29C010 flash ROM
  Other: Holtek HT6542B i8042-based keyboard controller
         3V coin battery

SOUND BOARD
-----------
A40MX04 QFP84 CPLD
Yamaha YMZ280B + YAC516
16MHz XTAL
PIC12C508 (secured, not read)
Atmel 93C46 EEPROM
LM358 OP AMP (x3)

ROM BOARD
---------
MX29F1610MC 16M FlashROM (x7)

**************************************************************************************************/


#include "emu.h"
#include "cpu/i386/i386.h"
#include "machine/pci.h"
#include "machine/pci-ide.h"
#include "machine/pci-smbus.h"
#include "machine/i82443bx_host.h"
#include "machine/i82371eb_isa.h"
#include "machine/i82371eb_ide.h"
#include "machine/i82371eb_acpi.h"
#include "machine/i82371eb_usb.h"
#include "video/virge_pci.h"
#include "bus/isa/isa.h"
#include "bus/isa/isa_cards.h"
//#include "bus/rs232/hlemouse.h"
//#include "bus/rs232/null_modem.h"
//#include "bus/rs232/rs232.h"
//#include "bus/rs232/sun_kbd.h"
//#include "bus/rs232/terminal.h"
#include "machine/eepromser.h"
#include "machine/mc146818.h"
#include "machine/8042kbdc.h"
#include "sound/cdda.h"
#include "sound/ymz280b.h"
#include "video/voodoo_pci.h"

#include "speaker.h"

#define LOG_FLASH     (1U << 1)

#define VERBOSE (LOG_GENERAL | LOG_FLASH)
//#define LOG_OUTPUT_FUNC osd_printf_warning

#include "logmacro.h"

#define LOGFLASH(...)     LOGMASKED(LOG_FLASH,     __VA_ARGS__)

#define ENABLE_VOODOO 1

/*
 * ISA16 Oksan ROM DISK
 *
 * "OKSAN (R) ROM DISK for MK-III Version 1.00.0305"
 * "Copyright (C) OKSAN Co., Ltd. 1989-1999" (!)
 *
 */

class isa16_oksan_rom_disk : public device_t, public device_isa16_card_interface
{
public:
	// construction/destruction
	isa16_oksan_rom_disk(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

	template <typename T> void set_rom_tag(T &&tag) { m_flash_rom.set_tag(std::forward<T>(tag)); }

protected:
	virtual void device_start() override;
	virtual void device_reset() override;

private:
	required_memory_region m_flash_rom;

	u8 read(offs_t offset);
	void write(offs_t offset, u8 data);

	void remap(int space_id, offs_t start, offs_t end) override;

	u8 m_flash_cmd = 0;
	u32 m_flash_addr = 0;
	bool m_flash_unlock = false;
	u8 m_flash_state = 0;
};

DEFINE_DEVICE_TYPE(ISA16_OKSAN_ROM_DISK, isa16_oksan_rom_disk, "isa16_oksan_rom_disk", "ISA16 Oksan ROM DISK for MK-III")

isa16_oksan_rom_disk::isa16_oksan_rom_disk(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, ISA16_OKSAN_ROM_DISK, tag, owner, clock)
	, device_isa16_card_interface(mconfig, *this)
	, m_flash_rom(*this, finder_base::DUMMY_TAG)
{
}

void isa16_oksan_rom_disk::device_start()
{
	set_isa_device();
}

void isa16_oksan_rom_disk::device_reset()
{
	m_flash_state = 0;
}

void isa16_oksan_rom_disk::remap(int space_id, offs_t start, offs_t end)
{
	if (space_id == AS_IO)
	{
		m_isa->install_device(0x02d0, 0x02df, read8sm_delegate(*this, FUNC(isa16_oksan_rom_disk::read)), write8sm_delegate(*this, FUNC(isa16_oksan_rom_disk::write)));
	}
}

// TODO: quick and dirty MX29F1610MC serial flash ROM implementation
// should really be a serflash_device inheritance ...
u8 isa16_oksan_rom_disk::read(offs_t offset)
{
	//printf("%02x\n", offset);
	if (offset == 0xa || offset == 0xb)
	{
		//if ((m_flash_addr & 0x0fffff) == 0)
		//	printf("%02x %08x %d %d\n", m_flash_cmd, m_flash_addr << 1, m_flash_unlock, m_flash_state);
		if (m_flash_cmd == 0xf0 && m_flash_unlock)
		{
			const u32 flash_size = m_flash_rom->bytes() - 1;
			u8 rom_data = m_flash_rom->base()[((m_flash_addr << 1) + (offset & 1)) & flash_size];
			if (offset & 1 && !machine().side_effects_disabled())
				m_flash_addr ++;

			return rom_data;
		}
	}
	return 0;
}

void isa16_oksan_rom_disk::write(offs_t offset, u8 data)
{
//  if (offset < 8 && ((offset & 1) == 0) && m_flash_cmd == 0xf0)
	//  printf("%04x %04x \n", offset, data);

	switch(offset)
	{
		// address port
		case 0x0:
			m_flash_addr &= 0xffffff00;
			m_flash_addr |= data & 0xff;
			break;
		case 0x2:
			m_flash_addr &= 0xffff00ff;
			m_flash_addr |= (data & 0xff) << 8;
			break;
		case 0x4:
			//if (data)
			//  printf("%02x\n", data);
			m_flash_addr &= 0xff00ffff;
			m_flash_addr |= (data & 0xff) << 16;
			break;
		case 0x6:
			m_flash_addr &= 0x00ffffff;
			m_flash_addr |= (data & 0xff) << 24;
			break;
		// data port
		case 0xa:
		{
			const u16 flash_lower_addr = m_flash_addr & 0xffff;
			LOGFLASH("%02x %04x\n", data, m_flash_addr);
			if (data == 0xaa && flash_lower_addr == 0x5555 && m_flash_state == 0)
			{
				m_flash_state = 1;
			}
			else if (data == 0x55 && flash_lower_addr == 0x2aaa && m_flash_state == 1)
				m_flash_state = 2;
			else if (m_flash_state == 2 && flash_lower_addr == 0x5555)
			{
				m_flash_state = 0;
				m_flash_cmd = data;
				//printf("%02x %08x\n", data, m_flash_addr);
			}
			break;
		}
		// chip enable, 0 -> 1 transitions
		case 0xc:
			m_flash_unlock = bool(BIT(data, 3));
			break;
	}
}

/*
 * ISA16 Oksan I/O & Sound board
 *
 */

class isa16_xtom3d_io_sound : public device_t, public device_isa16_card_interface
{
public:
	// construction/destruction
	isa16_xtom3d_io_sound(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

protected:
	isa16_xtom3d_io_sound(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, uint32_t clock);

	virtual void device_start() override;
	virtual void device_reset() override;

	virtual ioport_constructor device_input_ports() const override;
	virtual void device_add_mconfig(machine_config &config) override;
private:
	required_device<ymz280b_device> m_ymz;
	required_device<eeprom_serial_93cxx_device> m_eeprom;
	required_ioport m_system;
	required_ioport m_in0;
	required_ioport m_in1;
	required_ioport m_in2;

	void remap(int space_id, offs_t start, offs_t end) override;
	void io_map(address_map &map);
};

class isa16_pumpitup_io_sound : public isa16_xtom3d_io_sound
{
public:
	isa16_pumpitup_io_sound(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

protected:
	virtual ioport_constructor device_input_ports() const override;
};

DEFINE_DEVICE_TYPE(ISA16_XTOM3D_IO_SOUND, isa16_xtom3d_io_sound, "isa16_xtom3d_io_sound", "ISA16 X-Tom 3d I/O & Sound board")
DEFINE_DEVICE_TYPE(ISA16_PUMPITUP_IO_SOUND, isa16_pumpitup_io_sound, "isa16_pumpitup_io_sound", "ISA16 Pump It Up I/O & Sound board")

isa16_xtom3d_io_sound::isa16_xtom3d_io_sound(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: isa16_xtom3d_io_sound(mconfig, ISA16_XTOM3D_IO_SOUND, tag, owner, clock)
{
}

isa16_xtom3d_io_sound::isa16_xtom3d_io_sound(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, type, tag, owner, clock)
	, device_isa16_card_interface(mconfig, *this)
	, m_ymz(*this, "ymz")
	, m_eeprom(*this, "eeprom")
	, m_system(*this, "SYSTEM")
	, m_in0(*this, "IN0")
	, m_in1(*this, "IN1")
	, m_in2(*this, "IN2")
{
}

isa16_pumpitup_io_sound::isa16_pumpitup_io_sound(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: isa16_xtom3d_io_sound(mconfig, ISA16_PUMPITUP_IO_SOUND, tag, owner, clock)
{
}


void isa16_xtom3d_io_sound::device_add_mconfig(machine_config &config)
{
	// TODO: may be 8BIT
	EEPROM_93C46_16BIT(config, "eeprom");

	SPEAKER(config, "lspeaker").front_left();
	SPEAKER(config, "rspeaker").front_right();

	YMZ280B(config, m_ymz, XTAL(16'934'400));
	m_ymz->add_route(0, "lspeaker", 0.5);
	m_ymz->add_route(1, "rspeaker", 0.5);
}

static INPUT_PORTS_START(xtom3d)
	PORT_START("SYSTEM")
	PORT_DIPNAME( 0x0001, 0x0001, "SYSTEM" )
	PORT_DIPSETTING(      0x0001, DEF_STR( Off ) )
	PORT_DIPSETTING(      0x0000, DEF_STR( On ) )
	PORT_SERVICE_NO_TOGGLE( 0x02, IP_ACTIVE_LOW )
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_COIN1 )
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_START1 )
	PORT_DIPNAME( 0x0010, 0x0010, DEF_STR( Unknown ) )
	PORT_DIPSETTING(      0x0010, DEF_STR( Off ) )
	PORT_DIPSETTING(      0x0000, DEF_STR( On ) )
	PORT_DIPNAME( 0x0020, 0x0020, DEF_STR( Unknown ) )
	PORT_DIPSETTING(      0x0020, DEF_STR( Off ) )
	PORT_DIPSETTING(      0x0000, DEF_STR( On ) )
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_SERVICE1 )
	PORT_DIPNAME( 0x0080, 0x0080, DEF_STR( Unknown ) )
	PORT_DIPSETTING(      0x0080, DEF_STR( Off ) )
	PORT_DIPSETTING(      0x0000, DEF_STR( On ) )

	PORT_START("IN0")
	PORT_BIT( 0x0001, IP_ACTIVE_LOW, IPT_JOYSTICK_UP ) PORT_PLAYER(1)
	PORT_BIT( 0x0002, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN ) PORT_PLAYER(1)
	PORT_BIT( 0x0004, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT ) PORT_PLAYER(1)
	PORT_BIT( 0x0008, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT ) PORT_PLAYER(1)
	PORT_BIT( 0x0010, IP_ACTIVE_LOW, IPT_BUTTON1 ) PORT_PLAYER(1)
	PORT_BIT( 0x0020, IP_ACTIVE_LOW, IPT_BUTTON2 ) PORT_PLAYER(1)
	PORT_BIT( 0x00c0, IP_ACTIVE_LOW, IPT_UNUSED )

	PORT_START("IN1")
	PORT_BIT( 0x0001, IP_ACTIVE_LOW, IPT_JOYSTICK_UP ) PORT_PLAYER(2)
	PORT_BIT( 0x0002, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN ) PORT_PLAYER(2)
	PORT_BIT( 0x0004, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT ) PORT_PLAYER(2)
	PORT_BIT( 0x0008, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT ) PORT_PLAYER(2)
	PORT_BIT( 0x0010, IP_ACTIVE_LOW, IPT_BUTTON1 ) PORT_PLAYER(2)
	PORT_BIT( 0x0020, IP_ACTIVE_LOW, IPT_BUTTON2 ) PORT_PLAYER(2)
	PORT_BIT( 0x00c0, IP_ACTIVE_LOW, IPT_UNUSED )

	PORT_START("IN2")
	PORT_DIPNAME( 0x0001, 0x0001, "IN2" )
	PORT_DIPSETTING(      0x0001, DEF_STR( Off ) )
	PORT_DIPSETTING(      0x0000, DEF_STR( On ) )
	PORT_DIPNAME( 0x0002, 0x0002, DEF_STR( Unknown ) )
	PORT_DIPSETTING(      0x0002, DEF_STR( Off ) )
	PORT_DIPSETTING(      0x0000, DEF_STR( On ) )
	PORT_DIPNAME( 0x0004, 0x0004, DEF_STR( Unknown ) )
	PORT_DIPSETTING(      0x0004, DEF_STR( Off ) )
	PORT_DIPSETTING(      0x0000, DEF_STR( On ) )
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_START2 )
	PORT_DIPNAME( 0x0010, 0x0010, DEF_STR( Unknown ) )
	PORT_DIPSETTING(      0x0010, DEF_STR( Off ) )
	PORT_DIPSETTING(      0x0000, DEF_STR( On ) )
	PORT_DIPNAME( 0x0020, 0x0020, DEF_STR( Unknown ) )
	PORT_DIPSETTING(      0x0020, DEF_STR( Off ) )
	PORT_DIPSETTING(      0x0000, DEF_STR( On ) )
	PORT_DIPNAME( 0x0040, 0x0040, DEF_STR( Unknown ) )
	PORT_DIPSETTING(      0x0040, DEF_STR( Off ) )
	PORT_DIPSETTING(      0x0000, DEF_STR( On ) )
	PORT_DIPNAME( 0x0080, 0x0080, DEF_STR( Unknown ) )
	PORT_DIPSETTING(      0x0080, DEF_STR( Off ) )
	PORT_DIPSETTING(      0x0000, DEF_STR( On ) )
INPUT_PORTS_END

static INPUT_PORTS_START( pumpitup )
	PORT_INCLUDE( xtom3d )

	PORT_MODIFY("IN0")
	PORT_BIT( 0x0001, IP_ACTIVE_LOW, IPT_BUTTON1 ) PORT_NAME("P1 Top-Left step") PORT_PLAYER(1)
	PORT_BIT( 0x0002, IP_ACTIVE_LOW, IPT_BUTTON2 ) PORT_NAME("P1 Top-Right step") PORT_PLAYER(1)
	PORT_BIT( 0x0004, IP_ACTIVE_LOW, IPT_BUTTON3 ) PORT_NAME("P1 Center step") PORT_PLAYER(1)
	PORT_BIT( 0x0008, IP_ACTIVE_LOW, IPT_BUTTON4 ) PORT_NAME("P1 Bottom-Left step") PORT_PLAYER(1)
	PORT_BIT( 0x0010, IP_ACTIVE_LOW, IPT_BUTTON5 ) PORT_NAME("P1 Bottom-Right step") PORT_PLAYER(1)
	PORT_BIT( 0x00e0, IP_ACTIVE_LOW, IPT_UNUSED )

	PORT_MODIFY("IN1")
	PORT_BIT( 0x0001, IP_ACTIVE_LOW, IPT_BUTTON1 ) PORT_NAME("P2 Top-Left step") PORT_PLAYER(2)
	PORT_BIT( 0x0002, IP_ACTIVE_LOW, IPT_BUTTON2 ) PORT_NAME("P2 Top-Right step") PORT_PLAYER(2)
	PORT_BIT( 0x0004, IP_ACTIVE_LOW, IPT_BUTTON3 ) PORT_NAME("P2 Center step") PORT_PLAYER(2)
	PORT_BIT( 0x0008, IP_ACTIVE_LOW, IPT_BUTTON4 ) PORT_NAME("P2 Bottom-Left step") PORT_PLAYER(2)
	PORT_BIT( 0x0010, IP_ACTIVE_LOW, IPT_BUTTON5 ) PORT_NAME("P2 Bottom-Right step") PORT_PLAYER(2)
	PORT_BIT( 0x00e0, IP_ACTIVE_LOW, IPT_UNUSED )
INPUT_PORTS_END

ioport_constructor isa16_xtom3d_io_sound::device_input_ports() const
{
	return INPUT_PORTS_NAME(xtom3d);
}

ioport_constructor isa16_pumpitup_io_sound::device_input_ports() const
{
	return INPUT_PORTS_NAME(pumpitup);
}


void isa16_xtom3d_io_sound::device_start()
{
	set_isa_device();
}

void isa16_xtom3d_io_sound::device_reset()
{
}

void isa16_xtom3d_io_sound::io_map(address_map &map)
{
	// $2a0-$2a3 sound
	map(0x00, 0x03).rw("ymz", FUNC(ymz280b_device::read), FUNC(ymz280b_device::write)).umask16(0x00ff);
	map(0x08, 0x09).lr8(
		NAME([this] (offs_t offset) {
			return offset & 1 ? m_system->read() : m_in0->read();
		})
	);
	map(0x0a, 0x0b).lr8(
		NAME([this] (offs_t offset) {
			return offset & 1 ? m_in2->read() : m_in1->read();
		})
	);
	map(0x0c, 0x0c).lw8(
		NAME([this] (u8 data) {
			// bit 4: always written, more CS?
			m_eeprom->clk_write(BIT(data, 1) ? ASSERT_LINE : CLEAR_LINE);
			m_eeprom->cs_write(BIT(data, 0) ? ASSERT_LINE : CLEAR_LINE);
			m_eeprom->di_write(BIT(data, 2) ? ASSERT_LINE : CLEAR_LINE);
		})
	);
	map(0x0e, 0x0e).lr8(
		NAME([this] () {
			return m_eeprom->do_read() | 0xfe;
		})
	);
}

void isa16_xtom3d_io_sound::remap(int space_id, offs_t start, offs_t end)
{
	if (space_id == AS_IO)
	{
		m_isa->install_device(0x02a0, 0x02af, *this, &isa16_xtom3d_io_sound::io_map);
	}
}

/*
 * ISA16 Oksan Virtual LPC
 *
 * Doesn't really accesses a Super I/O, which implies that the Holtek keyboard
 * and the RTC chips are motherboard ISA resources.
 *
 */

class isa16_oksan_lpc : public device_t, public device_isa16_card_interface
{
public:
	// construction/destruction
	isa16_oksan_lpc(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

	required_device<mc146818_device> m_rtc;
	required_device<kbdc8042_device> m_kbdc;

protected:
	virtual void device_start() override;
	virtual void device_reset() override;
	virtual void device_add_mconfig(machine_config &config) override;

private:
	void remap(int space_id, offs_t start, offs_t end) override;

	void device_map(address_map &map);
};

DEFINE_DEVICE_TYPE(ISA16_OKSAN_LPC, isa16_oksan_lpc, "isa16_oksan_lpc", "ISA16 Oksan Virtual LPC")

isa16_oksan_lpc::isa16_oksan_lpc(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, ISA16_OKSAN_LPC, tag, owner, clock)
	, device_isa16_card_interface(mconfig, *this)
	, m_rtc(*this, "rtc")
	, m_kbdc(*this, "kbdc")
{
}

void isa16_oksan_lpc::device_add_mconfig(machine_config &config)
{
	MC146818(config, m_rtc, 32.768_kHz_XTAL);
	//m_rtc->irq().set(m_pic8259_2, FUNC(pic8259_device::ir0_w));
	m_rtc->set_century_index(0x32);

	KBDC8042(config, m_kbdc, 0);
	m_kbdc->set_keyboard_type(kbdc8042_device::KBDC8042_STANDARD);
	m_kbdc->system_reset_callback().set_inputline(":maincpu", INPUT_LINE_RESET);
	m_kbdc->gate_a20_callback().set_inputline(":maincpu", INPUT_LINE_A20);
	m_kbdc->input_buffer_full_callback().set(":pci:07.0", FUNC(i82371eb_isa_device::pc_irq1_w));
#if !ENABLE_VOODOO
	m_kbdc->set_keyboard_tag("at_keyboard");

	at_keyboard_device &at_keyb(AT_KEYB(config, "at_keyboard", pc_keyboard_device::KEYBOARD_TYPE::AT, 1));
	at_keyb.keypress().set(m_kbdc, FUNC(kbdc8042_device::keyboard_w));
#endif
}


void isa16_oksan_lpc::device_start()
{
	set_isa_device();
}

void isa16_oksan_lpc::device_reset()
{

}

void isa16_oksan_lpc::remap(int space_id, offs_t start, offs_t end)
{
	if (space_id == AS_IO)
		m_isa->install_device(0x60, 0x7f, *this, &isa16_oksan_lpc::device_map);
}

void isa16_oksan_lpc::device_map(address_map &map)
{
	map(0x00, 0x0f).rw(m_kbdc, FUNC(kbdc8042_device::data_r), FUNC(kbdc8042_device::data_w));
	map(0x10, 0x1f).w(m_rtc, FUNC(mc146818_device::address_w)).umask32(0x00ff00ff);
	map(0x10, 0x1f).rw(m_rtc, FUNC(mc146818_device::data_r), FUNC(mc146818_device::data_w)).umask32(0xff00ff00);
}


namespace {

#define PCI_AGP_ID "pci:01.0:00.0"
#define PCI_IDE_ID "pci:07.1"

class xtom3d_state : public driver_device
{
public:
	xtom3d_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, m_maincpu(*this, "maincpu")
		, m_voodoo(*this, PCI_AGP_ID)
		, m_pci_isa(*this, "pci:07.0")
		, m_pci_ide(*this, PCI_IDE_ID)
	{
	}

	void xtom3d(machine_config &config);
	void pumpitup(machine_config &config);

private:
	required_device<pentium2_device> m_maincpu;
	// TODO: optional for debugging
	optional_device<voodoo_banshee_pci_device> m_voodoo;
	required_device<i82371eb_isa_device> m_pci_isa;
	required_device<i82371eb_ide_device> m_pci_ide;

	void xtom3d_map(address_map &map);
//  void xtom3d_io(address_map &map);

//  void vblank_assert(int state);

	static void romdisk_config(device_t *device);
//  static void cdrom_config(device_t *device);
};


void xtom3d_state::xtom3d_map(address_map &map)
{
	map.unmap_value_high();
}

void xtom3d_isa_cards(device_slot_interface &device)
{
	device.option_add_internal("oksan_romdisk", ISA16_OKSAN_ROM_DISK);
	device.option_add_internal("oksan_lpc", ISA16_OKSAN_LPC);
	device.option_add_internal("xtom3d_io_sound", ISA16_XTOM3D_IO_SOUND);
	device.option_add_internal("pumpitup_io_sound", ISA16_PUMPITUP_IO_SOUND);
}

void xtom3d_state::romdisk_config(device_t *device)
{
	isa16_oksan_rom_disk &romdisk = *downcast<isa16_oksan_rom_disk *>(device);
	romdisk.set_rom_tag("game_rom");
}

// TODO: stub for drive options (speed/drive type etc.)
/*void xtom3d_state::cdrom_config(device_t *device)
{
}
*/

// TODO: unverified PCI config space
void xtom3d_state::xtom3d(machine_config &config)
{
	PENTIUM2(config, m_maincpu, XTAL(33'868'800) * 2); // actually Celeron Socket 370 x10, x2 is (roughly) for AGP bottleneck
	m_maincpu->set_addrmap(AS_PROGRAM, &xtom3d_state::xtom3d_map);
//  m_maincpu->set_addrmap(AS_IO, &xtom3d_state::xtom3d_io);
	m_maincpu->set_irq_acknowledge_callback("pci:07.0:pic8259_master", FUNC(pic8259_device::inta_cb));
	m_maincpu->smiact().set("pci:00.0", FUNC(i82443bx_host_device::smi_act_w));

	PCI_ROOT(config, "pci", 0);
	// PCB has ZX marking but BIOS returns BX, shouldn't matter
	I82443BX_HOST(config, "pci:00.0", 0, "maincpu", 128*1024*1024);
	I82443BX_BRIDGE(config, "pci:01.0", 0 ); //"pci:01.0:00.0");
	//I82443BX_AGP   (config, "pci:01.0:00.0");

	i82371eb_isa_device &isa(I82371EB_ISA(config, "pci:07.0", 0, m_maincpu));
	isa.boot_state_hook().set([](u8 data) { /* printf("%02x\n", data); */ });
	isa.smi().set_inputline("maincpu", INPUT_LINE_SMI);

	I82371EB_IDE(config, m_pci_ide, 0, m_maincpu);
	m_pci_ide->irq_pri().set("pci:07.0", FUNC(i82371eb_isa_device::pc_irq14_w));
	m_pci_ide->irq_sec().set("pci:07.0", FUNC(i82371eb_isa_device::pc_mirq0_w));

	m_pci_ide->subdevice<bus_master_ide_controller_device>("ide1")->slot(0).set_default_option(nullptr);
//  ide.subdevice<bus_master_ide_controller_device>("ide1")->slot(0).set_fixed(true);

	m_pci_ide->subdevice<bus_master_ide_controller_device>("ide2")->slot(0).set_default_option(nullptr);

	I82371EB_USB (config, "pci:07.2", 0);
	I82371EB_ACPI(config, "pci:07.3", 0);
	LPC_ACPI     (config, "pci:07.3:acpi", 0);
	SMBUS        (config, "pci:07.3:smbus", 0);

	ISA16_SLOT(config, "board1", 0, "pci:07.0:isabus", xtom3d_isa_cards, "oksan_romdisk", true).set_option_machine_config("oksan_romdisk", romdisk_config);
	ISA16_SLOT(config, "board2", 0, "pci:07.0:isabus", xtom3d_isa_cards, "oksan_lpc", true);
	ISA16_SLOT(config, "isa1", 0, "pci:07.0:isabus", xtom3d_isa_cards, "xtom3d_io_sound", true);
	ISA16_SLOT(config, "isa2", 0, "pci:07.0:isabus", pc_isa16_cards, nullptr, false);

	// Expansion slots, mapping SVGA for debugging
#if ENABLE_VOODOO
	VOODOO_BANSHEE_X86_PCI(config, m_voodoo, 0, m_maincpu, "screen"); // "pci:0d.0" J4D2
	// TODO: confirm values
	m_voodoo->set_fbmem(16);
	// NOTE: pumpit1 touches this a lot
	m_voodoo->set_status_cycles(1000);
	// TODO: check me, probably unconnected
	subdevice<generic_voodoo_device>(PCI_AGP_ID":voodoo")->vblank_callback().set("pci:07.0", FUNC(i82371eb_isa_device::pc_irq5_w));

	// TODO: fix legacy raw setup here
	screen_device &screen(SCREEN(config, "screen", SCREEN_TYPE_RASTER));
	screen.set_refresh_hz(57);
	screen.set_size(640, 480);
	screen.set_visarea(0, 640 - 1, 0, 480 - 1);
	screen.set_screen_update(PCI_AGP_ID, FUNC(voodoo_banshee_pci_device::screen_update));
#else
	VIRGE_PCI(config, "pci:0e.0", 0); // J4C1
#endif
	// "pci:0d.0" J4D2
	// "pci:0e.0" J4D1
}

static void cdrom_config(device_t *device)
{
	device->subdevice<cdda_device>("cdda")->add_route(0, ":lmicrophone", 0.25);
	device->subdevice<cdda_device>("cdda")->add_route(1, ":rmicrophone", 0.25);
}

void xtom3d_state::pumpitup(machine_config &config)
{
	xtom3d(config);

	SPEAKER(config, "lmicrophone").front_left();
	SPEAKER(config, "rmicrophone").front_right();

	m_pci_ide->subdevice<bus_master_ide_controller_device>("ide1")->slot(0).set_default_option("cdrom");
	m_pci_ide->subdevice<bus_master_ide_controller_device>("ide1")->slot(0).set_option_machine_config("cdrom", cdrom_config);

	ISA16_SLOT(config.replace(), "isa1", 0, "pci:07.0:isabus", xtom3d_isa_cards, "pumpitup_io_sound", true);
}

ROM_START( xtom3d )
	ROM_REGION32_LE(0x20000, "pci:07.0", 0)
	ROM_LOAD( "bios.u22", 0x000000, 0x020000, CRC(f7c58044) SHA1(fd967d009e0d3c8ed9dd7be852946f2b9dee7671) )

	ROM_REGION32_LE(0x1000000, "board1:game_rom", ROMREGION_ERASEFF )
	ROM_LOAD( "u3",  0x000000, 0x200000, CRC(f332e030) SHA1(f04fc7fc97e6ada8122ea7d111455043d7cc42df) )
	ROM_LOAD( "u4",  0x200000, 0x200000, CRC(ac40ea0b) SHA1(6fcb86f493885d62d20df6bddaa1a1b19d478c65) )
	ROM_LOAD( "u5",  0x400000, 0x200000, CRC(0fb98a20) SHA1(d21f33b0ca65dc6f90a411a9682f960e9c60244c) )
	ROM_LOAD( "u6",  0x600000, 0x200000, CRC(5c092c58) SHA1(d347e1ed957cc989dc71f4f347af926589ae926d) )
	ROM_LOAD( "u7",  0x800000, 0x200000, CRC(833c179c) SHA1(586555f5a4066a762fc05a43ef01be9fa202bb7f) )

	ROM_REGION(0x400000, "isa1:xtom3d_io_sound:ymz", ROMREGION_ERASEFF )
	ROM_LOAD( "u19", 0x000000, 0x200000, CRC(a1ae73d0) SHA1(232c73bfee426b5f651a015c505c26b8ed7176b7) )
	ROM_LOAD( "u20", 0x200000, 0x200000, CRC(452131d9) SHA1(f62a0f1a7da9025ac1f7d5de4df90166871ac1e5) )
ROM_END

// provided dump is half size and definitely don't seem sane,
// just assume they didn't change that part
//  ROM_LOAD( "bios.u22", 0x000000, 0x010000, BAD_DUMP CRC(574bb327) SHA1(c24484e9b304b9d570c5ead6be768f563d5c389f) )

#define PUMPITUP_BIOS \
	ROM_REGION32_LE(0x20000, "pci:07.0", 0) \
	ROM_LOAD( "bios.u22", 0x000000, 0x020000, CRC(f7c58044) SHA1(fd967d009e0d3c8ed9dd7be852946f2b9dee7671) ) \
	ROM_REGION32_LE(0x1000000, "board1:game_rom", ROMREGION_ERASEFF ) \
	ROM_LOAD( "piu10.u8",  0x000000, 0x200000, CRC(5911e31a)  SHA1(295723b9b7da9e55b5dd5586b23b06355f4837ef) ) \
	ROM_REGION(0x400000, "isa1:pumpitup_io_sound:ymz", ROMREGION_ERASEFF ) \
	ROM_LOAD( "piu10.u9",  0x000000, 0x200000, CRC(9c436cfa) SHA1(480ea52e74721d1963ced41be5c482b7b913ccd2) )

ROM_START( pumpitup )
	PUMPITUP_BIOS
ROM_END

ROM_START( pumpit1 )
	PUMPITUP_BIOS

	DISK_REGION( PCI_IDE_ID":ide1:0:cdrom" )
	DISK_IMAGE_READONLY( "19990930", 0,  SHA1(a848061806c56ba30c75a24233300f175fb3eb9d) )
ROM_END

} // anonymous namespace

GAME(1999, xtom3d, 0, xtom3d, 0, xtom3d_state, empty_init, ROT0, "Andamiro / Jamie System Development", "X Tom 3D", MACHINE_NOT_WORKING | MACHINE_NO_SOUND )
GAME(1999, pumpitup, 0,        pumpitup, 0, xtom3d_state, empty_init, ROT0, "Andamiro", "Pump It Up BIOS", MACHINE_NOT_WORKING | MACHINE_NO_SOUND | MACHINE_IS_BIOS_ROOT )
GAME(1999, pumpit1,  pumpitup, pumpitup, 0, xtom3d_state, empty_init, ROT0, "Andamiro", "Pump It Up: The 1st Dance Floor (ver 0.53.1999.9.31)", MACHINE_NOT_WORKING | MACHINE_NO_SOUND )
//GAME(1999, pumpit2,  pumpitup, pumpitup, 0, xtom3d_state, empty_init, ROT0, "Andamiro", "Pump it Up: The 2nd Dance Floor", MACHINE_NOT_WORKING | MACHINE_NO_SOUND )
//GAME(1999, pumpit3,  pumpitup, pumpitup, 0, xtom3d_state, empty_init, ROT0, "Andamiro", "Pump it Up The O.B.G: The 3rd Dance Floor", MACHINE_NOT_WORKING | MACHINE_NO_SOUND )
// GAME(2000, pumpito,  pumpitup, pumpitup, 0, xtom3d_state, empty_init, ROT0, "Andamiro", "Pump it Up The O.B.G: The Season Evolution Dance Floor", MACHINE_NOT_WORKING | MACHINE_NO_SOUND )
// GAME(2000, pumpitc,  pumpitup, pumpitup, 0, xtom3d_state, empty_init, ROT0, "Andamiro", "Pump it Up: The Collection", MACHINE_NOT_WORKING | MACHINE_NO_SOUND )
// GAME(2000, pumpitpc, pumpitup, pumpitup, 0, xtom3d_state, empty_init, ROT0, "Andamiro", "Pump it Up: The Perfect Collection", MACHINE_NOT_WORKING | MACHINE_NO_SOUND )
// GAME(2001, pumpite,  pumpitup, pumpitup, 0, xtom3d_state, empty_init, ROT0, "Andamiro", "Pump it Up Extra", MACHINE_NOT_WORKING | MACHINE_NO_SOUND )
// GAME(2001, pumpitpr, pumpitup, pumpitup, 0, xtom3d_state, empty_init, ROT0, "Andamiro","Pump it Up The Premiere: The International Dance Floor", MACHINE_NOT_WORKING | MACHINE_NO_SOUND )
// GAME(2001, pumpitpx, pumpitup, pumpitup, 0, xtom3d_state, empty_init, ROT0, "Andamiro", "Pump it Up The PREX: The International Dance Floor", MACHINE_NOT_WORKING | MACHINE_NO_SOUND )
// GAME(2002, pumpit8,  pumpitup, pumpitup, 0, xtom3d_state, empty_init, ROT0, "Andamiro", "Pump it Up The Rebirth: The 8th Dance Floor", MACHINE_NOT_WORKING | MACHINE_NO_SOUND )
// GAME(2002, pumpitp2, pumpitup, pumpitup, 0, xtom3d_state, empty_init, ROT0, "Andamiro", "Pump it Up The Premiere 2: The International 2nd Dance Floor", MACHINE_NOT_WORKING | MACHINE_NO_SOUND )
// GAME(2002, pumpipx2, pumpitup, pumpitup, 0, xtom3d_state, empty_init, ROT0, "Andamiro", "Pump it Up The PREX 2", MACHINE_NOT_WORKING | MACHINE_NO_SOUND )
// GAME(2003, pumpitp3, pumpitup, pumpitup, 0, xtom3d_state, empty_init, ROT0, "Andamiro", "Pump it Up The Premiere 3: The International 3rd Dance Floor", MACHINE_NOT_WORKING | MACHINE_NO_SOUND )
// GAME(2003, pumpipx3, pumpitup, pumpitup, 0, xtom3d_state, empty_init, ROT0, "Andamiro", "Pump it Up The PREX 3: The International 4th Dance Floor", MACHINE_NOT_WORKING | MACHINE_NO_SOUND )

// GAME(1999, "family production,inc", "N3 Heartbreakers Advanced" known to exist on this HW
// https://namu.wiki/w/%ED%95%98%ED%8A%B8%20%EB%B8%8C%EB%A0%88%EC%9D%B4%EC%BB%A4%EC%A6%88
// (Korean encoded URL)
