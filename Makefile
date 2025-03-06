CFLAGS=-g -O -I/usr/include/tirpc
#CFLAGS=-g -O -Wall

LIBS=-ltirpc
#LIBS=-lrpcsvc

nfsping: nfsping.o
	$(CC) -o nfsping nfsping.o $(LIBS)

clean:
	rm -f nfsping *.o *~ \#*

distclean: clean

# Git targets
push: 	distclean
	git add -A && git commit -a && git push

pull:
	git pull
