#include <stdio.h>
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <sys/io.h>
#include <mach.h>
#include "acpi_shutdown.h"

void disappear_via_acpi(void)
{
  uint16_t i, pm1a_ctl, smi_cmd;
  uint8_t regbuf[2], acpi_en;
  FILE *facp;

  /* Open the ACPI FADT table */
  facp = fopen(SERVERS_ACPI_FADT, "r");
  if (!facp)
    exit(errno);

  /* Grab value to write to SMI_CMD to enable ACPI */
  fseek(facp, SMI_EN_OFFSET, SEEK_SET);
  fread(&acpi_en, 1, 1, facp);

  /* Grab SMI_CMD I/O port */
  fseek(facp, SMI_CMD_OFFSET, SEEK_SET);
  fread(regbuf, 2, 1, facp);
  smi_cmd = (uint16_t)regbuf[0] |
            ((uint16_t)regbuf[1] << 8);

  /* Grab PM1a Control I/O port */
  fseek(facp, PM1A_CTL_OFFSET, SEEK_SET);
  fread(regbuf, 2, 1, facp);
  pm1a_ctl = (uint16_t)regbuf[0] |
             ((uint16_t)regbuf[1] << 8);

  /* Close the ACPI FADT table */
  fclose(facp);

  /* Get I/O permissions */
  if (ioperm(smi_cmd, 2, 1)) {
    fprintf(stderr, "EPERM on ioperm(smi_cmd)\n");
    return;
  }
  if (ioperm(pm1a_ctl, 2, 1)) {
    fprintf(stderr, "EPERM on ioperm(pm1a_ctl)\n");
    return;
  }

  /* Enable ACPI */
  outb(acpi_en, smi_cmd);
  for (i = 0; i < 300; i++)
  {
    if ( (inw(pm1a_ctl) & SCI_EN) == SCI_EN)
      break;
  }

  /* Kill machine */

  /* try sleep state 5 first */
  outw(SLP_TYP5 | SLP_EN, pm1a_ctl);

  /* if we reach here then above did not work */
  outw(SLP_TYP0 | SLP_EN, pm1a_ctl);

  /* Never reached */
}
