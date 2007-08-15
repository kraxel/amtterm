# just some maintainer stuff for me ...
########################################################################

make-sync-dir = $(HOME)/projects/gnu-makefiles
repository = $(shell cat CVS/Repository)
release-dir = $(HOME)/projects/Releases

.PHONY: sync
sync:: distclean
	test -d $(make-sync-dir)
	rm -f $(srcdir)/INSTALL $(srcdir)/mk/*.mk
	cp -v $(make-sync-dir)/INSTALL $(srcdir)/.
	cp -v $(make-sync-dir)/*.mk $(srcdir)/mk
	chmod 444 $(srcdir)/INSTALL $(srcdir)/mk/*.mk

release:
	cvs tag $(RELTAG)
	cvs export -r $(RELTAG) -d "$(repository)-$(VERSION)" "$(repository)"
	find "$(repository)-$(VERSION)" -name .cvsignore -exec rm -fv "{}" ";"
	tar -c -z -f "$(release-dir)/$(repository)-$(VERSION).tar.gz" \
		"$(repository)-$(VERSION)"
	rm -rf "$(repository)-$(VERSION)"

