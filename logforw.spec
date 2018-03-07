#
# spec file for irods-logforw
#
Summary: Log forwarder for irods
Name: logforw
Version: 1.0.0
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
rm -rf $RPM_BUILD_DIR/*

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


%changelog
* Wed Mar 7 2018 Ilker Manap <manap@kth.se>
* Added spec file for building rpm

