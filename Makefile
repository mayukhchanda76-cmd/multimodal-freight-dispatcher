CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -O2
CPPFLAGS :=
LDFLAGS :=
LDLIBS :=

TARGET := mfyd
SRC := $(wildcard *.c)
OBJ := $(SRC:.c=.o)
DEP := $(OBJ:.o=.d)

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJ) $(DEP) $(TARGET)

-include $(DEP)
