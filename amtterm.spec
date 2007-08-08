Name:         amtterm
License:      GPL
Autoreqprov:  on
Version:      42
Release:      0%{?dist}
Summary:      intel amt serial-over-lan client
Group:        Applications
Source:       amtterm-%{version}.tar.gz
Buildroot:    %{_tmppath}/root-%{name}-%{version}

%description
intel amt serial-over-lan client

%prep
%setup -q -n amtterm

%build
make

%install
make DESTDIR=%{buildroot} install

%files
%defattr(-,root,root)
%doc *.txt
%{_bindir}/amtterm

