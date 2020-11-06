</$objtype/mkfile

OBJ=\
	waffle.$O

%.$O: %.c
	$CC $CFLAGS $prereq

$O.out: $OBJ
	$LD $prereq

all:V: $O.out

install:V: all
	cp $O.out /$objtype/bin/waffle

uninstall:V:
	rm -f /$objtype/bin/waffle

clean nuke:V:
	rm -f *.[$OS] $O.out
