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

/* Memory layout. */
#define STACK_OFFSET      0x100
#define IRQ_OFFSET        0xFFFE

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

static void my_push(uint8_t value)
{
	my6502_write(STACK_OFFSET + my_sp--, value);
}

static uint8_t my_pop(void)
{
	return my6502_read(STACK_OFFSET + ++my_sp);
}

/* Addressing modes. */
enum my_addr {
	IMMEDIATE,
	ABSOLUTE,
	ABSOLUTE_X,
	ABSOLUTE_Y,
	RELATIVE,
	INDIRECT,
	INDIRECT_Y,
	ZEROPAGE,
	ZEROPAGE_X,
	ZEROPAGE_Y,
};

static uint16_t my_read_addr_from_mem(uint16_t *reg)
{
	uint16_t addr;

	addr = my6502_read((*reg)++);
	addr |= (my6502_read((*reg)++) << 8);

	return addr;
}

static uint16_t my_read_addr(enum my_addr mode)
{
	uint16_t addr;

	switch (mode) {
	case ABSOLUTE:
		return my_read_addr_from_mem(&my_pc);
	case ABSOLUTE_X:
		return my_read_addr_from_mem(&my_pc) + my_x;
	case ABSOLUTE_Y:
		return my_read_addr_from_mem(&my_pc) + my_y;
	case RELATIVE:
		addr = my6502_read(my_pc++);
		return my_pc + (int8_t)addr;
	case INDIRECT:
		addr = my_read_addr_from_mem(&my_pc);
		return my_read_addr_from_mem(&addr);
	case INDIRECT_Y:
		addr = my6502_read(my_pc++);
		return my_read_addr_from_mem(&addr) + my_y;
	case ZEROPAGE:
		return my6502_read(my_pc++);
	case ZEROPAGE_X:
		/* Wrap around without penalty for crossing page boundaries. */
		return (my6502_read(my_pc++) + my_x) & 0xFF;
	case ZEROPAGE_Y:
		return (my6502_read(my_pc++) + my_y) & 0xFF;
	default:
		assert(0);
		break;
	}
}

