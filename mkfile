kerndir = /sys/src/9
projdir = `{pwd}

build:
	cd $kerndir/pc64
	mk install 'CONF=qemu'
	cd $projdir

bind: ethersndvirtio.c
	bind -a $projdir /sys/src/9/pc
