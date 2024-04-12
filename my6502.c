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

static void my_update_sr_with_carry(uint8_t reg_value, uint8_t flags,
                                    uint8_t carry_value)
{
	my_update_sr(reg_value, flags);
	if (carry_value) {
		SR_SET(SR_FLAG_CARRY);
	} else {
		SR_CLR(SR_FLAG_CARRY);
	}
}

/* Addressing modes. */
enum my_addr {
	IMMEDIATE,
	ABSOLUTE,
	RELATIVE,
};

static uint16_t my_read_addr(enum my_addr mode)
{
	uint16_t addr;

	switch (mode) {
	case ABSOLUTE:
		addr = my6502_read(my_pc++);
		addr |= (my6502_read(my_pc++) << 8);
		return addr;
	case RELATIVE:
		addr = my6502_read(my_pc++);
		return my_pc + (int8_t)addr;
	default:
		assert(0);
		break;
	}
}

static uint8_t my_read_op(enum my_addr mode)
{
	switch (mode) {
	case ABSOLUTE:
		return my6502_read(my_read_addr(mode));
	case IMMEDIATE:
		return my6502_read(my_pc++);
	default:
		assert(0);
		break;
	}
}

/* Add Memory to Accumulator with Carry. */
static void my_adc(uint8_t value)
{
	uint16_t result;
	result = (uint16_t)my_ac + (uint16_t)value;
	if (SR_IS_SET(SR_FLAG_CARRY)) {
		result++;
	}

	if (result >= 0x100) {
		SR_SET(SR_FLAG_CARRY);
	} else {
		SR_CLR(SR_FLAG_CARRY);
	}

	/* It loses the higher byte, but it is OK because we have
	 * already computed the carry flag to accommodate it. */
	my_ac = result;
}

/* Branch on Result not Zero. */
static void my_bne(uint16_t addr)
{
	if (!SR_IS_SET(SR_FLAG_ZERO)) {
		my_pc = addr;
	}
}

/* Branch on Result Zero. */
static void my_beq(uint16_t addr)
{
	if (SR_IS_SET(SR_FLAG_ZERO)) {
		my_pc = addr;
	}
}

/* Branch on Result Plus. */
static void my_bpl(uint16_t addr)
{
	if (!SR_IS_SET(SR_FLAG_NEGATIVE)) {
		my_pc = addr;
	}
}

/* Clear Carry Flag. */
static void my_clc(void)
{
	my_sr &= ~SR_FLAG_CARRY;
}

/* Clear Decimal Mode. */
static void my_cld(void)
{
	my_sr &= ~SR_FLAG_DECIMAL;
}

static void my_cmp(uint8_t value)
{
	my_update_sr_with_carry(my_ac - value, SR_FLAG_NEGATIVE | SR_FLAG_ZERO,
	                        my_ac >= value);
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

static void my_jmp(uint16_t addr)
{
	my_pc = addr;
}

/* Load Accumulator with Memory */
static void my_lda(uint8_t value)
{
	my_ac = value;
	my_update_sr(my_ac, SR_FLAG_NEGATIVE | SR_FLAG_ZERO);
}

static void my_ldx(uint8_t value)
{
	my_x = value;
	my_update_sr(my_x, SR_FLAG_NEGATIVE | SR_FLAG_ZERO);
}

static void my_ldy(uint8_t value)
{
	my_y = value;
	my_update_sr(my_y, SR_FLAG_NEGATIVE | SR_FLAG_ZERO);
}

static void my_sta(uint16_t addr)
{
	my6502_write(addr, my_ac);
}

/* Transfer Accumulator to Index X. */
static void my_tax(void)
{
	my_x = my_ac;
	my_update_sr(my_x, SR_FLAG_NEGATIVE | SR_FLAG_ZERO);
}

/* Transfer Index X to Stack Register. */
static void my_txs(void)
{
	my_sp = my_x;
}

/* Transfer Index Y to Accumulator. */
static void my_tya(void)
{
	my_ac = my_y;
	my_update_sr(my_ac, SR_FLAG_NEGATIVE | SR_FLAG_ZERO);
}

void my6502_step(void)
{
	uint8_t opcode = my6502_read(my_pc++);
	switch (opcode) {
	case 0x10:
		my_bpl(my_read_addr(RELATIVE));
		break;
	case 0x18:
		my_clc();
		break;
	case 0x4C:
		my_jmp(my_read_addr(ABSOLUTE));
		break;
	case 0x69:
		my_adc(my_read_op(IMMEDIATE));
		break;
	case 0x88:
		my_dey();
		break;
	case 0x8D:
		my_sta(my_read_addr(ABSOLUTE));
		break;
	case 0x98:
		my_tya();
		break;
	case 0x9A:
		my_txs();
		break;
	case 0xA0:
		my_ldy(my_read_op(IMMEDIATE));
		break;
	case 0xA2:
		my_ldx(my_read_op(IMMEDIATE));
		break;
	case 0xA9:
		my_lda(my_read_op(IMMEDIATE));
		break;
	case 0xAA:
		my_tax();
		break;
	case 0xAD:
		my_lda(my_read_op(ABSOLUTE));
		break;
	case 0xC9:
		my_cmp(my_read_op(IMMEDIATE));
		break;
	case 0xCA:
		my_dex();
		break;
	case 0xD0:
		my_bne(my_read_addr(RELATIVE));
		break;
	case 0xD8:
		my_cld();
		break;
	case 0xEA:
		/* NOP */
		break;
	case 0xF0:
		my_beq(my_read_addr(RELATIVE));
		break;
	default:
		assert(0);
		break;
	}
}
