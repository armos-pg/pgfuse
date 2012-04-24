all: pgfuse

# name and version of package
PACKAGE_NAME = pgfuse
PACKAGE_VERSION = 0.0.1

# installation dirs
DESTDIR=
prefix=/usr

# standard directories following FHS
execdir=$(DESTDIR)$(prefix)
bindir=$(execdir)/bin
datadir=$(execdir)/share

include inc.mak

clean:
	rm -f pgfuse pgfuse.o pgsql.o pool.o
	cd tests && $(MAKE) clean

test:
	cd tests && $(MAKE) test
	
pgfuse: pgfuse.o pgsql.o pool.o
	$(CC) -o pgfuse pgfuse.o pgsql.o pool.o $(LDFLAGS) 

pgfuse.o: pgfuse.c pgsql.h pool.h config.h
	$(CC) -c $(CFLAGS) -o pgfuse.o pgfuse.c

pgsql.o: pgsql.c pgsql.h config.h
	$(CC) -c $(CFLAGS) -o pgsql.o pgsql.c

pool.o: pool.c pool.h
	$(CC) -c $(CFLAGS) -o pool.o pool.c

install: all
	test -d "$(bindir)" || mkdir -p "$(bindir)"
	cp pgfuse "$(bindir)"
	test -d "$(datadir)/man/man1" || mkdir -p "$(datadir)/man/man1"
	cp pgfuse.1 "$(datadir)/man/man1"
	gzip "$(datadir)/man/man1/pgfuse.1"
	test -d "$(datadir)/$(PACKAGE_NAME)-$(PACKAGE_VERSION)" || \
		mkdir -p "$(datadir)/$(PACKAGE_NAME)-$(PACKAGE_VERSION)"
	cp schema.sql "$(datadir)/$(PACKAGE_NAME)-$(PACKAGE_VERSION)"
	
dist:
	rm -rf /tmp/$(PACKAGE_NAME)-$(PACKAGE_VERSION)
	mkdir /tmp/$(PACKAGE_NAME)-$(PACKAGE_VERSION)
	cp -r * /tmp/$(PACKAGE_NAME)-$(PACKAGE_VERSION)/.
	cd /tmp/$(PACKAGE_NAME)-$(PACKAGE_VERSION); \
		$(MAKE) clean; \
		cd .. ; \
		tar cvf $(PACKAGE_NAME)-$(PACKAGE_VERSION).tar \
			$(PACKAGE_NAME)-$(PACKAGE_VERSION)
	rm -rf /tmp/$(PACKAGE_NAME)-$(PACKAGE_VERSION)
	mv /tmp/$(PACKAGE_NAME)-$(PACKAGE_VERSION).tar .

dist-gz: dist
	rm -f $(PACKAGE_NAME)-$(PACKAGE_VERSION).tar.gz
	gzip $(PACKAGE_NAME)-$(PACKAGE_VERSION).tar
