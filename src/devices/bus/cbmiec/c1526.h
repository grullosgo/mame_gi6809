// license:BSD-3-Clause
// copyright-holders:Curt Coder
/**********************************************************************

    Commodore 1526/MPS-802/4023 Printer emulation

**********************************************************************/

#ifndef MAME_BUS_CBMIEC_C1626_H
#define MAME_BUS_CBMIEC_C1626_H

#pragma once

#include "cbmiec.h"
#include "bus/ieee488/ieee488.h"
#include "cpu/m6502/m6504.h"



//**************************************************************************
//  TYPE DEFINITIONS
//**************************************************************************

// ======================> c1526_device_base

class c1526_device_base : public device_t
{
protected:
	// construction/destruction
	c1526_device_base(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, uint32_t clock);

	// device-level overrides
	virtual void device_start() override;
	virtual void device_reset() override;
};


// ======================> c1526_device

class c1526_device : public c1526_device_base, public device_cbm_iec_interface
{
public:
	// construction/destruction
	c1526_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

	// optional information overrides
	virtual machine_config_constructor device_mconfig_additions() const override;
	virtual ioport_constructor device_input_ports() const override;
	virtual const tiny_rom_entry *device_rom_region() const override;

protected:
	// device_cbm_iec_interface overrides
	void cbm_iec_atn(int state) override;
	void cbm_iec_data(int state) override;
	void cbm_iec_reset(int state) override;
};


// ======================> c4023_device

class c4023_device : public c1526_device_base, public device_ieee488_interface
{
public:
	// construction/destruction
	c4023_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

	// optional information overrides
	virtual machine_config_constructor device_mconfig_additions() const override;
	virtual ioport_constructor device_input_ports() const override;
	virtual const tiny_rom_entry *device_rom_region() const override;

protected:
	// device_ieee488_interface overrides
	virtual void ieee488_atn(int state) override;
	virtual void ieee488_ifc(int state) override;
};


// device type definition
DECLARE_DEVICE_TYPE(C1526, c1526_device)
DECLARE_DEVICE_TYPE(C4023, c4023_device)
extern const device_type MPS802;


#endif // MAME_BUS_CBMIEC_C1626_H
