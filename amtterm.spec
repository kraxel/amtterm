Name:         amtterm
License:      GPLv2+
Version:      1.7
Release:      1%{?dist}
Summary:      Serial-over-lan (sol) client for Intel AMT
Group:        Applications/Internet
URL:          http://www.kraxel.org/blog/linux/amtterm/
Source:       http://www.kraxel.org/releases/%{name}/%{name}-%{version}.tar.gz
Requires:     xdg-utils

BuildRequires: gcc
BuildRequires: desktop-file-utils
BuildRequires: pkgconfig(gtk+-3.0)
BuildRequires: pkgconfig(gdk-3.0)
BuildRequires: pkgconfig(vte-2.91)

%description
Serial-over-lan (sol) client for Intel AMT.
Includes a terminal and a graphical (gtk) version.
Also comes with a perl script to gather informations
about and remotely control AMT managed computers.

%prep
%setup -q

%build
export CFLAGS="%{optflags}"
make prefix=/usr

%install
make prefix=/usr DESTDIR=%{buildroot} STRIP="" install
desktop-file-install --dir=%{buildroot}%{_datadir}/applications/ \
    %{buildroot}/%{_datadir}/applications/gamt.desktop

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