static uint8_t my_read_op(enum my_addr mode)
{
	switch (mode) {
	case ABSOLUTE:
	case ABSOLUTE_X:
	case ABSOLUTE_Y:
	case INDIRECT_Y:
	case ZEROPAGE:
	case ZEROPAGE_X:
	case ZEROPAGE_Y:
		return my6502_read(my_read_addr(mode));
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

/* Branch on Carry Set. */
static void my_bcs(uint16_t addr)
{
	if (SR_IS_SET(SR_FLAG_CARRY)) {
		my_pc = addr;
	}
}

/* Branch on Result not Zero. */
static void my_bne(uint16_t addr)
{
	if (!SR_IS_SET(SR_FLAG_ZERO)) {
		my_pc = addr;
	}
}

/* Branch on Carry Clear. */
static void my_bcc(uint16_t addr)
{
	if (!SR_IS_SET(SR_FLAG_CARRY)) {
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

static void my_bmi(uint16_t addr)
{
	if (SR_IS_SET(SR_FLAG_NEGATIVE)) {
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

/* Force Break. */
static void my_brk(void)
{
	uint16_t return_addr = my_pc + 1;

	my_push(return_addr >> 8);
	my_push(return_addr);
	my_push(my_sr | SR_FLAG_BREAK);

	my_pc = my6502_read(IRQ_OFFSET);
	my_pc |= my6502_read(IRQ_OFFSET + 1) << 8;

	my_sr |= SR_FLAG_INTERRUPT;
}

static void my_bvc(uint16_t addr)
{
	if (!SR_IS_SET(SR_FLAG_OVERFLOW)) {
		my_pc = addr;
	}
}

static void my_bvs(uint16_t addr)
{
	if (SR_IS_SET(SR_FLAG_OVERFLOW)) {
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

static void my_cli(void)
{
	my_sr &= ~SR_FLAG_INTERRUPT;
}

static void my_clv(void)
{
	my_sr &= ~SR_FLAG_OVERFLOW;
}

static void my_cmp(uint8_t value)
{
	my_update_sr_with_carry(my_ac - value, SR_FLAG_NEGATIVE | SR_FLAG_ZERO,
	                        my_ac >= value);
}

static void my_cpx(uint8_t value)
{
	my_update_sr_with_carry(my_x - value, SR_FLAG_NEGATIVE | SR_FLAG_ZERO,
	                        my_x >= value);
}

static void my_cpy(uint8_t value)
{
	my_update_sr_with_carry(my_y - value, SR_FLAG_NEGATIVE | SR_FLAG_ZERO,
	                        my_y >= value);
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

/* Exclusive-OR Memory with Accumulator. */
static void my_eor(uint8_t value)
{
	my_ac ^= value;
	my_update_sr(my_ac, SR_FLAG_NEGATIVE | SR_FLAG_ZERO);
}

static void my_jmp(uint16_t addr)
{
	my_pc = addr;
}

/* Jump to New Location Saving Return Address. */
static void my_jsr(uint16_t addr)
{
	/* Mimic the hardware behaviour that saves the 8-bit buffer. */
	uint16_t old_pc = my_pc - 1;

	my_push(old_pc >> 8);
	my_push(old_pc);

	my_pc = addr;
}

static void my_inx(void)
{
	my_update_sr(++my_x, SR_FLAG_NEGATIVE | SR_FLAG_ZERO);
}

static void my_iny(void)
{
	my_update_sr(++my_y, SR_FLAG_NEGATIVE | SR_FLAG_ZERO);
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

static void my_ora(uint8_t value)
{
	my_ac |= value;
	my_update_sr(my_ac, SR_FLAG_NEGATIVE | SR_FLAG_ZERO);
}

static void my_rti(void)
{
	my_sr = my_pop() | SR_FLAG_BREAK;

	my_pc = my_pop();
	my_pc |= my_pop() << 8;
}

static void my_rts(void)
{
	my_pc = my_pop();
	my_pc |= my_pop() << 8;

	/* Mimic the hardware behaviour, see JSR. */
	my_pc++;
}

/* Push Accumulator on Stack. */
static void my_pha(void)
{
	my_push(my_ac);
}

static void my_php(void)
{
	/* The status register will be pushed with the break
	 * flag and bit 5 set to 1. */
	my_push(my_sr | SR_FLAG_BREAK);
}

/* Pull Accumulator from Stack. */
static void my_pla(void)
{
	my_ac = my_pop();
	my_update_sr(my_ac, SR_FLAG_NEGATIVE | SR_FLAG_ZERO);
}

/* Pull Processor Status from Stack. */
static void my_plp(void)
{
	/* The unused bit must always be set. */
	my_sr = my_pop() | SR_FLAG_UNUSED;
}

static void my_sec(void)
{
	my_sr |= SR_FLAG_CARRY;
}

static void my_sed(void)
{
	my_sr |= SR_FLAG_DECIMAL;
}

static void my_sei(void)
{
	my_sr |= SR_FLAG_INTERRUPT;
}

static void my_sta(uint16_t addr)
{
	my6502_write(addr, my_ac);
}

static void my_stx(uint16_t addr)
{
	my6502_write(addr, my_x);
}

static void my_sty(uint16_t addr)
{
	my6502_write(addr, my_y);
}

/* Transfer Accumulator to Index X. */
static void my_tax(void)
{
	my_x = my_ac;
	my_update_sr(my_x, SR_FLAG_NEGATIVE | SR_FLAG_ZERO);
}

static void my_tay(void)
{
	my_y = my_ac;
	my_update_sr(my_y, SR_FLAG_NEGATIVE | SR_FLAG_ZERO);
}

/* Transfer Stack Pointer to Index X. */
static void my_tsx(void)
{
	my_x = my_sp;
	my_update_sr(my_x, SR_FLAG_NEGATIVE | SR_FLAG_ZERO);
}

static void my_txa(void)
{
	my_ac = my_x;
	my_update_sr(my_ac, SR_FLAG_NEGATIVE | SR_FLAG_ZERO);
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
	case 0x00:
		my_brk();
		break;
	case 0x08:
		my_php();
		break;
	case 0x09:
		my_ora(my_read_op(IMMEDIATE));
		break;
	case 0x10:
		my_bpl(my_read_addr(RELATIVE));
		break;
	case 0x18:
		my_clc();
		break;
	case 0x20:
		my_jsr(my_read_addr(ABSOLUTE));
		break;
	case 0x28:
		my_plp();
		break;
	case 0x30:
		my_bmi(my_read_addr(RELATIVE));
		break;
	case 0x38:
		my_sec();
		break;
	case 0x40:
		my_rti();
		break;
	case 0x48:
		my_pha();
		break;
	case 0x49:
		my_eor(my_read_op(IMMEDIATE));
		break;
	case 0x4C:
		my_jmp(my_read_addr(ABSOLUTE));
		break;
	case 0x50:
		my_bvc(my_read_addr(RELATIVE));
		break;
	case 0x58:
		my_cli();
		break;
	case 0x60:
		my_rts();
		break;
	case 0x68:
		my_pla();
		break;
	case 0x69:
		my_adc(my_read_op(IMMEDIATE));
		break;
	case 0x6C:
		my_jmp(my_read_addr(INDIRECT));
		break;
	case 0x70:
		my_bvs(my_read_addr(RELATIVE));
		break;
	case 0x78:
		my_sei();
		break;
	case 0x84:
		my_sty(my_read_addr(ZEROPAGE));
		break;
	case 0x85:
		my_sta(my_read_addr(ZEROPAGE));
		break;
	case 0x86:
		my_stx(my_read_addr(ZEROPAGE));
		break;
	case 0x88:
		my_dey();
		break;
	case 0x8A:
		my_txa();
		break;
	case 0x8C:
		my_sty(my_read_addr(ABSOLUTE));
		break;
	case 0x8D:
		my_sta(my_read_addr(ABSOLUTE));
		break;
	case 0x8E:
		my_stx(my_read_addr(ABSOLUTE));
		break;
	case 0x90:
		my_bcc(my_read_addr(RELATIVE));
		break;
	case 0x94:
		my_sty(my_read_addr(ZEROPAGE_X));
		break;
	case 0x95:
		my_sta(my_read_addr(ZEROPAGE_X));
		break;
	case 0x96:
		my_stx(my_read_addr(ZEROPAGE_Y));
		break;
	case 0x98:
		my_tya();
		break;
	case 0x99:
		my_sta(my_read_addr(ABSOLUTE_Y));
		break;
	case 0x9A:
		my_txs();
		break;
	case 0x9D:
		my_sta(my_read_addr(ABSOLUTE_X));
		break;
	case 0xA0:
		my_ldy(my_read_op(IMMEDIATE));
		break;
	case 0xA2:
		my_ldx(my_read_op(IMMEDIATE));
		break;
	case 0xA4:
		my_ldy(my_read_op(ZEROPAGE));
		break;
	case 0xA5:
		my_lda(my_read_op(ZEROPAGE));
		break;
	case 0xA6:
		my_ldx(my_read_op(ZEROPAGE));
		break;
	case 0xA8:
		my_tay();
		break;
	case 0xA9:
		my_lda(my_read_op(IMMEDIATE));
		break;
	case 0xAA:
		my_tax();
		break;
	case 0xAC:
		my_ldy(my_read_op(ABSOLUTE));
		break;
	case 0xAD:
		my_lda(my_read_op(ABSOLUTE));
		break;
	case 0xAE:
		my_ldx(my_read_op(ABSOLUTE));
		break;
	case 0xB0:
		my_bcs(my_read_addr(RELATIVE));
		break;
	case 0xB1:
		my_lda(my_read_op(INDIRECT_Y));
		break;
	case 0xB4:
		my_ldy(my_read_op(ZEROPAGE_X));
		break;
	case 0xB5:
		my_lda(my_read_op(ZEROPAGE_X));
		break;
	case 0xB6:
		my_ldx(my_read_op(ZEROPAGE_Y));
		break;
	case 0xB8:
		my_clv();
		break;
	case 0xB9:
		my_lda(my_read_op(ABSOLUTE_Y));
		break;
	case 0xBA:
		my_tsx();
		break;
	case 0xBC:
		my_ldy(my_read_op(ABSOLUTE_X));
		break;
	case 0xBD:
		my_lda(my_read_op(ABSOLUTE_X));
		break;
	case 0xBE:
		my_ldx(my_read_op(ABSOLUTE_Y));
		break;
	case 0xC0:
		my_cpy(my_read_op(IMMEDIATE));
		break;
	case 0xC4:
		my_cpy(my_read_op(ZEROPAGE));
		break;
	case 0xC5:
		my_cmp(my_read_op(ZEROPAGE));
		break;
	case 0xC8:
		my_iny();
		break;
	case 0xC9:
		my_cmp(my_read_op(IMMEDIATE));
		break;
	case 0xCA:
		my_dex();
		break;
	case 0xCC:
		my_cpy(my_read_op(ABSOLUTE));
		break;
	case 0xCD:
		my_cmp(my_read_op(ABSOLUTE));
		break;
	case 0xD0:
		my_bne(my_read_addr(RELATIVE));
		break;
	case 0xD5:
		my_cmp(my_read_op(ZEROPAGE_X));
		break;
	case 0xD8:
		my_cld();
		break;
	case 0xD9:
		my_cmp(my_read_op(ABSOLUTE_Y));
		break;
	case 0xDD:
		my_cmp(my_read_op(ABSOLUTE_X));
		break;
	case 0xE0:
		my_cpx(my_read_op(IMMEDIATE));
		break;
	case 0xE4:
		my_cpx(my_read_op(ZEROPAGE));
		break;
	case 0xE8:
		my_inx();
		break;
	case 0xEA:
		/* NOP */
		break;
	case 0xEC:
		my_cpx(my_read_op(ABSOLUTE));
		break;
	case 0xF0:
		my_beq(my_read_addr(RELATIVE));
		break;
	case 0xF8:
		my_sed();
		break;
	default:
		assert(0);
		break;
	}
}
