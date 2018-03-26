#
# spec file for irods-logforw
#
Summary: Log forwarder for irods
Name: logforw
Version: 1.0.2
Release: 1
License: Simplified BSD
Group: Applications/System
%undefine _disable_source_fetch
Source: https://github.com/KTH-PDC/irods-logforw/archive/%{version}.tar.gz
URL: https://github.com/KTH-PDC/irods-logforw
Distribution: Centos
Vendor: KTH
Packager: Ilker Manap <manap@kth.se>

%description
iRODS log forwarder

%prep

%setup -q -c

%build
(cd irods-%{name}-%{version}; make)

%install
mkdir -p %{buildroot}/usr/local/bin
cp irods-%{name}-%{version}/logforw          %{buildroot}/usr/local/bin
cp irods-%{name}-%{version}/logforw-errpt    %{buildroot}/usr/local/bin
cp irods-%{name}-%{version}/logforw-start    %{buildroot}/usr/local/bin
cp irods-%{name}-%{version}/logforw-status   %{buildroot}/usr/local/bin
cp irods-%{name}-%{version}/logforw-stop     %{buildroot}/usr/local/bin

mkdir -p  %{buildroot}/usr/local/share/man/man1
cp irods-%{name}-%{version}/logforw.1          %{buildroot}/usr/local/share/man/man1
cp irods-%{name}-%{version}/logforw-errpt.1    %{buildroot}/usr/local/share/man/man1
cp irods-%{name}-%{version}/logforw-start.1    %{buildroot}/usr/local/share/man/man1
cp irods-%{name}-%{version}/logforw-status.1   %{buildroot}/usr/local/share/man/man1
cp irods-%{name}-%{version}/logforw-stop.1     %{buildroot}/usr/local/share/man/man1

mkdir -p %{buildroot}/etc/sysconfig
cp irods-%{name}-%{version}/sysconfig/logforw %{buildroot}/etc/sysconfig/logforw

mkdir -p %{buildroot}/etc/systemd/system
cp irods-%{name}-%{version}/systemd/logforw.service %{buildroot}/etc/systemd/system/logforw.service

%files
%defattr(-,root,root,-)
%attr(755,root,root) /usr/local/bin/logforw
%attr(755,root,root) /usr/local/bin/logforw-errpt
%attr(755,root,root) /usr/local/bin/logforw-start
%attr(755,root,root) /usr/local/bin/logforw-status
%attr(755,root,root) /usr/local/bin/logforw-stop

%attr(644,root,root) /usr/local/share/man/man1/logforw.1
%attr(644,root,root) /usr/local/share/man/man1/logforw-errpt.1
%attr(644,root,root) /usr/local/share/man/man1/logforw-start.1
%attr(644,root,root) /usr/local/share/man/man1/logforw-status.1
%attr(644,root,root) /usr/local/share/man/man1/logforw-stop.1

%attr(644,root,root) /etc/sysconfig/logforw
%attr(644,root,root) /etc/systemd/system/logforw.service

%post
mkdir -p /var/log/logforw

%changelog
* Thu Mar 15 2018 Ilari Korhonen <ilarik@kth.se?
* Added systemd service manifest and config file

* Wed Mar 7 2018 Ilker Manap <manap@kth.se>
* Added spec file for building rpm

