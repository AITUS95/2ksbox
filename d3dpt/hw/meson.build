# 2ksbox: the d3dpt-vga display adapter (doc 15; overlay of d3dpt/hw),
# which reuses the VGA core, and the executor loader it opens.
i386_system_ss.add(files('d3dpt_exec_load.c'))
i386_system_ss.add(when: 'CONFIG_VGA', if_true: files('d3dpt_vga.c'))
# ... and in aarch64's QEMU too, for Windows 11 on Arm's WDDM driver
# (track M25; the virt board's PCI has VGA_PCI, so CONFIG_VGA)
arm_system_ss.add(files('d3dpt_exec_load.c'))
arm_system_ss.add(when: 'CONFIG_VGA', if_true: files('d3dpt_vga.c'))
