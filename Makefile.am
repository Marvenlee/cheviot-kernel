
sbin_PROGRAMS = kernel

kernel_SOURCES =

include proc/Makefile.am
include fs/Makefile.am
include utility/Makefile.am
include vm/Makefile.am
include socket/Makefile.am


if BOARD_RASPBERRY_PI_1

include boards/raspberry_pi_1/Makefile.am

AM_CFLAGS = -O0 -g2 -std=c99
AM_CPPFLAGS = -I$(srcdir)/include -I$(srcdir)/boards/raspberry_pi_1/include -DBOARD_RASPBERRY_PI_1
AM_CCASFLAGS = -r -I$(srcdir)/include -I$(srcdir)/boards/raspberry_pi_1/include -DBOARD_RASPBERRY_PI_1

kernel_LDADD = -lrpihal
kernel_LDFLAGS = -v \
                 -nostartfiles \
                 -lgcc \
                 -T $(srcdir)/boards/raspberry_pi_1/linker.ls

else
if BOARD_RASPBERRY_PI_4

include boards/raspberry_pi_4/Makefile.am

AM_CFLAGS = -O0 -g2 -std=c99 -mcpu=cortex-a72 -mfpu=vfpv3-d16 -Wall
AM_CPPFLAGS = -I$(srcdir)/include -I$(srcdir)/boards/raspberry_pi_4/include -DBOARD_RASPBERRY_PI_4
AM_CCASFLAGS = -mcpu=cortex-a72 -mfpu=vfpv3-d16 -r -I$(srcdir)/include -I$(srcdir)/boards/raspberry_pi_4/include -DBOARD_RASPBERRY_PI_4

kernel_LDADD = -lrpihal
kernel_LDFLAGS = -v \
                 -nostartfiles \
                 -lgcc \
                 -T $(srcdir)/boards/raspberry_pi_4/linker.ls
else
  echo "BOARD env variable not valid";
  exit -1;

endif
endif

