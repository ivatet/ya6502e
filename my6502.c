#include <stdint.h>

/* Documentation:
 * 1. https://www.masswerk.at/6502/6502_instruction_set.html
 * 2. https://stackoverflow.com/questions/16913423/why-is-the-initial-state-of-the-interrupt-flag-of-the-6502-a-1
 */

/* Registers. */
uint16_t my_pc;
uint8_t my_ac, my_x, my_y, my_sr, my_sp;

/* NV-BDIZC */
#define SR_FLAG_NEGATIVE  (1 << 7)
#define SR_FLAG_OVERFLOW  (1 << 6)

/* At power-up, the 'unused' bit in the Status Register is hardwired
 * to logic '1' by the internal circuitry of the CPU. It can never be
 * anything other than '1', since it is not controlled by any internal
 * flag or register but is determined by a physical connection to
 * a 'high' signal line. */
#define SR_FLAG_UNUSED    (1 << 5)

#define SR_FLAG_BREAK     (1 << 4)
#define SR_FLAG_DECIMAL   (1 << 3)
#define SR_FLAG_INTERRUPT (1 << 2)
#define SR_FLAG_ZERO      (1 << 1)
#define SR_FLAG_CARRY     (1 << 0)

void my6502_reset(uint16_t pc)
{
	my_pc = pc;
	my_ac = 0;
	my_x = 0;
	my_y = 0;
	my_sp = 0xFD;

	/* FIXME The I flag must be set. It is commented out
	 * to match the reference implementation. */
	my_sr = SR_FLAG_UNUSED /* | SR_FLAG_INTERRUPT */;
}

void my6502_step(void)
{
}
