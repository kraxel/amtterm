Name:         amtterm
License:      GPLv2+
Version:      1.8
Release:      1%{?dist}
Summary:      Serial-over-lan (sol) client for Intel AMT
Group:        Applications/Internet
URL:          http://www.kraxel.org/blog/linux/amtterm/
Source:       http://www.kraxel.org/releases/%{name}/%{name}-%{version}.tar.gz
Requires:     xdg-utils

BuildRequires: meson gcc
BuildRequires: desktop-file-utils
BuildRequires: pkgconfig(gtk+-3.0)
BuildRequires: pkgconfig(gdk-3.0)
BuildRequires: pkgconfig(vte-2.91)
BuildRequires: pkgconfig(gnutls)

%description
Serial-over-lan (sol) client for Intel AMT.
Includes a terminal and a graphical (gtk) version.
Also comes with a perl script to gather informations
about and remotely control AMT managed computers.

%prep
%setup -q

%build
export CFLAGS="%{optflags}"
meson --prefix=%{_prefix} build-rpm
ninja-build -C build-rpm

%install
export DESTDIR=%{buildroot}
ninja-build -C build-rpm install
mkdir -p %{buildroot}%{_datadir}/applications

%files
%doc COPYING
%{_bindir}/amtterm
%{_bindir}/amtider
%{_bindir}/amttool
%{_bindir}/gamt
%{_mandir}/man1/amtterm.1.gz
%{_mandir}/man1/amtider.1.gz
%{_mandir}/man1/amttool.1.gz
%{_mandir}/man1/gamt.1.gz
%{_mandir}/man7/amt-howto.7.gz
%{_datadir}/applications/gamt.desktop

%changelog
* Mon May 04 2026 Gerd Hoffmann <kraxel@redhat.com> 1.8-1
- mapages: document new options (roger.pau@citrix.com)
- ssl: use SSL by default without server certificate checking
  (roger.pau@citrix.com)
- tcp: constify some of the function parameters (roger.pau@citrix.com)
- ssl: widen GnuTLS priority string (roger.pau@citrix.com)
- purge make buildsystem (kraxel@redhat.com)
- amtider: allow to build without signalfd (roger.pau@citrix.com)
- tcp: use POSIX defines for family types (roger.pau@citrix.com)
- meson: make gtk, gdk and vte optional dependencies (roger.pau@citrix.com)
- ci: downgrade make test to manual (kraxel@redhat.com)
- move .desktop install from rpm spec to meson.build (kraxel@redhat.com)
- switch rpm build to meson (kraxel@redhat.com)
- add meson.build (kraxel@redhat.com)
- rename manual pages (kraxel@redhat.com)
- add missing include files (kraxel@redhat.com)
- ider: remove dependency on Linux specific scsi/scsi.h header
  (roger.pau@citrix.com)
- redir: remove extra '&' in mask (roger.pau@citrix.com)
- ci: meson -> make (not converted yet) (kraxel@redhat.com)
- ci: drop include (kraxel@redhat.com)
- amttool: set SSL_OP_ALLOW_UNSAFE_LEGACY_RENEGOTIATION (flokli@flokli.de)
- enable TLSv1.2 support with legacy renegotiation (dmitry@bedrocksystems.com)
- update upload host (kraxel@redhat.com)
- enable ssl in rpm builds (kraxel@redhat.com)
- update gitignore, add gitlab ci (kraxel@redhat.com)

* Fri Apr 22 2022 Gerd Hoffmann <kraxel@redhat.com> 1.7-1
- add amtider to specfile (kraxel@redhat.com)
- amtider: use defines for floppy and cdrom type (hare@suse.de)
- amtider: switch to cd-rom emulation per default and add manpage
  (hare@suse.de)
- ider: cleanup documentation (hare@suse.de)
- ider: implement GET CONFIGURATION (hare@suse.de)
- ider: implement READ DISC INFORMATION and READ TRACK INFORMATION
  (hare@suse.de)
- ider: fixup READ TOC (hare@suse.de)
- ider: chunked transport (hare@suse.de)
- redir: split client and server seqno (hare@suse.de)
- ider: switch to floppy emulation (hare@suse.de)
- ider: fixups and better logging (hare@suse.de)
- amtider: option to start gracefully or onreboot (hare@suse.de)
- ider: implement ider_read_data() (hare@suse.de)
- ider: implement READ_10 (hare@suse.com)
- ider: Add MODE_SENSE_10 (hare@suse.com)
- ider: implement device select (hare@suse.com)
- ider: implement READ CAPACITY (hare@suse.com)
- ider: fixup compilation (hare@suse.de)
- ider: implement data_to_host (hare@suse.de)
- redir: split off IDE redirection commands (hare@suse.de)
- redir: reshuffle functions (hare@suse.de)
- redir: add ider reset handling (hare@suse.de)
- amtider: handle SIGTERM (hare@suse.de)
- redir: IDE-redirection receive stubs (hare@suse.de)
- redir: select start function by type (hare@suse.de)
- amtider: IDE-redirection client (hare@suse.de)
- RedirectionConstants.h: remove linebreak (hare@suse.de)
- Merge branch 'ssl' (hare@suse.de)
- amtterm: cleanup whitespace (hare@suse.de)
- redir: cleanup whitespace (hare@suse.de)
- add note for newer machines to amtterm manpage (kraxel@redhat.com)
- add gcc to specfile (kraxel@redhat.com)
- fix memory leak (kraxel@redhat.com)
- fix typo in man page (kraxel@redhat.com)

* Mon Feb 27 2017 Gerd Hoffmann <kraxel@redhat.com> 1.6-1
- sync makefile & specfile (kraxel@redhat.com)
- *really* switch to vte 2.91 (kraxel@redhat.com)

* Mon Feb 27 2017 Gerd Hoffmann <kraxel@redhat.com> 1.5-1
- new package built with tito

