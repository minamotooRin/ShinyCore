/* The application owns its manifest. Some MinGW distributions give their
   default resource a non-neutral language, preventing ld's usual replacement. */
SECTIONS { /DISCARD/ : { *default-manifest.o(.rsrc) } }
INSERT BEFORE .rsrc;
