TARGET:=6502.elf

.PHONY: all
all: $(TARGET)

$(TARGET): main.c vendor/fake6502.c
	gcc $^ -o $@

.PHONY: clean
clean:
	rm -f $(TARGET)

