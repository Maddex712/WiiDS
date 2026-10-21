/* Apple ld has no --defsym; dsptask.c needs lbl_80004600 to link.
 * DSP HLE does not execute the retail microcode that used the GC address. */
unsigned char lbl_80004600[4];
