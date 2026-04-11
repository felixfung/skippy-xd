Name:           skippy-xd
Version:        VERSION_PLACEHOLDER
Release:        1%{?dist}
Summary:        Full-screen window switcher for X11
License:        GPL-2.0-only
URL:            https://github.com/musqz/skippy-xd
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  make
BuildRequires:  libX11-devel
BuildRequires:  libXft-devel
BuildRequires:  libXrender-devel
BuildRequires:  libXcomposite-devel
BuildRequires:  libXdamage-devel
BuildRequires:  libXfixes-devel
BuildRequires:  libXext-devel
BuildRequires:  libXinerama-devel
BuildRequires:  libjpeg-turbo-devel
BuildRequires:  giflib-devel
BuildRequires:  libpng-devel

%description
Skippy-XD is a full-screen task switcher for X11 with live thumbnails
of all open windows, similar to the Expose feature in macOS.
It is window-manager agnostic and works with any EWMH-compliant WM.

%prep
%autosetup

%build
%make_build

%install
mkdir -p %{buildroot}%{_mandir}/man1
%make_install PREFIX=%{_prefix} MANDIR=%{_mandir}/man1

%files
%license COPYING
%{_bindir}/skippy-xd
%{_mandir}/man1/skippy-xd.1*
%config(noreplace) /etc/xdg/skippy-xd.rc

%changelog
* $(date '+%a %b %d %Y') musqz <musqz@github> - VERSION_PLACEHOLDER-1
- Release VERSION_PLACEHOLDER
