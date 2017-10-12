
all:

install-data-local:
	if [ ! -d "$(DESTDIR)@datadir@/ltfs" ]; then \
		mkdir -p "$(DESTDIR)@datadir@/ltfs"; \
	fi ;\
	if [ ! -f "$(DESTDIR)@datadir@/ltfs/ltfs" ]; then \
		install -m 755 ltfs "$(DESTDIR)@datadir@/ltfs/ltfs"; \
	fi

clean-local:
