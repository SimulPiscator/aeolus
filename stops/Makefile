default:
	echo Please specify make target: archive

DIR := $(shell basename `pwd`)

archive:	distclean
	cd ..; /bin/rm -f $(DIR).tar.bz2; tar cvf $(DIR).tar $(DIR); bzip2 $(DIR).tar

distclean:
	/bin/rm -f *~
	/bin/rm -f waves/*


