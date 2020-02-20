# This Makefile script is invoked by copr to build source rpm
# See: https://docs.pagure.org/copr.copr/user_documentation.html#make-srpm

PROTOCOL_GIT_REPO = https://gitlab.freedesktop.org/spice/spice-protocol
BUILD = make automake autoconf autoconf-archive libtool xz git rpm-build

srpm:
	dnf install -y $(BUILD)

	# get upstream spice protocol
	git clone $(PROTOCOL_GIT_REPO)
	cd spice-protocol && ./autogen.sh --prefix=/usr/ && make install

	# get other dependencies for project excluding spice-protocol
	dnf install -y `sed '/^BuildRequires:/!d; s/.*://; s/\bspice-protocol\b//; s/>.*//' *.spec.in`

	# create source rpm
	./autogen.sh
	sed -i -E "s/(^Release:[[:space:]]*)([^%]*)/\1`date +'%Y%m%d%H%M.spice.latest'`/" *.spec
	make dist
	rpmbuild -bs *.spec --define "_sourcedir $$PWD" --define "_srcrpmdir $(outdir)"
