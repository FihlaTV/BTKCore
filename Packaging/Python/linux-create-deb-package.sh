# Alien is used to create the DEB pacakge from the RPM package generated by setuptools
# But the 'debian/control' file must be adapted as:
# - The mail adress for the Maintainer is not recognized by the Ubuntu software center (maintainer-address-malformed error)
# - The dependencies for the RPM package are not added in the DEB package
# - Property 'Section' modified (alien -> python)
# - Property 'Priority' modified (extra -> optional)
# - Property 'Homepage' added 
BTK_PYTHON_RPM_PACKAGE_PATH=`find . -name "*.rpm" | head -n1`
TEMP_DEB_PACKAGE_PATH='./temp-deb-package'
mkdir $TEMP_DEB_PACKAGE_PATH
cd $TEMP_DEB_PACKAGE_PATH
fakeroot alien --scripts --keep-version --single ../$BTK_PYTHON_RPM_PACKAGE_PATH >/dev/null 2>&1
cd $(find . -name "python-btk*" -type d)
sed -i 's/Maintainer:.*/Maintainer: Arnaud Barré <arnaud.barre@gmail.com>/' debian/control
sed -i 's/Depends:.*/Depends: ${shlibs:Depends}, python-numpy (>= 1.6.2)/' debian/control
sed -i 's/Section:.*/Section: python/' debian/control
sed -i 's/Priority:.*/Priority: optional/' debian/control
sed -i '9iHomepage: http://b-tk.googlecode.com' debian/control
sed -i -n -e :a -e '1,2!{P;N;D;};N;ba' debian/control
dpkg-buildpackage >/dev/null 2>&1
cd ..
find . -name "*.deb" | xargs -i mv '{}' ../'{}'
cd ..
rm -R './temp-deb-package'