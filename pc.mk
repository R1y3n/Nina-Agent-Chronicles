# PC test rig: the PS4 layout (-DRT_PS4) built natively with gcc so the host layer can be iterated on quickly.
CC := gcc
CFLAGS := -O0 -w -pipe -MMD -MP -DRT_PS4 -I. -I.
GEN := $(wildcard out/*.c)
RT  := rt.c input_map.c gfx_null.c gfx_soft.c win32_shim.c api_stubs_generated.c modules_lithtech.c main_lithtech.c $(wildcard host_*.c)
OBJ := $(patsubst %.c,obj/%.o,$(notdir $(GEN) $(RT)))
DEP := $(OBJ:.o=.d)
vpath %.c out .
obj/%.o: %.c
	@mkdir -p obj; $(CC) $(CFLAGS) -c $< -o $@
lithtech_pc: $(OBJ)
	$(CC) -o $@ $(OBJ) -lm -lpthread -lX11

-include $(DEP)
