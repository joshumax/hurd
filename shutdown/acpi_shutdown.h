#ifndef _ACPI_SHUTDOWN_H_
#define _ACPI_SHUTDOWN_H_

#include <hurd/paths.h>

#define _SERVERS_ACPI		_SERVERS	"/acpi/tables"
#define SERVERS_ACPI_FADT	_SERVERS_ACPI	"/FACP"
#define SLP_TYP0	(0x0 << 10)
#define SLP_TYP5	(0x5 << 10)
#define SLP_EN		(0x1 << 13)
#define SCI_EN		1
#define SMI_CMD_OFFSET	12
#define SMI_EN_OFFSET	16
#define PM1A_CTL_OFFSET	28

void disappear_via_acpi(void);

#endif
