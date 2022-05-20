# pacleaves - show packages not required by any other
#
# Copyright (c) 2022 Emil Renner Berthing
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

MAKEFLAGS += rR

PACLEAVES = pacleaves

O         = .
S        := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
TARGET    = $O/$(PACLEAVES)

ARCHFLAGS =
OPT       = -O2
DEPENDS   = -MMD -MP
WARNINGS  = -Wall -Wextra -Wshadow -Wpointer-arith -Wformat=2 -Wformat-truncation=2 -Wundef -Wno-unused-parameter
CPPFLAGS  =
CFLAGS    = $(ARCHFLAGS) $(OPT) -ggdb -pipe $(DEPENDS) $(WARNINGS) $(CPPFLAGS)
LDFLAGS   = $(ARCHFLAGS) $(OPT)
SFLAGS    = --strip-all --strip-unneeded

CC        = $(CROSS_COMPILE)gcc
STRIP     = $(CROSS_COMPILE)strip
#PKGCONFIG = $(CROSS_COMPILE)pkg-config
MKDIR_P   = mkdir -p
RM_F      = rm -f
RMDIR     = rmdir
echo      = @echo '$1'

ifneq ($(PKGCONFIG),)
LIBALPM   = libalpm
CFLAGS   += $(shell $(PKGCONFIG) --cflags $(LIBALPM))
LIBS     += $(shell $(PKGCONFIG) --libs $(LIBALPM))
else
LIBALPM   = -lalpm
LIBS     += $(LIBALPM)
endif

objects = $(patsubst $S/%.c,$O/%.o,$(wildcard $S/*.c))
clean   = $O/*.d $O/*.o $(TARGET)

# use make V=1 to see raw commands or make -s for silence
ifeq ($V$(findstring s,$(word 1,$(MAKEFLAGS))),)
Q := @
else
echo =
endif

#.SECONDEXPANSION:
.PHONY: all strip clean
.PRECIOUS: $O/%.o

all: $(TARGET)

strip: $(TARGET)
	$(call echo,  STRIP $<)
	$Q$(STRIP) $(SFLAGS) $<

$(TARGET): $(objects)
	$(call echo,  CCLD  $@)
	$Q$(CC) -o $@ $(LDFLAGS) $^ $(LIBS)

$O/%.o: $S/%.c $(MAKEFILE_LIST) | $O/
	$(call echo,  CC    $<)
	$Q$(CC) -o $@ $(CFLAGS) -c $<

$O/:
	$(call echo,  MKDIR $@)
	$Q$(MKDIR_P) $@

clean:
	$(call echo,  RM    $(clean:./%=%))
	$Q$(RM_F) $(clean)
	$(if $(O:.=),$Q$(RMDIR) $O/ >/dev/null 2>&1 || true)

-include $O/*.d
