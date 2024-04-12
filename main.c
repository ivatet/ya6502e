#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

/* Reference implementation. */
extern void reset6502();
extern void step6502();

extern uint16_t pc;
extern uint8_t sp, a, x, y, status;

static uint8_t fake6502_mem[0x10000];

uint8_t read6502(uint16_t address)
{
	uint8_t value = fake6502_mem[address];

	printf(". rd(%04x) -> %02x\n", address, value);
	return value;
}

void write6502(uint16_t address, uint8_t value)
{
	printf(". wr(%04x) = %02x\n", address, value);
	fake6502_mem[address] = value;
}

static void dump_fake6502_reg(void)
{
	printf(". pc=%04x sp=%02x a=%02x x=%02x y=%02x status=%02x\n",
		pc, sp, a, x, y, status);
}

/* My implementation. */
extern void my6502_reset(uint16_t pc);
extern void my6502_step(void);

extern uint16_t my_pc;
extern uint8_t my_ac, my_x, my_y, my_sr, my_sp;

static uint8_t my6502_mem[0x10000];

uint8_t my6502_read(uint16_t address)
{
	uint8_t value = my6502_mem[address];

	printf("! rd(%04x) -> %02x\n", address, value);
	return value;
}

void my6502_write(uint16_t address, uint8_t value)
{
	printf("! wr(%04x) = %02x\n", address, value);
	my6502_mem[address] = value;
}

static void dump_my6502_reg(void)
{
	printf("! pc=%04x sp=%02x a=%02x x=%02x y=%02x status=%02x\n",
		my_pc, my_sp, my_ac, my_x, my_y, my_sr);
}

static void load_memory(const char *file_name, uint8_t *mem, size_t mem_sz)
{
	struct stat s;
	int fd, rc;
	void *p;

	fd = open(file_name, O_RDONLY);
	assert(fd >= 0);

	rc = fstat(fd, &s);
	assert(rc == 0);
	assert(s.st_size <= mem_sz);

	p = mmap(0, s.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	assert(p != MAP_FAILED);

	memcpy(mem, p, s.st_size);

	munmap(p, s.st_size);
	close(fd);

	printf("loaded %lld bytes\n", s.st_size);
}

static int cmp_reg(void)
{
	return (pc != my_pc || sp != my_sp || a != my_ac
		|| x != my_x || y != my_y || status != my_sr);
}

int main(int argc, char *argv[])
{
	int i;

	if (argc != 2) {
		printf("Usage: %s <rom.bin>\n", getprogname());
		return 1;
	}

	load_memory(argv[1], fake6502_mem, sizeof(fake6502_mem));
	memcpy(my6502_mem, fake6502_mem, sizeof(my6502_mem));

	reset6502();
	my6502_reset(0x400);

	/* Altering PC to run functional tests.
	 * See:
	 * https://github.com/Klaus2m5/6502_65C02_functional_tests/tree/master/bin_files
	 */
	pc = 0x400;
	printf("altered reference pc\n");

	dump_fake6502_reg();
	dump_my6502_reg();
	for (i = 0; i < 40; i++)
	{
		printf("step %d\n", i);
		step6502();
		my6502_step();

		if (cmp_reg()) {
			printf("! register mismatch\n");
			dump_fake6502_reg();
			dump_my6502_reg();
			return 1;
		}

		if (memcmp(fake6502_mem, my6502_mem, sizeof(fake6502_mem))) {
			printf("! memory mismatch\n");
			return 1;
		}
	}

	printf("stopped\n");
	return 0;
}
