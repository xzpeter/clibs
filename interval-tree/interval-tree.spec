Summary: A interval tree test program
Name: interval-tree-test
Version: 0.1
Release: 1
License: GPL
Group: Applications/Misc
URL: http://xzpeter.org

%description
This is a test program for the interval tree library.
Nothing else.

%prep
mkdir -p %{_builddir}/interval-tree
tar zxvf %{tarball} -C %{_builddir}/interval-tree

%build
cd interval-tree
make

%install
cd interval-tree
mkdir -p %{buildroot}/usr/local/bin
cp tree-test %{buildroot}/usr/local/bin

%clean
rm -rf %{buildroot}

%files
%defattr(-,root,root,-)
/usr/local/bin/tree-test

%changelog
* Wed Jun 13 2018 peterx <peterx@xz-mi> - tree-1
- Initial build.

