/* Physical 0x10000000: kept resident and reserved for the VPU at handoff. */
MEMORY { ram (rwx) : ORIGIN = 0x90000000, LENGTH = 256K }
PHDRS {
  text PT_LOAD FLAGS(0x7);
  data PT_LOAD FLAGS(0x6);
}
GROUP(-lgcc)
SECTIONS {
  _start = .;
  .vectorTable : { . = 0x0; KEEP(*(.data.vectorTable)) } :text
  .text : {
    _text = .;
    KEEP(*(.text.start))
    *(.text) *(.text.*) *(.gnu.warning)
    _etext = .;
  } :text
  .rodata : { *(.rodata) *(.rodata.*) _erodata = .; } :text
  commands : { PROVIDE(__start_commands = .); KEEP(*(commands)) PROVIDE(__stop_commands = .); } >ram :text
  fs_impl : { PROVIDE(__start_fs_impl = .); KEEP(*(fs_impl)) PROVIDE(__stop_fs_impl = .); } >ram :text
  usb_hooks : { PROVIDE(__start_usb_hooks = .); KEEP(*(usb_hooks)) PROVIDE(__stop_usb_hooks = .); } >ram :text
  .init_array : {
    . = ALIGN(16);
    PROVIDE(__ctor_list = .);
    KEEP(*(.init_array)) KEEP(*(.init_array.*)) KEEP(*(.ctors)) KEEP(*(.ctors.*))
    __ctor_end = .;
  } :text
  . = ALIGN(CONSTANT (MAXPAGESIZE));
  .data : { *(.data) *(.data.*) _edata = .; } :data
  .bss : {
    . = ALIGN(4); _fbss = .;
    *(.bss) *(.bss.*) *(COMMON)
    . = ALIGN(4); _ebss = .; _end = .;
  } :data
  _fstack = ORIGIN(ram) + LENGTH(ram) - 4;
}
