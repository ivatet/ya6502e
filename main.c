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

	printf("rd(%04x) -> %02x\n", address, value);
	return value;
}

void write6502(uint16_t address, uint8_t value)
{
	printf("wr(%04x) = %02x\n", address, value);
	fake6502_mem[address] = value;
}

/* This implementation. */
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

static void dump_fake6502_reg(void)
{
	printf("pc=%04x sp=%02x a=%02x x=%02x y=%02x status=%02x\n",
		pc, sp, a, x, y, status);
}

int main(int argc, char *argv[])
{
	int i;

	if (argc != 2) {
		printf("Usage: %s <rom.bin>\n", getprogname());
		return 1;
	}

	load_memory(argv[1], fake6502_mem, sizeof(fake6502_mem));

	reset6502();
	dump_fake6502_reg();

	for (i = 0; i < 10; i++)
	{
		step6502();
		dump_fake6502_reg();
	}

	printf("stopped\n");
	return 0;
}
