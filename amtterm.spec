Name:         amtterm
License:      GPLv2+
Version:      1.4
Release:      1%{?dist}
Summary:      Serial-over-lan (sol) client for Intel AMT
Group:        Applications/Internet
URL:          http://www.kraxel.org/blog/linux/amtterm/
Source:       http://www.kraxel.org/releases/%{name}/%{name}-%{version}.tar.gz
Requires:     xdg-utils

BuildRequires: desktop-file-utils
BuildRequires: pkgconfig(gtk+-x11-3.0)
BuildRequires: pkgconfig(vte-2.90)

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
%{_bindir}/amttool
%{_bindir}/gamt
%{_mandir}/man1/amtterm.1.gz
%{_mandir}/man1/amttool.1.gz
%{_mandir}/man1/gamt.1.gz
%{_mandir}/man7/amt-howto.7.gz
%{_datadir}/applications/gamt.desktop

%changelog
