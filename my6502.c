#include <assert.h>
#include <stdint.h>

/* Documentation:
 * 1. https://www.masswerk.at/6502/6502_instruction_set.html
 * 2. https://stackoverflow.com/questions/16913423/why-is-the-initial-state-of-the-interrupt-flag-of-the-6502-a-1
 */

/* Forward declarations to be provided by the user. */
uint8_t my6502_read(uint16_t address);
void my6502_write(uint16_t address, uint8_t value);

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

/* Status register helpers. */
#define SR_CLR(bit)       (my_sr &= ~(bit))
#define SR_SET(bit)       (my_sr |= (bit))

#define SR_IS_SET(bit)    (my_sr & (bit))

/* Addressing modes. */
enum my_addr {
	IMMEDIATE,
	ABSOLUTE,
};

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

static void my_update_sr(uint8_t value, uint8_t flags)
{
	if (flags & SR_FLAG_NEGATIVE) {
		if (value & 0x80) {
			SR_SET(SR_FLAG_NEGATIVE);
		} else {
			SR_CLR(SR_FLAG_NEGATIVE);
		}
	}

	if (flags & SR_FLAG_ZERO) {
		if (!value) {
			SR_SET(SR_FLAG_ZERO);
		} else {
			SR_CLR(SR_FLAG_ZERO);
		}
	}
}

/* Branch on Result not Zero. */
static void my_bne(void)
{
	if (!SR_IS_SET(SR_FLAG_ZERO)) {
		uint8_t offset = my6502_read(my_pc++);
		my_pc = my_pc + (int8_t)offset;
	}
}

/* Branch on Result Zero. */
static void my_beq(void)
{
	if (SR_IS_SET(SR_FLAG_ZERO)) {
		uint8_t offset = my6502_read(my_pc++);
		my_pc = my_pc + (int8_t)offset;
	}
}

/* Clear Decimal Mode. */
static void my_cld(void)
{
	my_sr &= ~SR_FLAG_DECIMAL;
}

/* Decrement Index X by One */
static void my_dex(void)
{
	my_update_sr(--my_x, SR_FLAG_NEGATIVE | SR_FLAG_ZERO);
}

/* Decrement Index Y by One */
static void my_dey(void)
{
	my_update_sr(--my_y, SR_FLAG_NEGATIVE | SR_FLAG_ZERO);
}

static void my_jmp(enum my_addr mode)
{
	uint16_t addr;

	switch (mode) {
	case ABSOLUTE:
		addr = my6502_read(my_pc++);
		addr |= (my6502_read(my_pc++) << 8);
		my_pc = addr;
		break;
	default:
		assert(0);
		break;
	}
}

/* Load Accumulator with Memory */
static void my_lda(enum my_addr mode)
{
	switch (mode) {
	case IMMEDIATE:
		my_ac = my6502_read(my_pc++);
		my_update_sr(my_ac, SR_FLAG_NEGATIVE | SR_FLAG_ZERO);
		break;
	default:
		assert(0);
		break;
	}
}

static void my_ld_index(uint8_t *reg, enum my_addr mode)
{
	assert(reg);

	switch (mode) {
	case IMMEDIATE:
		*reg = my6502_read(my_pc++);
		my_update_sr(*reg, SR_FLAG_NEGATIVE | SR_FLAG_ZERO);
		break;
	default:
		assert(0);
		break;
	}
}

static void my_sta(enum my_addr mode)
{
	uint16_t addr;

	switch (mode) {
	case ABSOLUTE:
		addr = my6502_read(my_pc++);
		addr |= (my6502_read(my_pc++) << 8);
		my6502_write(addr, my_ac);
		break;
	default:
		assert(0);
		break;
	}
}

/* Transfer Index X to Stack Register. */
static void my_txs(void)
{
	my_sp = my_x;
}

void my6502_step(void)
{
	uint8_t opcode = my6502_read(my_pc++);
	switch (opcode) {
	case 0x4C:
		my_jmp(ABSOLUTE);
		break;
	case 0x88:
		my_dey();
		break;
	case 0x8D:
		my_sta(ABSOLUTE);
		break;
	case 0x9A:
		my_txs();
		break;
	case 0xA0:
		my_ld_index(&my_y, IMMEDIATE); /* ldy */
		break;
	case 0xA2:
		my_ld_index(&my_x, IMMEDIATE); /* ldx */
		break;
	case 0xA9:
		my_lda(IMMEDIATE);
		break;
	case 0xCA:
		my_dex();
		break;
	case 0xD0:
		my_bne();
		break;
	case 0xD8:
		my_cld();
		break;
	case 0xF0:
		my_beq();
		break;
	}
}
