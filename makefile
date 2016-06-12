#
#########################################################################
# makefile for fstab formatter						#
# includes new data dictionary						#
# Warning. VPATH should not have any *.o files from this makefile       #
# Reminder $< input file   $@ output file				#
#########################################################################
#
CC=gcc       #-Wextra
CFLAGS= -O4  -Wall # -DNDEBUG
srcs=src/*.c
OBJDIR=./obj
OBJS=$(addprefix $(OBJDIR)/,dictionary.o )
#VPATH=./src:
vpath %c ./src
vpath %h ./src
PROGS=	fstabxref fstablsblk

	 
all	:  ${PROGS} src/dictionary.c src/dictionary.h
default :  ${PROGS} src/dictionary.c src/dictionary.h 

.PHONY : clean all install tar cleantest
clean: 
	rm -f ${PROGS} *.o $(OBJDIR)/*

cleantest:
	rm -f fstabxref.tar *CHECKSUM

install:
	cp -rp fstabxref  ~/bin
	cp -rp fstablsblk ~/bin

fstabxref: fstabxref.c    $(OBJS)  
	${CC} ${CFLAGS} $< $(OBJS) -o $@

fstablsblk: fstablsblk.c  $(OBJS)
	${CC} ${CFLAGS} $< $(OBJS) -o $@

src/dictionary.c: ../iniParser/src/dictionary.c
	cp -f  $<  $@

src/dictionary.h: ../iniParser/src/dictionary.h
	cp -f  $<  $@

tar:
	@sha256sum fstabxref fstablsblk README*   >fstabxref.sha256sum.CHECKSUM 
	tar -cjvf fstabxref.tar  fstabxref fstablsblk  README* *CHECKSUM 

obj/dictionary.o : dictionary.c dictionary.h
	$(CC) $(CFLAGS) -c $<  -o $@ 

