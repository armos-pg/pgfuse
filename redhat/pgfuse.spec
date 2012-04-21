# PgFuse RPM spec file
#
# Copyright (C) 2012 

%define rhel 0
%define rhel5 0
%define rhel6 0
%if 0%{?rhel_version} >= 500 && 0%{?rhel_version} <= 599
%define dist rhel5
%define rhel 1
%define rhel5 1
%endif
%if 0%{?rhel_version} >= 600 && 0%{?rhel_version} <= 699
%define dist rhel6
%define rhel 1
%define rhel6 1
%endif

%define centos 0
%if 0%{?centos_version} >= 500 && 0%{?centos_version} <= 599
%define dist centos5
%define centos 1
%define rhel5 1
%endif

%if 0%{?centos_version} >= 600 && 0%{?centos_version} <= 699
%define dist centos6
%define centos 1
%define rhel6 1
%endif

%define fedora 0
%define fc14 0
%if 0%{?fedora_version} == 14
%define dist fc14
%define fc14 1
%define fedora 1
%endif
%define fc15 0  
%if 0%{?fedora_version} == 15
%define dist fc15
%define fc15 1
%define fedora 1
%endif
%define fc16 0  
%if 0%{?fedora_version} == 16
%define dist fc16
%define fc16 1   
%define fedora 1
%endif

%define suse 0
%if 0%{?suse_version} == 1140
%define dist osu114
%define suse 1
%endif
%if 0%{?suse_version} > 1140
%define dist osu121
%define suse 1
%endif

%define sles 0
%if 0%{?sles_version} == 11
%define dist sle11
%define sles 1
%endif

Summary: PgFuse stores files in a PostgreSQL database using the FUSE API.
Name: pgfuse
Version: 0.0.1
Release: 0.1
License: GPLv3
Group: System Environment/Filesystems

Source: %{name}_%{version}.tar.gz

URL: https://github.com/andreasbaumann/pgfuse

BuildRoot: %{_tmppath}/%{name}-root

# Build dependencies
###

%if %{rhel} || %{centos} || %{fedora}
BuildRequires: pkgconfig
%endif
%if %{suse} || %{sles}
BuildRequires: pkg-config
%endif

BuildRequires: gcc

%if %{rhel} || %{centos} || %{fedora}
%if %{rhel5}
BuildRequires: postgresql84-devel
Requires: postgresql84-libs
%else
BuildRequires: postgresql-devel >= 8.4
Requires: postgresql-libs >= 8.4
%endif
%endif

%if %{suse}
BuildRequires: postgresql-devel >= 8.4
Requires: postgresql-libs >= 8.4
%endif

%if %{sles}
BuildRequires: postgresql-devel >= 8.4
Requires: postgresql-libs >= 8.4
%endif

BuildRequires: fuse-devel >= 2.6
Requires: fuse-libs >= 2.6
Requires: fuse >= 2.6

# Check if 'Distribution' is really set by OBS (as mentioned in bacula)
%if ! 0%{?opensuse_bs}
Distribution: %{dist}
%endif

Packager: Andreas Baumann <abaumann@yahoo.com>

%description
PgFuse stores a whole filesystem in a set of database tables in a
PostgreSQL database. This is done using the FUSE API.

%prep
%setup

%build

make %{?_smp_mflags}

%install
make DESTDIR=$RPM_BUILD_ROOT install

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr( -, root, root )
%{_bindir}/pgfuse
%{_datadir}/man/man1/pgfuse.1.gz
%dir %{_datadir}/%{name}-%{version}
%{_datadir}/%{name}-%{version}/schema.sql

%changelog
* Fri Apr 20 2012 Andreas Baumann <abaumann@yahoo.com> 0.0.1-0.1
- preliminary release
