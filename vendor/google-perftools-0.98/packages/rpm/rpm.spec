%define	RELEASE	1
%define rel     %{?CUSTOM_RELEASE} %{!?CUSTOM_RELEASE:%RELEASE}
%define	prefix	/usr

Name: %NAME
Summary: Performance tools for C++
Version: %VERSION
Release: %rel
Group: Development/Libraries
URL: http://code.google.com/p/google-perftools/
License: BSD
Vendor: Google
Packager: Google <opensource@google.com>
Source: http://%{NAME}.googlecode.com/files/%{NAME}-%{VERSION}.tar.gz
Distribution: Redhat 7 and above.
Buildroot: %{_tmppath}/%{name}-root
Prefix: %prefix

%description
The %name packages contains some utilities to improve and analyze the
performance of C++ programs.  This includes an optimized thread-caching
malloc() and cpu and heap profiling utilities.

%package devel
Summary: Performance tools for C++
Group: Development/Libraries
Requires: %{NAME} = %{VERSION}

%description devel
The %name-devel package contains static and debug libraries and header
files for developing applications that use the %name package.

%changelog
    * Fri Mar 11 2005 <opensource@google.com>
    - First draft

%prep
%setup

%build
./configure
make prefix=%prefix

%install
rm -rf $RPM_BUILD_ROOT
make prefix=$RPM_BUILD_ROOT%{prefix} install

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root)

%doc AUTHORS COPYING ChangeLog INSTALL NEWS README TODO
%doc doc/index.html doc/designstyle.css
%doc doc/pprof.1 doc/pprof_remote_servers.html
%doc doc/tcmalloc.html
%doc doc/overview.dot doc/overview.gif
%doc doc/pageheap.dot doc/pageheap.gif
%doc doc/spanmap.dot doc/spanmap.gif 
%doc doc/threadheap.dot doc/threadheap.gif
%doc doc/t-test1.times.txt 
%doc doc/tcmalloc-opspercpusec.vs.threads.1024.bytes.png doc/tcmalloc-opspercpusec.vs.threads.128.bytes.png doc/tcmalloc-opspercpusec.vs.threads.131072.bytes.png doc/tcmalloc-opspercpusec.vs.threads.16384.bytes.png doc/tcmalloc-opspercpusec.vs.threads.2048.bytes.png doc/tcmalloc-opspercpusec.vs.threads.256.bytes.png doc/tcmalloc-opspercpusec.vs.threads.32768.bytes.png doc/tcmalloc-opspercpusec.vs.threads.4096.bytes.png doc/tcmalloc-opspercpusec.vs.threads.512.bytes.png doc/tcmalloc-opspercpusec.vs.threads.64.bytes.png doc/tcmalloc-opspercpusec.vs.threads.65536.bytes.png doc/tcmalloc-opspercpusec.vs.threads.8192.bytes.png
%doc doc/tcmalloc-opspersec.vs.size.1.threads.png doc/tcmalloc-opspersec.vs.size.12.threads.png doc/tcmalloc-opspersec.vs.size.16.threads.png doc/tcmalloc-opspersec.vs.size.2.threads.png doc/tcmalloc-opspersec.vs.size.20.threads.png doc/tcmalloc-opspersec.vs.size.3.threads.png doc/tcmalloc-opspersec.vs.size.4.threads.png doc/tcmalloc-opspersec.vs.size.5.threads.png doc/tcmalloc-opspersec.vs.size.8.threads.png 
%doc doc/heapprofile.html doc/heap-example1.png doc/heap_checker.html
%doc doc/cpuprofile.html doc/cpuprofile-fileformat.html
%doc doc/pprof-test-big.gif doc/pprof-test.gif doc/pprof-vsnprintf-big.gif doc/pprof-vsnprintf.gif

%{prefix}/lib/libtcmalloc.so.0
%{prefix}/lib/libtcmalloc.so.0.0.0
%{prefix}/lib/libtcmalloc_minimal.so.0
%{prefix}/lib/libtcmalloc_minimal.so.0.0.0
%{prefix}/lib/libprofiler.so.0
%{prefix}/lib/libprofiler.so.0.0.0
%{prefix}/bin/pprof
%{prefix}/man/man1/pprof.1.gz

%files devel
%defattr(-,root,root)

%{prefix}/include/google
%{prefix}/lib/libtcmalloc.a
%{prefix}/lib/libtcmalloc.la
%{prefix}/lib/libtcmalloc.so
%{prefix}/lib/libtcmalloc_minimal.a
%{prefix}/lib/libtcmalloc_minimal.la
%{prefix}/lib/libtcmalloc_minimal.so
%{prefix}/lib/libprofiler.a
%{prefix}/lib/libprofiler.la
%{prefix}/lib/libprofiler.so
