Name:		microcom
Version:	1.3
Release:	1%{?dist}
Summary:	A simple serial port communication tool.

License:	GPLv3
URL:		https://ringis.se/microcom/
Source0:	%{name}-%{version}.tar.gz

BuildRequires:	cmake

%description
Microcom is a simple serial port communication tool. It does not
implement terminal emulation or special modem support, but is very
efficiant when you need a serial connection to a device connected to
the serial port, such as an embedded CPU board or a managed Ethernet
switch.  It can either be run interactively, or it can run an program
with the standard input and output of that program connected to the
serial port.


%prep
%setup -q


%build
%cmake
%cmake_build


%install
%cmake_install


%files
%defattr(-,root,root,-)
%license COPYING
%doc NEWS
%{_bindir}/microcom
%{_mandir}/man1/*


%changelog
* Wed Jan 20 14:23:06 CET 2021 Tobias Ringström <tobias@ringis.se>
- 
