/* Linker script to configure memory regions. */
/* the MDB40T I’m using has 256KB (0x40000) Flash and 16KB (0x4000) Ram */

SEARCH_DIR(.)
GROUP(-lgcc -lc -lnosys)

MEMORY
{
  FLASH (rx) : ORIGIN = 0x18000, LENGTH = 0x28000
  RAM (rwx) :  ORIGIN = 0x20002000, LENGTH = 0x6000
}

INCLUDE "gcc_nrf51_common.ld"