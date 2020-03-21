# This Makefile script is invoked by copr to build source rpm
# See: https://docs.pagure.org/copr.copr/user_documentation.html#make-srpm

PROTOCOL_GIT_REPO = https://gitlab.freedesktop.org/spice/spice-protocol
BUILD = xz git rpm-build

srpm:
	dnf install -y $(BUILD)

	# get upstream spice protocol
	git clone $(PROTOCOL_GIT_REPO)
	meson --buildtype=release spice-protocol build-spice-protocol --prefix=/usr --werror
	ninja -C build-spice-protocol install

	# get other dependencies for project excluding spice-protocol
	dnf install -y `sed '/^BuildRequires:/!d; s/.*://; s/\bspice-protocol\b//; s/>.*//' *.spec.in`

	# set project version
	sed -i -E "s/^( +version : )'[0-9\.]+'/\\1'$$(date +'%Y%m%d%H%M.spice.latest')'/" meson.build
	git add meson.build
	git commit -m 'automatic version update'

	# create source rpm
	rm -rf build && mkdir build
	meson . build --prefix=/usr --werror
	if ! test -r ../spice-common.git; then DIR=`basename "$$PWD"`; ln -s "$$DIR/.git/modules/spice-common" ../spice-common.git; fi
	ninja -C build dist
	rpmbuild -ts build/meson-dist/spice-streaming-agent-*.tar.xz --define "_srcrpmdir $(outdir)"
