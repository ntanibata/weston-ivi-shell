Name:           weston-ivi-shell
Version:        0.1.5
Release:        0
Summary:        Weston IVI Shell
License:        MIT
Group:          Graphics & UI Framework/Wayland Window System
Url:            https://github.com/ntanibata/weston-ivi-shell/
Source0:        %name-%version.tar.xz
Source1:        weston.ini
Source1001:     weston-ivi-shell.manifest
BuildRequires:  autoconf >= 2.64, automake >= 1.11
BuildRequires:  libtool >= 2.2
BuildRequires:  libjpeg-devel
BuildRequires:  pkgconfig
BuildRequires:  pkgconfig(weston) >= 1.5
BuildRequires:  pkgconfig(cairo-egl) >= 1.11.3
BuildRequires:  pkgconfig(egl) >= 7.10
BuildRequires:  pkgconfig(mtdev) >= 1.1.0
BuildRequires:  pkgconfig(pixman-1)
BuildRequires:  pkgconfig(wayland-client)
BuildRequires:  pkgconfig(wayland-egl)
BuildRequires:  pkgconfig(wayland-server)
BuildRequires:  pkgconfig(xkbcommon) >= 0.3.0
BuildRequires:  pkgconfig(pangocairo) >= 1.34.0
Requires:       weston >= 1.5

%description
A reference Weston shell designed for use in IVI systems.

%package devel
Summary: Development files for package %{name}
Group:   Graphics & UI Framework/Development
Requires:  %{name} = %{version}-%{release}
%description devel
This package provides header files and other developer releated files
for package %{name}.

%package clients
Summary: Sample clients for package %{name}
Group:   Graphics & UI Framework/Development
%description clients
This package provides a set of example ivi wayland clients useful for
validating the functionality of wayland with very little dependencies
on other system components.

%package config
Summary:    Tizen IVI Weston configuration for package %{name}
Group:      Automotive/Configuration
Requires:   weston-ivi-shell-clients
Requires:   weekeyboard
Requires:   genivi-shell
Conflicts:  weston-ivi-config
Conflicts:  ico-uxf-weston-plugin
%description config
This package contains Tizen IVI-specific configuration for %{name}.

%prep
%setup -q
cp %{SOURCE1001} .

%build
# We only care about the ivi-shell related bits so disable anything
# unrelated.
%autogen \
    --disable-static \
    --disable-libunwind \
    --disable-xwayland \
    --disable-xwayland-test \
    --disable-drm-compositor \
    --disable-x11-compositor \
    --disable-rpi-compositor \
    --disable-fbdev-compositor \
    --disable-wayland-compositor \
    --disable-headless-compositor \
    --disable-weston-launch \
    --enable-simple-clients \
    --enable-clients \
    --disable-wcap-tools \
    --enable-demo-clients-install \
    --disable-libinput-backend \
    --disable-fullscreen-shell \
    --disable-desktop-shell \
    --disable-simple-clients \
    --enable-ivi-shell

%__make %{?_smp_mflags}


%install
%make_install

# install example clients
%define ivi_shell_client_dir %{_bindir}/ivi
mkdir -p %{buildroot}%{ivi_shell_client_dir}
install -m 755 weston-calibrator %{buildroot}%{ivi_shell_client_dir}
install -m 755 weston-simple-touch %{buildroot}%{ivi_shell_client_dir}
install -m 755 weston-simple-shm %{buildroot}%{ivi_shell_client_dir}
install -m 755 weston-simple-egl %{buildroot}%{ivi_shell_client_dir}
install -m 755 weston-flower %{buildroot}%{ivi_shell_client_dir}
install -m 755 weston-image %{buildroot}%{ivi_shell_client_dir}
install -m 755 weston-cliptest %{buildroot}%{ivi_shell_client_dir}
install -m 755 weston-dnd %{buildroot}%{ivi_shell_client_dir}
install -m 755 weston-editor %{buildroot}%{ivi_shell_client_dir}
install -m 755 weston-smoke %{buildroot}%{ivi_shell_client_dir}
install -m 755 weston-resizor %{buildroot}%{ivi_shell_client_dir}
install -m 755 weston-eventdemo %{buildroot}%{ivi_shell_client_dir}
install -m 755 weston-clickdot %{buildroot}%{ivi_shell_client_dir}
install -m 755 weston-subsurfaces %{buildroot}%{ivi_shell_client_dir}
install -m 755 weston-transformed %{buildroot}%{ivi_shell_client_dir}
install -m 755 weston-fullscreen %{buildroot}%{ivi_shell_client_dir}

install -d %{buildroot}/%{_datadir}/%{name}/protocol/

install -m 644 protocol/*.xml \
    %{buildroot}/%{_datadir}/%{name}/protocol/

install -m 644 protocol/*.h \
    %{buildroot}/%{_datadir}/%{name}/protocol/

install -m 644 protocol/*.c \
    %{buildroot}/%{_datadir}/%{name}/protocol/

%define weston_config_dir %{_sysconfdir}/xdg/weston
mkdir -p %{buildroot}%{weston_config_dir}
install -m 0644 %{SOURCE1} %{buildroot}%{weston_config_dir}
install -d %{buildroot}/%{_datadir}/weston/
cp -rfva data/* %{buildroot}/%{_datadir}/weston/

%define _unpackaged_files_terminate_build 0


%files
%manifest %{name}.manifest
%defattr(-,root,root)
%license COPYING
%_libdir/weston/ivi-shell.so
%_libdir/weston/ivi-layout.so
%_libdir/weston/hmi-controller.so
%_libexecdir/weston-ivi-shell-user-interface
%_datadir/weston/*

%exclude %_bindir/weston
%exclude %_libdir/weston/desktop-shell.so

%files devel
%manifest %{name}.manifest
%_includedir/weston/ivi-layout-export.h
%_includedir/weston/ivi-layout.h
%_includedir/weston/ivi-shell.h
%_includedir/weston/ivi-shell-ext.h
%_includedir/weston/ivi-layout-transition.h
%{_datadir}/%{name}/protocol/*

%files clients
%manifest %{name}.manifest
%{ivi_shell_client_dir}/weston-simple-touch
%{ivi_shell_client_dir}/weston-simple-shm
%{ivi_shell_client_dir}/weston-simple-egl
%{ivi_shell_client_dir}/weston-flower
%{ivi_shell_client_dir}/weston-image
%{ivi_shell_client_dir}/weston-cliptest
%{ivi_shell_client_dir}/weston-dnd
%{ivi_shell_client_dir}/weston-editor
%{ivi_shell_client_dir}/weston-smoke
%{ivi_shell_client_dir}/weston-resizor
%{ivi_shell_client_dir}/weston-eventdemo
%{ivi_shell_client_dir}/weston-clickdot
%{ivi_shell_client_dir}/weston-subsurfaces
%{ivi_shell_client_dir}/weston-transformed
%{ivi_shell_client_dir}/weston-fullscreen
%{ivi_shell_client_dir}/weston-calibrator

%files config
%manifest %{name}.manifest
%config %{weston_config_dir}/weston.ini

%exclude %_libdir/pkgconfig/weston.pc
